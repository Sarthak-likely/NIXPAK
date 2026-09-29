/* main_fb.c — DRM/KMS fullscreen front-end for NIXPAK, glibc-only. */
#include "common.h"
#include "gfx.h"
#include "input.h"
#include "ui.h"
#include "font8x16.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>

#define HDR_H     28
#define STATUS_H  22
#define LINE_H    18
#define PAD       10

typedef enum {
    V_MENU = 0, V_SEARCH, V_LIST, V_CONFIRM, V_BUSY, V_QUIT
} fb_view_t;

typedef struct {
    fb_view_t view;
    fb_view_t return_to;
    char query[128];
    size_t qlen;
    pkg_result_t *results;
    size_t n_results;
    size_t selected;
    size_t scroll;

    char status[256];
    char confirm_msg[256];
    char confirm_id[300];
    int  confirm_action;

    int menu_selected;
    int mouse_x, mouse_y;
} fb_app_t;

static gfx_t    g_gfx;
static fb_app_t g_app;
static volatile sig_atomic_t g_quit = 0;

static void on_sig(int sig) { (void)sig; g_quit = 1; }

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_app.status, sizeof(g_app.status), fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_header(const char *title) {
    surface_t *s = &g_gfx.back;
    surf_fill_rect(s, 0, 0, s->width, HDR_H, C_HEADER);
    surf_fill_rect(s, 0, HDR_H - 1, s->width, 1, C_ACCENT);
    int tx = PAD;
    int ty = (HDR_H - FONT_H) / 2;
    surf_draw_text(s, tx, ty, "NIXPAK", C_ACCENT, C_HEADER);
    tx += surf_text_width("NIXPAK") + PAD * 2;
    surf_draw_text(s, tx, ty, "|", C_FG_DIM, C_HEADER);
    tx += surf_text_width("|") + PAD;
    surf_draw_text(s, tx, ty, title, C_FG, C_HEADER);
}

static void draw_status(void) {
    surface_t *s = &g_gfx.back;
    int y = s->height - STATUS_H;
    surf_fill_rect(s, 0, y, s->width, STATUS_H, C_BG_ALT);
    surf_fill_rect(s, 0, y, s->width, 1, C_BORDER);
    int ty = y + (STATUS_H - FONT_H) / 2;
    surf_draw_text_n(s, PAD, ty, g_app.status, (s->width - 2 * PAD) / FONT_W,
                     C_FG_DIM, C_BG_ALT);
}

static void draw_menu(void) {
    surface_t *s = &g_gfx.back;
    surf_clear(s, C_BG);
    draw_header("main menu");

    const char *items[] = {
        "Search packages",
        "System: test configuration",
        "System: switch configuration",
        "System: boot configuration",
        "System: rollback",
        "Garbage collect",
        "Quit"
    };
    const char *descs[] = {
        "Search Nixpkgs and install/remove packages",
        "Build configuration, activate temporarily",
        "Build, activate, and set as boot default",
        "Build and set as boot default, no activation",
        "Revert to the previous system generation",
        "Delete old generations and unreferenced store paths",
        "Exit NIXPAK"
    };
    int n = (int)(sizeof(items) / sizeof(items[0]));
    int y0 = HDR_H + PAD * 2;
    for (int i = 0; i < n; i++) {
        int y = y0 + i * (LINE_H + PAD);
        int h = LINE_H + PAD;
        if (i == g_app.menu_selected)
            surf_fill_rect(s, PAD, y, s->width - 2 * PAD, h, C_SEL_BG);
        uint32_t fg = (i == g_app.menu_selected) ? C_SEL_FG : C_FG;
        uint32_t bg = (i == g_app.menu_selected) ? C_SEL_BG : C_BG;
        surf_draw_text(s, PAD * 2, y + PAD / 2, items[i], fg, bg);
        surf_draw_text(s, PAD * 2, y + PAD / 2 + LINE_H, descs[i], C_FG_DIM, bg);
    }
}

