/* drm.c — DRM/KMS dumb-buffer presenter for nixpkg.
 *
 * Uses our vendored drm_uapi.h (see header for rationale). No libdrm,
 * no linux/drm.h. */
#include "gfx.h"
#include "drm_uapi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* ------------------------------------------------------------------ */
/* open the first DRM card that exists                                 */
/* ------------------------------------------------------------------ */

static int drm_open_card(void) {
    const char *paths[] = {
        "/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2", NULL
    };
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) return fd;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* GETRESOURCES                                                        */
/* ------------------------------------------------------------------ */

static int get_resources(int fd,
                         uint32_t *crtcs, uint32_t *n_crtcs,
                         uint32_t *connectors, uint32_t *n_connectors,
                         uint32_t *encoders, uint32_t *n_encoders,
                         uint32_t *fbs, uint32_t *n_fbs,
                         uint32_t *min_w, uint32_t *max_w,
                         uint32_t *min_h, uint32_t *max_h) {
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));

    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return -1;

    uint32_t local_crtcs[32];
    uint32_t local_conns[32];
    uint32_t local_encs[32];
    uint32_t local_fbs[32];
    if (res.count_crtcs      > 32) res.count_crtcs      = 32;
    if (res.count_connectors > 32) res.count_connectors = 32;
    if (res.count_encoders   > 32) res.count_encoders   = 32;
    if (res.count_fbs        > 32) res.count_fbs        = 32;

    res.crtc_id_ptr      = (uint64_t)(uintptr_t)local_crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)local_conns;
    res.encoder_id_ptr   = (uint64_t)(uintptr_t)local_encs;
    res.fb_id_ptr        = (uint64_t)(uintptr_t)local_fbs;

    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return -1;

    *n_crtcs      = res.count_crtcs;
    *n_connectors = res.count_connectors;
    *n_encoders   = res.count_encoders;
    *n_fbs        = res.count_fbs;
    memcpy(crtcs,      local_crtcs, res.count_crtcs      * sizeof(uint32_t));
    memcpy(connectors, local_conns, res.count_connectors * sizeof(uint32_t));
    memcpy(encoders,   local_encs,  res.count_encoders   * sizeof(uint32_t));
    memcpy(fbs,        local_fbs,   res.count_fbs        * sizeof(uint32_t));
    *min_w = res.min_width;  *max_w = res.max_width;
    *min_h = res.min_height; *max_h = res.max_height;
    return 0;
}

/* ------------------------------------------------------------------ */
/* GETCONNECTOR                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    uint32_t encoder_id;
    uint32_t connection;
    uint32_t mm_width, mm_height;
    uint32_t count_modes;
    uint32_t count_encoders;
    struct drm_mode_modeinfo first_mode;
    uint32_t first_encoder;
} conn_info_t;

static int get_connector(int fd, uint32_t id, conn_info_t *out) {
    struct drm_mode_get_connector c;
    memset(&c, 0, sizeof(c));
    c.connector_id = id;

    if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) != 0) return -1;

    uint32_t local_encs[16];
    struct drm_mode_modeinfo local_modes[32];
    uint32_t local_props[32];
    uint64_t local_prop_vals[32];
    if (c.count_encoders > 16) c.count_encoders = 16;
    if (c.count_modes    > 32) c.count_modes    = 32;

    c.encoders_ptr    = (uint64_t)(uintptr_t)local_encs;
    c.modes_ptr       = (uint64_t)(uintptr_t)local_modes;
    c.props_ptr       = (uint64_t)(uintptr_t)local_props;
    c.prop_values_ptr = (uint64_t)(uintptr_t)local_prop_vals;

    if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) != 0) return -1;

    out->id = c.connector_id;
    out->encoder_id = c.encoder_id;
    out->connection = c.connection;
    out->mm_width = c.mm_width;
    out->mm_height = c.mm_height;
    out->count_modes = c.count_modes;
    out->count_encoders = c.count_encoders;
    if (c.count_modes > 0) out->first_mode = local_modes[0];
    else memset(&out->first_mode, 0, sizeof(out->first_mode));
    out->first_encoder = c.count_encoders > 0 ? local_encs[0] : 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* GETENCODER                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
} enc_info_t;

static int get_encoder(int fd, uint32_t id, enc_info_t *out) {
    struct drm_mode_get_encoder e;
    memset(&e, 0, sizeof(e));
    e.encoder_id = id;
    if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) != 0) return -1;
    out->id = e.encoder_id;
    out->crtc_id = e.crtc_id;
    out->possible_crtcs = e.possible_crtcs;
    return 0;
}

/* ------------------------------------------------------------------ */
/* CRTC selection                                                      */
/* ------------------------------------------------------------------ */

