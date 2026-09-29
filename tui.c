/* tui.c — termios raw-mode TUI for NIXPAK, glibc-only.
 * Cell-diffed renderer, 256-color SGR, details view. */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <locale.h>

/* ------------------------------------------------------------------ */
/* terminal state                                                      */
/* ------------------------------------------------------------------ */

static struct termios g_saved_termios;
static int g_raw = 0;
static int g_utf8 = 1;

static void restore_term(void) {
    if (g_raw) {
        const char *s = "\x1b[0m\x1b[?1049l\x1b[?25h";
        ssize_t r = write(STDOUT_FILENO, s, strlen(s));
        (void)r;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
        g_raw = 0;
    }
}

static void on_signal(int sig) {
    (void)sig;
    restore_term();
    _exit(130);
}

static int enter_raw(void) {
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) return -1;
    struct termios raw = g_saved_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    g_raw = 1;
    const char *s = "\x1b[?1049h\x1b[?25l";
    ssize_t r = write(STDOUT_FILENO, s, strlen(s));
    (void)r;
    atexit(restore_term);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP,  on_signal);
    return 0;
}

static void term_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

static void detect_utf8(void) {
    const char *lang = getenv("LC_ALL");
    if (!lang || !*lang) lang = getenv("LC_CTYPE");
    if (!lang || !*lang) lang = getenv("LANG");
    if (!lang) { g_utf8 = 0; return; }
    if (strstr(lang, "UTF-8") || strstr(lang, "UTF8") ||
        strstr(lang, "utf-8") || strstr(lang, "utf8")) {
        g_utf8 = 1;
    } else {
        g_utf8 = 0;
    }
}

/* ------------------------------------------------------------------ */
/* chrome glyphs                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *tl, *tr, *bl, *br;
    const char *h, *v;
    const char *lt, *rt;
} chrome_t;

static const chrome_t CHROME_UNI = { "┌","┐","└","┘","─","│","├","┤" };
static const chrome_t CHROME_ASC = { "+","+","+","+","-","|","+","+" };
static chrome_t g_chrome;

/* ------------------------------------------------------------------ */
/* 256-color palette                                                   */
/* ------------------------------------------------------------------ */

typedef unsigned char c256_t;

#define P_BG         235
#define P_BG_ALT     236
#define P_BG_SEL     25
#define P_HEADER     24
#define P_FG         255
#define P_FG_DIM     245
#define P_FG_SEL     231
#define P_ACCENT     75
#define P_FLATPAK    67
#define P_OK         78

/* ------------------------------------------------------------------ */
/* cell grid                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    char          ch[4];
    c256_t        fg;
    c256_t        bg;
    unsigned char bold : 1;
    unsigned char dim  : 1;
    unsigned char pad  : 6;
} cell_t;

static cell_t *g_prev = NULL;
static cell_t *g_cur  = NULL;
static int     g_rows = 0, g_cols = 0;
static int     g_need_full_clear = 1;

static void grid_free(void) {
    free(g_prev); g_prev = NULL;
    free(g_cur);  g_cur  = NULL;
    g_rows = g_cols = 0;
}

static int grid_alloc(int rows, int cols) {
    if (rows == g_rows && cols == g_cols && g_prev && g_cur) return 0;
    grid_free();
    size_t n = (size_t)rows * (size_t)cols;
    g_prev = calloc(n, sizeof(cell_t));
    g_cur  = calloc(n, sizeof(cell_t));
    if (!g_prev || !g_cur) { grid_free(); return -1; }
    g_rows = rows;
    g_cols = cols;
    g_need_full_clear = 1;
    return 0;
}

static cell_t *cell_at(int r, int c) {
    if (r < 0 || r >= g_rows || c < 0 || c >= g_cols) return NULL;
    return &g_cur[(size_t)r * g_cols + c];
}

static void cell_put(int r, int c, const char *glyph_utf8,
                     c256_t fg, c256_t bg, int bold, int dim) {
    cell_t *cell = cell_at(r, c);
    if (!cell) return;
    size_t glen = strlen(glyph_utf8);
    if (glen > 3) glen = 3;
    memset(cell->ch, 0, 4);
    memcpy(cell->ch, glyph_utf8, glen);
    cell->fg = fg;
    cell->bg = bg;
    cell->bold = (unsigned char)!!bold;
    cell->dim  = (unsigned char)!!dim;
    cell->pad  = 0;
}

static void cell_fill(int r, int c0, int n, const char *glyph,
                      c256_t fg, c256_t bg) {
    for (int i = 0; i < n; i++) cell_put(r, c0 + i, glyph, fg, bg, 0, 0);
}

static void grid_clear(c256_t fg, c256_t bg) {
    for (int r = 0; r < g_rows; r++)
        for (int c = 0; c < g_cols; c++)
            cell_put(r, c, " ", fg, bg, 0, 0);
}

static int cell_text(int r, int c, const char *s, c256_t fg, c256_t bg,
                     int bold, int dim) {
    int written = 0;
    const unsigned char *p = (const unsigned char*)s;
    while (*p && c + written < g_cols) {
        int L = 1;
        if (*p >= 0x80) {
            if ((*p & 0xE0) == 0xC0) L = 2;
            else if ((*p & 0xF0) == 0xE0) L = 3;
            else if ((*p & 0xF8) == 0xF0) L = 4;
            else L = 1;
        }
        int k;
        for (k = 0; k < L; k++) if (!p[k]) break;
        if (k < L) break;
        char glyph[5] = {0,0,0,0,0};
        memcpy(glyph, p, L);
        cell_put(r, c + written, glyph, fg, bg, bold, dim);
        p += L;
        written++;
    }
    return written;
}

/* ------------------------------------------------------------------ */
/* frame present                                                       */
/* ------------------------------------------------------------------ */

