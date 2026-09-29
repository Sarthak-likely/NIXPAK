/* ui.h — surface drawing primitives, ARGB8888.
 * The surface_t type and color constants live here. */
#ifndef NIXPKG_UI_H
#define NIXPKG_UI_H

#include "gfx.h"
#include <stdint.h>

/* Colors — ARGB8888 */
#define C_BG        0xFF1A1A2E
#define C_BG_ALT    0xFF16213E
#define C_FG        0xFFEEEEEE
#define C_FG_DIM    0xFF8A8A9E
#define C_ACCENT    0xFF5FB8F2
#define C_FLATPAK   0xFF6B9BD1
#define C_SEL_BG    0xFF3584E4
#define C_SEL_FG    0xFFFFFFFF
#define C_OK        0xFF2EC27E
#define C_WARN      0xFFE66100
#define C_ERR       0xFFE01B24
#define C_BORDER    0xFF2A2A44
#define C_HEADER    0xFF0F3460

void surf_clear(surface_t *s, uint32_t argb);
void surf_fill_rect(surface_t *s, int x, int y, int w, int h, uint32_t argb);
void surf_stroke_rect(surface_t *s, int x, int y, int w, int h, uint32_t argb, int thickness);
void surf_draw_text(surface_t *s, int x, int y, const char *text, uint32_t fg, uint32_t bg);
void surf_draw_text_n(surface_t *s, int x, int y, const char *text, int max_chars, uint32_t fg, uint32_t bg);
int  surf_text_width(const char *text);

#endif
