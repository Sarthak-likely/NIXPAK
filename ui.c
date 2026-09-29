#include "ui.h"
#include "font8x16.h"
#include <string.h>

void surf_clear(surface_t *s, uint32_t argb) {
    size_t n = (size_t)s->width * s->height;
    for (size_t i = 0; i < n; i++) s->pixels[i] = argb;
}

void surf_fill_rect(surface_t *s, int x, int y, int w, int h, uint32_t argb) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->width)  w = s->width - x;
    if (y + h > s->height) h = s->height - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        uint32_t *row = s->pixels + (size_t)(y + yy) * s->width + x;
        for (int xx = 0; xx < w; xx++) row[xx] = argb;
    }
}

void surf_stroke_rect(surface_t *s, int x, int y, int w, int h, uint32_t argb, int t) {
    surf_fill_rect(s, x, y, w, t, argb);
    surf_fill_rect(s, x, y + h - t, w, t, argb);
    surf_fill_rect(s, x, y, t, h, argb);
    surf_fill_rect(s, x + w - t, y, t, h, argb);
}

int surf_text_width(const char *text) {
    return (int)strlen(text) * FONT_W;
}

/* Draw one glyph at (x, y) with foreground fg, background bg. */
static void draw_glyph(surface_t *s, int x, int y, unsigned char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = font8x16[c - 32];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = g[row];
        int yy = y + row;
        if (yy < 0 || yy >= s->height) continue;
        uint32_t *line = s->pixels + (size_t)yy * s->width;
        for (int col = 0; col < FONT_W; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= s->width) continue;
            line[xx] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

void surf_draw_text(surface_t *s, int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    int cx = x;
    for (const char *p = text; *p; p++) {
        draw_glyph(s, cx, y, (unsigned char)*p, fg, bg);
        cx += FONT_W;
    }
}

void surf_draw_text_n(surface_t *s, int x, int y, const char *text, int max_chars, uint32_t fg, uint32_t bg) {
    int cx = x;
    int n = 0;
    for (const char *p = text; *p && n < max_chars; p++, n++) {
        draw_glyph(s, cx, y, (unsigned char)*p, fg, bg);
        cx += FONT_W;
    }
}