static uint32_t pick_crtc(int fd,
                          const uint32_t *crtcs, uint32_t n_crtcs,
                          const uint32_t *conns, uint32_t n_conns,
                          uint32_t *conn_out) {
    for (uint32_t ci = 0; ci < n_conns; ci++) {
        conn_info_t info;
        if (get_connector(fd, conns[ci], &info) != 0) continue;
        if (info.connection != DRM_MODE_CONNECTED) continue;
        if (info.count_modes == 0) continue;

        /* Walk this connector's encoders. */
        struct drm_mode_get_connector cc;
        memset(&cc, 0, sizeof(cc));
        cc.connector_id = conns[ci];
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &cc) != 0) continue;

        uint32_t enc_ids[16];
        if (cc.count_encoders > 16) cc.count_encoders = 16;
        cc.encoders_ptr = (uint64_t)(uintptr_t)enc_ids;
        /* We don't want modes here. */
        cc.modes_ptr = 0;
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &cc) != 0) continue;

        for (uint32_t ei = 0; ei < cc.count_encoders; ei++) {
            enc_info_t e;
            if (get_encoder(fd, enc_ids[ei], &e) != 0) continue;
            for (uint32_t i = 0; i < n_crtcs; i++) {
                if (e.possible_crtcs & (1u << i)) {
                    *conn_out = conns[ci];
                    return crtcs[i];
                }
            }
        }
        /* If nothing matched, fall back to first CRTC for this connector. */
        if (n_crtcs > 0) {
            *conn_out = conns[ci];
            return crtcs[0];
        }
    }
    if (n_crtcs > 0 && n_conns > 0) {
        *conn_out = conns[0];
        return crtcs[0];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* dumb-buffer ioctls                                                  */
/* ------------------------------------------------------------------ */

static int ioctl_dumb_create(int fd, uint32_t w, uint32_t h,
                             uint32_t *handle_out, uint32_t *pitch_out,
                             uint64_t *size_out) {
    struct drm_mode_create_dumb arg;
    memset(&arg, 0, sizeof(arg));
    arg.width  = w;
    arg.height = h;
    arg.bpp    = 32;
    arg.flags  = 0;
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg) != 0) {
        fprintf(stderr, "nixpkg: DRM_IOCTL_MODE_CREATE_DUMB: %s\n", strerror(errno));
        return -1;
    }
    *handle_out = arg.handle;
    *pitch_out  = arg.pitch;
    *size_out   = arg.size;
    return 0;
}

static int ioctl_dumb_map(int fd, uint32_t handle, uint64_t *offset_out) {
    struct drm_mode_map_dumb arg;
    memset(&arg, 0, sizeof(arg));
    arg.handle = handle;
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &arg) != 0) {
        fprintf(stderr, "nixpkg: DRM_IOCTL_MODE_MAP_DUMB: %s\n", strerror(errno));
        return -1;
    }
    *offset_out = arg.offset;
    return 0;
}

static int ioctl_dumb_destroy(int fd, uint32_t handle) {
    struct drm_mode_destroy_dumb arg;
    memset(&arg, 0, sizeof(arg));
    arg.handle = handle;
    return ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
}

static int ioctl_add_fb(int fd, uint32_t w, uint32_t h, uint32_t pitch,
                        uint32_t handle, uint32_t *fb_id_out) {
    struct drm_mode_fb_cmd arg;
    memset(&arg, 0, sizeof(arg));
    arg.width  = w;
    arg.height = h;
    arg.pitch  = pitch;
    arg.bpp    = 32;
    arg.depth  = 24;
    arg.handle = handle;
    if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &arg) != 0) {
        fprintf(stderr, "nixpkg: DRM_IOCTL_MODE_ADDFB: %s\n", strerror(errno));
        return -1;
    }
    *fb_id_out = arg.fb_id;
    return 0;
}

static int ioctl_rm_fb(int fd, uint32_t fb_id) {
    return ioctl(fd, DRM_IOCTL_MODE_RMFB, &fb_id);
}

