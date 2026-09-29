/* gfx.h — DRM/KMS graphics layer for nixpkg, glibc-only.
 *
 * Presents a surface_t to a Linux DRM/KMS device using the dumb-buffer API.
 * No libdrm, no mesa, no GBM. Just /dev/dri/card0 and ioctl(). */
#ifndef NIXPKG_GFX_H
#define NIXPKG_GFX_H

#include <stdint.h>
#include <stddef.h>

/* ARGB8888 surface we draw into. Same layout as the old fb.h surface_t. */
typedef struct {
    int width;
    int height;
    uint32_t *pixels;   /* ARGB, stride = width */
} surface_t;

typedef struct {
    int fd;                     /* /dev/dri/card0 fd */
    int width;
    int height;

    /* Dumb buffer we render into (XRGB8888) */
    uint32_t handle;            /* GEM handle for the dumb buffer */
    uint32_t fb_id;             /* DRM framebuffer id */
    uint32_t pitch;             /* bytes per line, from drm_mode_create_dumb */
    uint64_t size;              /* size of the mapping */
    uint8_t *map;               /* mmap'd dumb buffer */
    int saved_crtc;             /* original CRTC id, for restore */

    surface_t back;             /* offscreen ARGB surface we draw into */
} gfx_t;

int  gfx_open(gfx_t *g);
void gfx_close(gfx_t *g);
void gfx_present(gfx_t *g);     /* copy back.pixels → dumb buffer → DRM dirty FB */

#endif