static void frame_flush(void) {
    ssize_t w;
    const char *reset = "\x1b[0m";
    w = write(1, reset, strlen(reset)); (void)w;

    if (g_need_full_clear) {
        const char *clr = "\x1b[2J";
        w = write(1, clr, strlen(clr)); (void)w;
        memset(g_prev, 0, (size_t)g_rows * g_cols * sizeof(cell_t));
        g_need_full_clear = 0;
    }

    int cur_fg = -1, cur_bg = -1, cur_bold = 0, cur_dim = 0;
    char seq[64];

    for (int r = 0; r < g_rows; r++) {
        for (int c = 0; c < g_cols; c++) {
            size_t idx = (size_t)r * g_cols + c;
            cell_t *a = &g_prev[idx];
            cell_t *b = &g_cur[idx];
            if (!memcmp(a, b, sizeof(cell_t))) continue;

            int n = snprintf(seq, sizeof(seq), "\x1b[%d;%dH", r + 1, c + 1);
            w = write(1, seq, n); (void)w;

            if (cur_fg != b->fg || cur_bg != b->bg ||
                cur_bold != b->bold || cur_dim != b->dim) {
                int sn = 0;
                sn += snprintf(seq + sn, sizeof(seq) - sn, "\x1b[");
                if (b->bold) sn += snprintf(seq + sn, sizeof(seq) - sn, "1;");
                if (b->dim)  sn += snprintf(seq + sn, sizeof(seq) - sn, "2;");
                sn += snprintf(seq + sn, sizeof(seq) - sn, "38;5;%d;48;5;%dm",
                               b->fg, b->bg);
                w = write(1, seq, sn); (void)w;
                cur_fg = b->fg; cur_bg = b->bg;
                cur_bold = b->bold; cur_dim = b->dim;
            }

            size_t glen = strlen(b->ch);
            if (glen == 0) { char sp = ' '; w = write(1, &sp, 1); (void)w; }
            else            { w = write(1, b->ch, glen); (void)w; }

            *a = *b;
        }
    }
    w = write(1, reset, strlen(reset)); (void)w;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    KEY_NONE = 0,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_ENTER, KEY_ESC, KEY_BACKSPACE,
    KEY_CHAR, KEY_TAB
} key_t;

static key_t read_key(char *out_char) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return KEY_NONE;
    if (c == '\x1b') {
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) <= 0) return KEY_ESC;
        if (read(STDIN_FILENO, &seq[1], 1) <= 0) return KEY_ESC;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
            }
        }
        return KEY_ESC;
    }
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == '\t') return KEY_TAB;
    if (c == 127 || c == 8) return KEY_BACKSPACE;
    if (c < 32) return KEY_NONE;
    *out_char = (char)c;
    return KEY_CHAR;
}