static int ioctl_set_crtc(int fd,
                          uint32_t crtc_id, uint32_t fb_id,
                          uint32_t x, uint32_t y,
                          const uint32_t *connectors, uint32_t n_conn,
                          const struct drm_mode_modeinfo *mode) {
    size_t sz = sizeof(struct drm_mode_crtc) + n_conn * sizeof(uint32_t);
    struct drm_mode_crtc *crtc = malloc(sz);
    if (!crtc) return -1;
    memset(crtc, 0, sz);
    crtc->set_connectors_ptr = (uint64_t)(uintptr_t)connectors;
    crtc->count_connectors   = n_conn;
    crtc->crtc_id            = crtc_id;
    crtc->fb_id              = fb_id;
    crtc->x = x;
    crtc->y = y;
    crtc->gamma_size = 0;
    if (mode) { crtc->mode_valid = 1; crtc->mode = *mode; }
    int rc = ioctl(fd, DRM_IOCTL_MODE_SETCRTC, crtc);
    free(crtc);
    return rc;
}

static int ioctl_dirty_fb(int fd, uint32_t fb_id,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct drm_mode_fb_dirty_cmd arg;
    memset(&arg, 0, sizeof(arg));
    uint32_t clip[4] = { x, y, w, h };
    arg.fb_id     = fb_id;
    arg.flags     = 0;
    arg.color     = 0;
    arg.num_clips = 1;
    arg.clips_ptr = (uint64_t)(uintptr_t)clip;
    arg.num_rects = 0;
    arg.pad       = 0;
    arg.rects_ptr = 0;
    return ioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &arg);
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int gfx_open(gfx_t *g) {
    memset(g, 0, sizeof(*g));
    g->fd = -1;

    g->fd = drm_open_card();
    if (g->fd < 0) {
        fprintf(stderr, "nixpkg: cannot open any /dev/dri/card*: %s\n", strerror(errno));
        return -1;
    }

    if (ioctl(g->fd, DRM_IOCTL_SET_MASTER, 0) != 0) {
        fprintf(stderr,
                "nixpkg: DRM_IOCTL_SET_MASTER failed: %s\n"
                "        Something else is DRM master on this card — kmscon,\n"
                "        a compositor, or a display manager. Stop it and retry.\n",
                strerror(errno));
        close(g->fd); g->fd = -1; return -1;
    }

    uint32_t crtcs[32], conns[32], encs[32], fbs[32];
    uint32_t n_crtcs = 0, n_conns = 0, n_encs = 0, n_fbs = 0;
    uint32_t min_w = 0, max_w = 0, min_h = 0, max_h = 0;
    if (get_resources(g->fd, crtcs, &n_crtcs, conns, &n_conns,
                      encs, &n_encs, fbs, &n_fbs,
                      &min_w, &max_w, &min_h, &max_h) != 0) {
        fprintf(stderr, "nixpkg: DRM_IOCTL_MODE_GETRESOURCES: %s\n", strerror(errno));
        ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(g->fd); g->fd = -1; return -1;
    }

    uint32_t conn_id = 0;
    uint32_t crtc_id = pick_crtc(g->fd, crtcs, n_crtcs, conns, n_conns, &conn_id);
    if (!crtc_id || !conn_id) {
        fprintf(stderr, "nixpkg: no usable CRTC/connector\n");
        ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(g->fd); g->fd = -1; return -1;
    }

    conn_info_t ci;
    if (get_connector(g->fd, conn_id, &ci) != 0 || ci.count_modes == 0) {
        fprintf(stderr, "nixpkg: connector %u has no modes\n", conn_id);
        ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(g->fd); g->fd = -1; return -1;
    }

    /* Pick the preferred mode if any is flagged. */
    struct drm_mode_modeinfo mode = ci.first_mode;
    {
        struct drm_mode_get_connector cc;
        memset(&cc, 0, sizeof(cc));
        cc.connector_id = conn_id;
        if (ioctl(g->fd, DRM_IOCTL_MODE_GETCONNECTOR, &cc) == 0) {
            uint32_t local_enc[16];
            struct drm_mode_modeinfo local_modes[32];
            uint32_t np = cc.count_modes > 32 ? 32 : cc.count_modes;
            if (cc.count_encoders > 16) cc.count_encoders = 16;
            cc.encoders_ptr = (uint64_t)(uintptr_t)local_enc;
            cc.modes_ptr    = (uint64_t)(uintptr_t)local_modes;
            cc.props_ptr    = 0;
            cc.prop_values_ptr = 0;
            if (ioctl(g->fd, DRM_IOCTL_MODE_GETCONNECTOR, &cc) == 0 && np > 0) {
                int chosen = 0;
                for (uint32_t i = 0; i < np; i++) {
                    if (local_modes[i].type & DRM_MODE_TYPE_PREFERRED) {
                        chosen = (int)i; break;
                    }
                }
                mode = local_modes[chosen];
            }
        }
    }

    if (mode.hdisplay == 0 || mode.vdisplay == 0) {
        fprintf(stderr, "nixpkg: connector has no usable mode\n");
        ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(g->fd); g->fd = -1; return -1;
    }

    g->width  = (int)mode.hdisplay;
    g->height = (int)mode.vdisplay;

    if (g->width < 320 || g->height < 200) {
        fprintf(stderr, "nixpkg: mode too small (%dx%d)\n", g->width, g->height);
        ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(g->fd); g->fd = -1; return -1;
    }

    g->saved_crtc = (int)crtc_id;

    if (ioctl_dumb_create(g->fd, (uint32_t)g->width, (uint32_t)g->height,
                          &g->handle, &g->pitch, &g->size) != 0)
        goto fail_master;

    uint64_t offset = 0;
    if (ioctl_dumb_map(g->fd, g->handle, &offset) != 0)
        goto fail_dumb;

    g->map = mmap(NULL, g->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                  g->fd, (off_t)offset);
    if (g->map == MAP_FAILED) {
        fprintf(stderr, "nixpkg: mmap dumb buffer: %s\n", strerror(errno));
        g->map = NULL;
        goto fail_dumb;
    }

    if (ioctl_add_fb(g->fd, (uint32_t)g->width, (uint32_t)g->height,
                     g->pitch, g->handle, &g->fb_id) != 0)
        goto fail_mmap;

    uint32_t conns_arr[1] = { conn_id };
    if (ioctl_set_crtc(g->fd, crtc_id, g->fb_id, 0, 0,
                       conns_arr, 1, &mode) != 0) {
        fprintf(stderr, "nixpkg: DRM_IOCTL_MODE_SETCRTC: %s\n", strerror(errno));
        goto fail_fb;
    }

    g->back.width  = g->width;
    g->back.height = g->height;
    g->back.pixels = calloc((size_t)g->width * g->height, sizeof(uint32_t));
    if (!g->back.pixels) goto fail_crtc;

    return 0;

fail_crtc:
    ioctl_set_crtc(g->fd, crtc_id, 0, 0, 0, NULL, 0, NULL);
fail_fb:
    ioctl_rm_fb(g->fd, g->fb_id);
fail_mmap:
    if (g->map) munmap(g->map, g->size);
fail_dumb:
    if (g->handle) ioctl_dumb_destroy(g->fd, g->handle);
fail_master:
    ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
    close(g->fd); g->fd = -1;
    return -1;
}