static void draw_search(void) {
    surface_t *s = &g_gfx.back;
    surf_clear(s, C_BG);
    draw_header("search");
    int y = HDR_H + PAD * 3;
    surf_draw_text(s, PAD, y, "Query:", C_FG_DIM, C_BG);
    y += LINE_H + PAD;
    int box_x = PAD, box_y = y, box_w = s->width - PAD * 2, box_h = 40;
    surf_fill_rect(s, box_x, box_y, box_w, box_h, C_BG_ALT);
    surf_stroke_rect(s, box_x, box_y, box_w, box_h, C_ACCENT, 1);
    surf_draw_text(s, box_x + 10, box_y + (box_h - FONT_H) / 2, g_app.query,
                   C_FG, C_BG_ALT);
    int cx = box_x + 10 + (int)g_app.qlen * FONT_W;
    if (cx < box_x + box_w - 4)
        surf_fill_rect(s, cx, box_y + 6, 1, box_h - 12, C_ACCENT);
}

static void draw_list(void) {
    surface_t *s = &g_gfx.back;
    surf_clear(s, C_BG);
    char title[200];
    snprintf(title, sizeof(title), "results: %s  (%zu)", g_app.query, g_app.n_results);
    draw_header(title);

    int list_y = HDR_H + PAD;
    int row_h = LINE_H + 6;
    int visible = (s->height - list_y - STATUS_H - PAD) / row_h;
    if (visible < 1) visible = 1;
    if (g_app.selected < g_app.scroll) g_app.scroll = g_app.selected;
    if (g_app.selected >= g_app.scroll + (size_t)visible)
        g_app.scroll = g_app.selected - (size_t)visible + 1;

    if (g_app.n_results == 0) {
        surf_draw_text(s, PAD, list_y + PAD, "No results.", C_FG_DIM, C_BG);
        return;
    }
    for (int i = 0; i < visible; i++) {
        size_t idx = g_app.scroll + (size_t)i;
        if (idx >= g_app.n_results) break;
        pkg_result_t *r = &g_app.results[idx];
        int y = list_y + i * row_h;
        int sel = ((size_t)idx == g_app.selected);
        uint32_t bg = sel ? C_SEL_BG : (i & 1) ? C_BG_ALT : C_BG;
        surf_fill_rect(s, PAD, y, s->width - 2 * PAD, row_h, bg);
        uint32_t tag_color = (r->source == SRC_FLATPAK) ? C_FLATPAK : C_ACCENT;
        surf_fill_rect(s, PAD + 4, y + 4, 6, row_h - 8, tag_color);
        uint32_t fg = sel ? C_SEL_FG : C_FG;
        int text_x = PAD + 16;
        int max_chars = (s->width - text_x - PAD) / FONT_W;
        char line[600];
        snprintf(line, sizeof(line), "%-35.35s  %s", r->name, r->description);
        surf_draw_text_n(s, text_x, y + 3, line, max_chars, fg, bg);
        if (nixpkg_check_installed(r->app_id, r->source)) {
            const char *tag = "installed";
            int tw = surf_text_width(tag);
            surf_draw_text(s, s->width - PAD - tw - 8, y + 3, tag, C_OK, bg);
        }
    }
}