/* ------------------------------------------------------------------ */
/* app state                                                           */
/* ------------------------------------------------------------------ */

typedef enum { V_MENU, V_SEARCH, V_LIST, V_DETAILS, V_CONFIRM, V_LOG } view_t;

typedef struct {
    view_t view;
    view_t return_to;
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

    char **log_lines;
    size_t n_log;
    size_t log_cap;

    /* details view state */
    pkg_details_t details;
    pkg_source_t  details_src;
    char          details_id[NIXPKG_MAX_APP];
    size_t        details_scroll;
} app_t;

static void log_append(app_t *a, const char *line) {
    if (!a->log_lines) {
        a->log_cap = 256;
        a->log_lines = calloc(a->log_cap, sizeof(char*));
        if (!a->log_lines) return;
    }
    if (a->n_log == a->log_cap) {
        size_t nc = a->log_cap * 2;
        char **nl = realloc(a->log_lines, nc * sizeof(char*));
        if (!nl) return;
        a->log_lines = nl;
        a->log_cap = nc;
    }
    a->log_lines[a->n_log++] = strdup(line);
}

static void log_clear(app_t *a) {
    for (size_t i = 0; i < a->n_log; i++) free(a->log_lines[i]);
    free(a->log_lines);
    a->log_lines = NULL;
    a->n_log = 0;
    a->log_cap = 0;
}

static void set_status(app_t *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof(a->status), fmt, ap);
    va_end(ap);
}

static const char *last_log_line(const app_t *a) {
    for (size_t i = a->n_log; i > 0; i--) {
        if (a->log_lines[i-1] && a->log_lines[i-1][0] != 0)
            return a->log_lines[i-1];
    }
    return "(no output)";
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_header(const char *subtitle) {
    cell_fill(0, 0, g_cols, " ", P_FG, P_HEADER);
    cell_text(0, 1, " NIXPAK ", P_ACCENT, P_HEADER, 1, 0);
    cell_text(0, 9, subtitle, P_FG, P_HEADER, 0, 1);
}

static void draw_footer(const app_t *a, const char *hints) {
    int r = g_rows - 1;
    cell_fill(r, 0, g_cols, " ", P_FG_DIM, P_BG_ALT);
    cell_text(r, 1, hints, P_FG_DIM, P_BG_ALT, 0, 0);
    int slen = (int)strlen(a->status);
    if (slen > 0 && slen < g_cols - 2) {
        int start = g_cols - slen - 1;
        if (start > 0) cell_text(r, start, a->status, P_FG, P_BG_ALT, 0, 0);
    }
}

static void draw_menu(const app_t *a) {
    grid_clear(P_FG, P_BG);
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
        "Search Nixpkgs and install or remove packages",
        "Build configuration and activate temporarily",
        "Build, activate, and make default boot target",
        "Build and make default boot target, no activation",
        "Revert to the previous system generation",
        "Delete old generations and unreferenced store paths",
        "Exit NIXPAK"
    };
    int n = (int)(sizeof(items)/sizeof(items[0]));

    for (int i = 0; i < n; i++) {
        int y = 2 + i * 3;
        if (y + 1 >= g_rows - 1) break;
        int selected = (i == a->menu_selected);
        c256_t bg = selected ? P_BG_SEL : P_BG;
        c256_t fg = selected ? P_FG_SEL : P_FG;

        char line[80];
        snprintf(line, sizeof(line), "%d. %s", i + 1, items[i]);
        int cx = 2;
        cell_text(y, cx, line, fg, bg, selected ? 1 : 0, 0);
        int used = (int)strlen(line);
        for (int k = cx + used; k < g_cols - 1; k++)
            cell_put(y, k, " ", fg, bg, 0, 0);
        cell_text(y + 1, 5, descs[i], P_FG_DIM, P_BG, 0, 0);
    }

    draw_footer(a, " Up/Down move   Enter select   q quit");
}