void gfx_close(gfx_t *g) {
    if (g->fd >= 0) {
        if (g->saved_crtc && g->fb_id) {
            struct drm_mode_crtc c;
            memset(&c, 0, sizeof(c));
            c.crtc_id = (uint32_t)g->saved_crtc;
            c.fb_id = 0;
            c.mode_valid = 0;
            c.count_connectors = 0;
            c.set_connectors_ptr = 0;
            ioctl(g->fd, DRM_IOCTL_MODE_SETCRTC, &c);
        }
        if (g->fb_id)  ioctl_rm_fb(g->fd, g->fb_id);
        if (g->map)    munmap(g->map, g->size);
        if (g->handle) ioctl_dumb_destroy(g->fd, g->handle);
        ioctl(g->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(g->fd);
    }
    if (g->back.pixels) free(g->back.pixels);
    memset(g, 0, sizeof(*g));
    g->fd = -1;
}

void gfx_present(gfx_t *g) {
    const int w = g->width;
    const int h = g->height;
    for (int y = 0; y < h; y++) {
        uint32_t *dst = (uint32_t*)(g->map + (size_t)y * g->pitch);
        const uint32_t *src = g->back.pixels + (size_t)y * w;
        for (int x = 0; x < w; x++)
            dst[x] = 0xFF000000u | (src[x] & 0x00FFFFFFu);
    }
    ioctl_dirty_fb(g->fd, g->fb_id, 0, 0, (uint32_t)w, (uint32_t)h);
}