static void draw_confirm(void) {
    surface_t *s = &g_gfx.back;
    draw_list();
    for (size_t i = 0; i < (size_t)s->width * s->height; i++) {
        uint32_t p = s->pixels[i];
        uint32_t r = ((p >> 16) & 0xFF) / 2;
        uint32_t g = ((p >>  8) & 0xFF) / 2;
        uint32_t b = ( p        & 0xFF) / 2;
        s->pixels[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    int bw = 640, bh = 180;
    if (bw > s->width - 40)  bw = s->width - 40;
    if (bh > s->height - 40) bh = s->height - 40;
    int bx = (s->width - bw) / 2;
    int by = (s->height - bh) / 2;
    surf_fill_rect(s, bx, by, bw, bh, C_BG_ALT);
    surf_stroke_rect(s, bx, by, bw, bh, C_ACCENT, 2);
    surf_draw_text(s, bx + PAD, by + PAD, g_app.confirm_msg, C_FG, C_BG_ALT);
    surf_draw_text(s, bx + PAD, by + PAD + LINE_H + 4, g_app.confirm_id,
                   C_ACCENT, C_BG_ALT);
    surf_draw_text(s, bx + PAD, by + bh - LINE_H - PAD,
                   "[ Enter ] confirm      [ Esc ] cancel", C_FG_DIM, C_BG_ALT);
}

static void draw_busy(void) {
    surface_t *s = &g_gfx.back;
    draw_list();
    for (size_t i = 0; i < (size_t)s->width * s->height; i++) {
        uint32_t p = s->pixels[i];
        uint32_t r = ((p >> 16) & 0xFF) * 2 / 3;
        uint32_t g = ((p >>  8) & 0xFF) * 2 / 3;
        uint32_t b = ( p        & 0xFF) * 2 / 3;
        s->pixels[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    const char *msg = g_app.confirm_msg[0] ? g_app.confirm_msg : "Working...";
    int tw = surf_text_width(msg);
    int bw = tw + PAD * 4, bh = 60;
    int bx = (s->width - bw) / 2, by = (s->height - bh) / 2;
    surf_fill_rect(s, bx, by, bw, bh, C_BG_ALT);
    surf_stroke_rect(s, bx, by, bw, bh, C_ACCENT, 2);
    surf_draw_text(s, bx + PAD * 2, by + (bh - FONT_H) / 2, msg, C_FG, C_BG_ALT);
}

static void render(void) {
    switch (g_app.view) {
        case V_MENU:    draw_menu();    break;
        case V_SEARCH:  draw_search();  break;
        case V_LIST:    draw_list();    break;
        case V_CONFIRM: draw_confirm(); break;
        case V_BUSY:    draw_busy();    break;
        case V_QUIT:    break;
    }
    draw_status();
    gfx_present(&g_gfx);
}

/* ------------------------------------------------------------------ */
/* operations                                                          */
/* ------------------------------------------------------------------ */

static int run_search_blocking(void) {
    surface_t *s = &g_gfx.back;
    surf_clear(s, C_BG);
    draw_header("searching...");
    surf_draw_text(s, PAD, HDR_H + PAD,
                   "Running nix search. This may take a few seconds.", C_FG, C_BG);
    surf_draw_text(s, PAD, HDR_H + PAD + LINE_H,
                   "Results will appear in the list.", C_FG_DIM, C_BG);
    gfx_present(&g_gfx);

    free(g_app.results);
    g_app.results = NULL;
    g_app.n_results = 0;
    g_app.selected = 0;
    g_app.scroll = 0;
    int rc = nixpkg_search(g_app.query, &g_app.results, &g_app.n_results);
    if (rc != 0) { set_status("search failed"); return -1; }
    set_status("found %zu results", g_app.n_results);
    return 0;
}

static int do_confirmed_action(void) {
    const char *action = (g_app.confirm_action == 1) ? "remove" : "install";
    g_app.view = V_BUSY;
    snprintf(g_app.confirm_msg, sizeof(g_app.confirm_msg),
             "Running nixpkg %s %s ...", action, g_app.confirm_id);
    render();
    int rc = nixpkg_mutate(action, g_app.confirm_id);
    if (rc == 0) set_status("%s ok: %s", action, g_app.confirm_id);
    else         set_status("%s failed: %s", action, g_app.confirm_id);
    return rc;
}

static void do_system_op(int which) {
    g_app.view = V_BUSY;
    const char *labels[] = {
        "nixos-rebuild test ...",
        "nixos-rebuild switch ...",
        "nixos-rebuild boot ...",
        "rolling back ...",
        "garbage collecting ..."
    };
    snprintf(g_app.confirm_msg, sizeof(g_app.confirm_msg), "%s", labels[which]);
    render();
    int rc = 0;
    switch (which) {
        case 0: rc = nixpkg_rebuild("test");   break;
        case 1: rc = nixpkg_rebuild("switch"); break;
        case 2: rc = nixpkg_rebuild("boot");   break;
        case 3: rc = nixpkg_rollback();        break;
        case 4: rc = nixpkg_gc();              break;
    }
    set_status("operation %s", rc == 0 ? "ok" : "failed");
    g_app.view = V_MENU;
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

int fb_main(int argc, char **argv) {
    (void)argc; (void)argv;
    memset(&g_app, 0, sizeof(g_app));
    g_app.view = V_MENU;

    if (gfx_open(&g_gfx) != 0) return 1;
    if (input_open() != 0) { gfx_close(&g_gfx); return 1; }

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    set_status("ready - arrow keys navigate, Enter selects, q quits");

    while (!g_quit && g_app.view != V_QUIT) {
        render();
        input_event_t ev;
        int r = input_poll(&ev, 200);
        if (r <= 0) continue;

        if (g_app.view == V_MENU) {
            const int n = 7;
            if (ev.key == IKEY_UP && g_app.menu_selected > 0) g_app.menu_selected--;
            if (ev.key == IKEY_DOWN && g_app.menu_selected + 1 < n) g_app.menu_selected++;
            if (ev.key == IKEY_CHAR && (ev.ch == 'q' || ev.ch == 'Q')) g_app.view = V_QUIT;
            if (ev.key == IKEY_ENTER) {
                switch (g_app.menu_selected) {
                    case 0: g_app.view = V_SEARCH; g_app.query[0] = 0; g_app.qlen = 0; break;
                    case 1: do_system_op(0); break;
                    case 2: do_system_op(1); break;
                    case 3: do_system_op(2); break;
                    case 4: do_system_op(3); break;
                    case 5: do_system_op(4); break;
                    case 6: g_app.view = V_QUIT; break;
                }
            }
            if (ev.key == IKEY_ESC) g_app.view = V_QUIT;
        } else if (g_app.view == V_SEARCH) {
            if (ev.key == IKEY_ESC) g_app.view = V_MENU;
            else if (ev.key == IKEY_BACKSPACE) {
                if (g_app.qlen > 0) g_app.query[--g_app.qlen] = 0;
            } else if (ev.key == IKEY_CHAR) {
                if (ev.ch && g_app.qlen + 1 < sizeof(g_app.query)) {
                    g_app.query[g_app.qlen++] = ev.ch;
                    g_app.query[g_app.qlen] = 0;
                }
            } else if (ev.key == IKEY_ENTER) {
                if (g_app.qlen > 0) {
                    if (run_search_blocking() == 0) {
                        g_app.view = V_LIST;
                        g_app.selected = 0;
                    }
                }
            }
        } else if (g_app.view == V_LIST) {
            if (ev.key == IKEY_ESC) g_app.view = V_SEARCH;
            else if (ev.key == IKEY_UP   && g_app.selected > 0) g_app.selected--;
            else if (ev.key == IKEY_DOWN && g_app.selected + 1 < g_app.n_results) g_app.selected++;
            else if (ev.key == IKEY_ENTER) {
                if (g_app.selected < g_app.n_results) {
                    pkg_result_t *r = &g_app.results[g_app.selected];
                    snprintf(g_app.confirm_id, sizeof(g_app.confirm_id), "%s", r->app_id);
                    g_app.confirm_action = nixpkg_check_installed(r->app_id, r->source) ? 1 : 0;
                    snprintf(g_app.confirm_msg, sizeof(g_app.confirm_msg),
                             g_app.confirm_action == 1
                                ? "Remove this package?"
                                : "Install this package?");
                    g_app.return_to = V_LIST;
                    g_app.view = V_CONFIRM;
                }
            } else if (ev.key == IKEY_CHAR && (ev.ch == 'd' || ev.ch == 'D')) {
                if (g_app.selected < g_app.n_results) {
                    pkg_result_t *r = &g_app.results[g_app.selected];
                    snprintf(g_app.confirm_id, sizeof(g_app.confirm_id), "%s", r->app_id);
                    g_app.confirm_action = 1;
                    snprintf(g_app.confirm_msg, sizeof(g_app.confirm_msg), "Remove this package?");
                    g_app.return_to = V_LIST;
                    g_app.view = V_CONFIRM;
                }
            }
        } else if (g_app.view == V_CONFIRM) {
            if (ev.key == IKEY_ESC) g_app.view = g_app.return_to;
            else if (ev.key == IKEY_ENTER) {
                do_confirmed_action();
                g_app.view = g_app.return_to;
            }
        }
    }

    free(g_app.results);
    input_close();
    gfx_close(&g_gfx);
    return 0;
}