static void draw_search(const app_t *a) {
    grid_clear(P_FG, P_BG);
    draw_header("search");

    int y = 2;
    cell_text(y, 2, "Query", P_FG_DIM, P_BG, 0, 0);
    y++;

    int box_x = 2, box_w = g_cols - 4;
    if (box_w < 20) box_w = 20;
    if (box_x + box_w > g_cols) box_w = g_cols - box_x;

    cell_put(y, box_x, g_chrome.tl, P_ACCENT, P_BG, 0, 0);
    for (int i = 1; i < box_w - 1; i++)
        cell_put(y, box_x + i, g_chrome.h, P_ACCENT, P_BG, 0, 0);
    cell_put(y, box_x + box_w - 1, g_chrome.tr, P_ACCENT, P_BG, 0, 0);
    y++;

    cell_put(y, box_x, g_chrome.v, P_ACCENT, P_BG_ALT, 0, 0);
    for (int i = 1; i < box_w - 1; i++)
        cell_put(y, box_x + i, " ", P_FG, P_BG_ALT, 0, 0);
    cell_put(y, box_x + box_w - 1, g_chrome.v, P_ACCENT, P_BG_ALT, 0, 0);
    int inner = box_w - 2;
    cell_text(y, box_x + 1, a->query, P_FG, P_BG_ALT, 0, 0);
    int cursor_x = box_x + 1 + (int)a->qlen;
    if (cursor_x < box_x + 1 + inner)
        cell_put(y, cursor_x, " ", P_BG_ALT, P_ACCENT, 0, 0);
    y++;

    cell_put(y, box_x, g_chrome.bl, P_ACCENT, P_BG, 0, 0);
    for (int i = 1; i < box_w - 1; i++)
        cell_put(y, box_x + i, g_chrome.h, P_ACCENT, P_BG, 0, 0);
    cell_put(y, box_x + box_w - 1, g_chrome.br, P_ACCENT, P_BG, 0, 0);
    y += 2;

    cell_text(y, 2, "Type a package name. Enter to search. Esc to go back.",
              P_FG_DIM, P_BG, 0, 0);

    draw_footer(a, " type   Enter search   Esc back   Backspace delete");
}

static void draw_list(const app_t *a) {
    grid_clear(P_FG, P_BG);

    char sub[240];
    snprintf(sub, sizeof(sub), "results for \"%s\"  (%zu)", a->query, a->n_results);
    draw_header(sub);

    int list_y = 2;
    int list_h = g_rows - list_y - 2;
    if (list_h < 1) list_h = 1;
    int row_h = 2;
    int visible = list_h / row_h;
    if (visible < 1) visible = 1;

    size_t scroll = a->scroll;
    if (a->selected < scroll) scroll = a->selected;
    if (a->selected >= scroll + (size_t)visible)
        scroll = a->selected - (size_t)visible + 1;

    if (a->n_results == 0) {
        cell_text(list_y + 1, 2, "No results.", P_FG_DIM, P_BG, 0, 0);
        draw_footer(a, " Esc back");
        return;
    }

    for (int i = 0; i < visible; i++) {
        size_t idx = scroll + (size_t)i;
        if (idx >= a->n_results) break;
        pkg_result_t *r = &a->results[idx];
        int y = list_y + i * row_h;
        int sel = ((size_t)idx == a->selected);
        c256_t bg = sel ? P_BG_SEL : ((i & 1) ? P_BG_ALT : P_BG);
        c256_t fg = sel ? P_FG_SEL : P_FG;
        c256_t dim = sel ? P_FG_SEL : P_FG_DIM;

        for (int k = 0; k < g_cols; k++)
            cell_put(y, k, " ", fg, bg, 0, 0);
        for (int k = 0; k < g_cols; k++)
            cell_put(y + 1, k, " ", dim, bg, 0, 0);

        const char *tag = (r->source == SRC_FLATPAK) ? "[FLAT]" : "[NIX]";
        c256_t tag_col = (r->source == SRC_FLATPAK) ? P_FLATPAK : P_ACCENT;
        cell_text(y, 1, tag, tag_col, bg, 1, 0);
        int name_x = 8;
        int name_w = g_cols - name_x - 2;
        if (name_w > 0) {
            cell_text(y, name_x, r->name, fg, bg, sel ? 1 : 0, 0);
        }
        if (r->installed && g_cols > 20) {
            cell_text(y, g_cols - 11, "installed", P_OK, bg, 1, 0);
        }
        cell_text(y + 1, name_x, r->description, dim, bg, 0, 0);
    }

    draw_footer(a, " Up/Down move   Enter details   i install   r remove   Esc back");
}

/* Wrap `notes` into rows starting at `row`, in the current grid. */
static int wrap_text(int row, int col, int maxw, const char *text,
                     c256_t fg, c256_t bg) {
    if (maxw < 4) maxw = 4;
    char line[512];
    size_t lb = 0;
    int cl = 0;
    for (const char *p = text; ; p++) {
        if (*p == '\n' || *p == 0 || cl >= maxw) {
            if (lb > 0 || *p == '\n') {
                line[lb] = 0;
                cell_text(row, col, line, fg, bg, 0, 0);
                row++;
                lb = 0;
                cl = 0;
                if (row >= g_rows - 2) return row;
                if (*p == 0) return row;
                if (*p == '\n') continue;
            } else if (*p == 0) {
                return row;
            }
        }
        if (lb + 1 < sizeof(line)) line[lb++] = *p;
        cl++;
    }
}

static void draw_details(const app_t *a) {
    grid_clear(P_FG, P_BG);

    char sub[280];
    snprintf(sub, sizeof(sub), "%s%s",
             (a->details_src == SRC_FLATPAK) ? "flatpak:" : "",
             a->details_id);
    draw_header(sub);

    int row = 2;

    if (!a->details.loaded) {
        cell_text(row, 2, "Loading details...", P_FG_DIM, P_BG, 0, 0);
        draw_footer(a, " please wait");
        return;
    }

    const c256_t LBL = P_ACCENT;
    const c256_t VAL = P_FG;
    const int lbl_w = 13;

    #define ROW(label, value) do {                                          \
        cell_text(row, 2, label, LBL, P_BG, 1, 0);                          \
        for (int k = 2 + lbl_w; k < g_cols - 1; k++)                        \
            cell_put(row, k, " ", VAL, P_BG, 0, 0);                         \
        cell_text(row, 2 + lbl_w, ((value) && (value)[0]) ? (value) : "-",  \
                  VAL, P_BG, 0, 0);                                          \
        row++;                                                              \
    } while (0)

    ROW("Source",      (a->details_src == SRC_FLATPAK) ? "Flathub" : "Nixpkgs");
    ROW("Description", a->details.description);
    ROW("License",     a->details.license);
    ROW("Version",     a->details.version);
    ROW("Developer",   a->details.developer);
    ROW("Size",        a->details.size);
    ROW("Homepage",    a->details.homepage);
    ROW("Bugtracker",  a->details.bugtracker);
    ROW("Safety",      a->details.safety_status);
    ROW("Age",         a->details.age_rating);
    ROW("GUI",         a->details.is_gui ? "Yes" : "No (CLI)");
    #undef ROW

    row++;
    cell_text(row, 2, "Release Notes", LBL, P_BG, 1, 0);
    row++;

    const char *notes = a->details.release_notes[0] ? a->details.release_notes : "(none)";
    wrap_text(row, 2, g_cols - 4, notes, P_FG_DIM, P_BG);

    draw_footer(a, " i install   r remove   Esc back");
}

static void draw_confirm(const app_t *a) {
    if (a->return_to == V_DETAILS) draw_details(a);
    else                            draw_list(a);

    int bw = 60;
    if (bw > g_cols - 6) bw = g_cols - 6;
    if (bw < 24) bw = 24;
    int bh = 6;
    int bx = (g_cols - bw) / 2;
    int by = (g_rows - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    for (int yy = by; yy < by + bh; yy++)
        for (int xx = bx; xx < bx + bw; xx++)
            cell_put(yy, xx, " ", P_FG, P_BG_ALT, 0, 0);

    cell_put(by, bx, g_chrome.tl, P_ACCENT, P_BG_ALT, 0, 0);
    for (int i = 1; i < bw - 1; i++)
        cell_put(by, bx + i, g_chrome.h, P_ACCENT, P_BG_ALT, 0, 0);
    cell_put(by, bx + bw - 1, g_chrome.tr, P_ACCENT, P_BG_ALT, 0, 0);

    cell_put(by + bh - 1, bx, g_chrome.bl, P_ACCENT, P_BG_ALT, 0, 0);
    for (int i = 1; i < bw - 1; i++)
        cell_put(by + bh - 1, bx + i, g_chrome.h, P_ACCENT, P_BG_ALT, 0, 0);
    cell_put(by + bh - 1, bx + bw - 1, g_chrome.br, P_ACCENT, P_BG_ALT, 0, 0);

    for (int yy = by + 1; yy < by + bh - 1; yy++) {
        cell_put(yy, bx, g_chrome.v, P_ACCENT, P_BG_ALT, 0, 0);
        cell_put(yy, bx + bw - 1, g_chrome.v, P_ACCENT, P_BG_ALT, 0, 0);
    }

    cell_text(by + 1, bx + 1, a->confirm_msg, P_FG, P_BG_ALT, 1, 0);
    cell_text(by + 2, bx + 1, a->confirm_id, P_ACCENT, P_BG_ALT, 0, 0);

    cell_put(by + 3, bx, g_chrome.lt, P_ACCENT, P_BG_ALT, 0, 0);
    for (int i = 1; i < bw - 1; i++)
        cell_put(by + 3, bx + i, g_chrome.h, P_ACCENT, P_BG_ALT, 0, 0);
    cell_put(by + 3, bx + bw - 1, g_chrome.rt, P_ACCENT, P_BG_ALT, 0, 0);

    cell_text(by + 4, bx + 1, "[ Enter ] confirm      [ Esc ] cancel",
              P_FG_DIM, P_BG_ALT, 0, 0);
}

static void draw_log(const app_t *a) {
    grid_clear(P_FG, P_BG);
    draw_header(a->confirm_msg[0] ? a->confirm_msg : "operation");

    int list_h = g_rows - 3;
    if (list_h < 1) list_h = 1;

    size_t start = 0;
    if (a->n_log > (size_t)list_h) start = a->n_log - (size_t)list_h;

    for (int i = 0; i < list_h; i++) {
        size_t idx = start + (size_t)i;
        if (idx >= a->n_log) break;
        cell_text(1 + i, 1, a->log_lines[idx], P_FG, P_BG, 0, 0);
    }

    draw_footer(a, " running - press any key when done");
}

static void draw_app(const app_t *a) {
    switch (a->view) {
        case V_MENU:    draw_menu(a);    break;
        case V_SEARCH:  draw_search(a);  break;
        case V_LIST:    draw_list(a);    break;
        case V_DETAILS: draw_details(a); break;
        case V_CONFIRM: draw_confirm(a); break;
        case V_LOG:     draw_log(a);     break;
    }
}

/* ------------------------------------------------------------------ */
/* operations                                                          */
/* ------------------------------------------------------------------ */

static int run_search(app_t *a) {
    free(a->results);
    a->results = NULL;
    a->n_results = 0;
    a->selected = 0;
    a->scroll = 0;

    grid_clear(P_FG, P_BG);
    draw_header("searching");
    cell_text(2, 2, "Querying Nixpkgs. This may take a few seconds.",
              P_FG, P_BG, 0, 0);
    cell_text(3, 2, "Checking install state for each result.",
              P_FG_DIM, P_BG, 0, 0);
    frame_flush();

    int rc = nixpkg_search(a->query, &a->results, &a->n_results);
    if (rc != 0) return rc;

    for (size_t i = 0; i < a->n_results; i++) {
        a->results[i].installed =
            nixpkg_check_installed(a->results[i].app_id, a->results[i].source);
    }
    return 0;
}

static int run_streaming(app_t *a, const char *cmd) {
    log_clear(a);
    a->view = V_LOG;
    draw_app(a);
    frame_flush();

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_append(a, "failed to launch command");
        draw_app(a);
        frame_flush();
        return 1;
    }

    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
        log_append(a, buf);
        draw_app(a);
        frame_flush();
    }
    int status = pclose(fp);
    log_append(a, "");
    log_append(a, (status == 0) ? ">>> done." : ">>> command failed.");
    draw_app(a);
    frame_flush();

    char c;
    while (read_key(&c) == KEY_NONE) { }
    return (status == 0) ? 0 : 1;
}

static void do_system_op(app_t *a, int which) {
    const char *cmd = NULL;
    const char *label = NULL;
    switch (which) {
        case 0: cmd = "PATH=$PATH:/run/current-system/sw/bin nixos-rebuild test 2>&1";             label = "nixos-rebuild test";   break;
        case 1: cmd = "PATH=$PATH:/run/current-system/sw/bin nixos-rebuild switch 2>&1";           label = "nixos-rebuild switch"; break;
        case 2: cmd = "PATH=$PATH:/run/current-system/sw/bin nixos-rebuild boot 2>&1";             label = "nixos-rebuild boot";   break;
        case 3: cmd = "PATH=$PATH:/run/current-system/sw/bin nixos-rebuild switch --rollback 2>&1"; label = "rollback";            break;
        case 4: cmd = "PATH=$PATH:/run/current-system/sw/bin "
                      "nix-env --delete-generations old -p /nix/var/nix/profiles/system 2>&1 && "
                      "PATH=$PATH:/run/current-system/sw/bin "
                      "nix-collect-garbage -d 2>&1";                                                label = "garbage collect";     break;
        default: return;
    }
    snprintf(a->confirm_msg, sizeof(a->confirm_msg), "%s", label);
    run_streaming(a, cmd);
    if (cmd) {
        /* status already set inside run_streaming path */
    }
    set_status(a, "%s finished", label);
    a->view = V_MENU;
}

static void do_confirmed_action(app_t *a) {
    const char *action = (a->confirm_action == 1) ? "remove" : "install";

    char self[1024];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n <= 0) {
        snprintf(a->confirm_msg, sizeof(a->confirm_msg), "cannot locate self");
        a->view = a->return_to;
        return;
    }
    self[n] = 0;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s '%s' 2>&1",
             self, action, a->confirm_id);
    snprintf(a->confirm_msg, sizeof(a->confirm_msg), "nixpkg %s", action);
    int rc = run_streaming(a, cmd);

    if (rc == 0) {
        for (size_t i = 0; i < a->n_results; i++) {
            if (strcmp(a->results[i].app_id, a->confirm_id) == 0) {
                a->results[i].installed = (a->confirm_action == 0) ? 1 : 0;
                a->results[i].details.loaded = 0;
                break;
            }
        }
    }

    if (rc != 0) {
        set_status(a, "%s failed: %.70s", action, last_log_line(a));
    } else {
        set_status(a, "%s ok: %s", action, a->confirm_id);
    }
    a->view = a->return_to;

    /* Refresh details view cache if we're returning there. */
    if (a->view == V_DETAILS) {
        for (size_t i = 0; i < a->n_results; i++) {
            if (strcmp(a->results[i].app_id, a->details_id) == 0) {
                a->details = a->results[i].details;
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

int tui_main(int argc, char **argv) {
    (void)argc; (void)argv;

    setlocale(LC_ALL, "");
    detect_utf8();
    g_chrome = g_utf8 ? CHROME_UNI : CHROME_ASC;

    if (enter_raw() != 0) {
        fprintf(stderr, "nixpkg: failed to enter raw mode: %s\n", strerror(errno));
        return 1;
    }

    int rows, cols;
    term_size(&rows, &cols);
    if (grid_alloc(rows, cols) != 0) {
        fprintf(stderr, "nixpkg: failed to allocate cell grid\n");
        return 1;
    }

    app_t app;
    memset(&app, 0, sizeof(app));
    app.view = V_MENU;
    set_status(&app, "ready");

    for (;;) {
        int nr, nc;
        term_size(&nr, &nc);
        if (nr != g_rows || nc != g_cols) {
            if (grid_alloc(nr, nc) != 0) break;
        }

        draw_app(&app);
        frame_flush();

        char ch = 0;
        key_t k = read_key(&ch);
        if (k == KEY_NONE) continue;

        if (app.view == V_MENU) {
            const int n = 7;
            if (k == KEY_UP   && app.menu_selected > 0) app.menu_selected--;
            if (k == KEY_DOWN && app.menu_selected + 1 < n) app.menu_selected++;
            if (k == KEY_CHAR && (ch == 'q' || ch == 'Q')) break;
            if (k == KEY_ENTER) {
                switch (app.menu_selected) {
                    case 0:
                        app.view = V_SEARCH;
                        app.query[0] = 0;
                        app.qlen = 0;
                        g_need_full_clear = 1;
                        break;
                    case 1: do_system_op(&app, 0); g_need_full_clear = 1; break;
                    case 2: do_system_op(&app, 1); g_need_full_clear = 1; break;
                    case 3: do_system_op(&app, 2); g_need_full_clear = 1; break;
                    case 4: do_system_op(&app, 3); g_need_full_clear = 1; break;
                    case 5: do_system_op(&app, 4); g_need_full_clear = 1; break;
                    case 6: goto out;
                }
            }
        } else if (app.view == V_SEARCH) {
            if (k == KEY_ESC) { app.view = V_MENU; g_need_full_clear = 1; }
            else if (k == KEY_ENTER) {
                if (app.qlen > 0) {
                    if (run_search(&app) == 0) {
                        app.view = V_LIST;
                        app.selected = 0;
                        g_need_full_clear = 1;
                    } else {
                        app.view = V_SEARCH;
                        g_need_full_clear = 1;
                    }
                }
            } else if (k == KEY_BACKSPACE) {
                if (app.qlen > 0) app.query[--app.qlen] = 0;
            } else if (k == KEY_CHAR) {
                if (ch && app.qlen + 1 < sizeof(app.query)) {
                    app.query[app.qlen++] = ch;
                    app.query[app.qlen] = 0;
                }
            }
        } else if (app.view == V_LIST) {
            if (k == KEY_ESC) { app.view = V_SEARCH; g_need_full_clear = 1; }
            else if (k == KEY_UP   && app.selected > 0) app.selected--;
            else if (k == KEY_DOWN && app.selected + 1 < app.n_results) app.selected++;
            else if (k == KEY_ENTER) {
                if (app.selected < app.n_results) {
                    pkg_result_t *r = &app.results[app.selected];
                    snprintf(app.details_id, sizeof(app.details_id), "%s", r->app_id);
                    app.details_src = r->source;
                    app.details_scroll = 0;

                    if (!r->details.loaded) {
                        nixpkg_details(r->app_id, r->source, &r->details);
                    }
                    app.details = r->details;

                    app.return_to = V_LIST;
                    app.view = V_DETAILS;
                    g_need_full_clear = 1;
                }
            } else if (k == KEY_CHAR && (ch == 'i' || ch == 'I')) {
                if (app.selected < app.n_results) {
                    pkg_result_t *r = &app.results[app.selected];
                    snprintf(app.confirm_id, sizeof(app.confirm_id), "%s", r->app_id);
                    app.confirm_action = 0;
                    snprintf(app.confirm_msg, sizeof(app.confirm_msg), "Install this package?");
                    app.return_to = V_LIST;
                    app.view = V_CONFIRM;
                    g_need_full_clear = 1;
                }
            } else if (k == KEY_CHAR && (ch == 'r' || ch == 'R' || ch == 'd' || ch == 'D')) {
                if (app.selected < app.n_results) {
                    pkg_result_t *r = &app.results[app.selected];
                    snprintf(app.confirm_id, sizeof(app.confirm_id), "%s", r->app_id);
                    app.confirm_action = 1;
                    snprintf(app.confirm_msg, sizeof(app.confirm_msg), "Remove this package?");
                    app.return_to = V_LIST;
                    app.view = V_CONFIRM;
                    g_need_full_clear = 1;
                }
            }
        } else if (app.view == V_DETAILS) {
            if (k == KEY_ESC) {
                app.view = app.return_to;
                g_need_full_clear = 1;
            } else if (k == KEY_CHAR && (ch == 'i' || ch == 'I')) {
                snprintf(app.confirm_id, sizeof(app.confirm_id), "%s", app.details_id);
                app.confirm_action = 0;
                snprintf(app.confirm_msg, sizeof(app.confirm_msg), "Install this package?");
                app.return_to = V_DETAILS;
                app.view = V_CONFIRM;
                g_need_full_clear = 1;
            } else if (k == KEY_CHAR && (ch == 'r' || ch == 'R')) {
                snprintf(app.confirm_id, sizeof(app.confirm_id), "%s", app.details_id);
                app.confirm_action = 1;
                snprintf(app.confirm_msg, sizeof(app.confirm_msg), "Remove this package?");
                app.return_to = V_DETAILS;
                app.view = V_CONFIRM;
                g_need_full_clear = 1;
            }
        } else if (app.view == V_CONFIRM) {
            if (k == KEY_ESC) { app.view = app.return_to; g_need_full_clear = 1; }
            else if (k == KEY_ENTER) {
                do_confirmed_action(&app);
                g_need_full_clear = 1;
            }
        }
    }

out:
    free(app.results);
    log_clear(&app);
    grid_free();
    restore_term();
    return 0;
}
