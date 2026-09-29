/* drm_uapi.h — minimal DRM/KMS UAPI definitions, vendored so we don't
 * depend on <drm/drm.h> (libdrm) or on kernel uapi headers being installed.
 *
 * Layouts and ioctl numbers are copied from the Linux kernel UAPI headers
 * (include/uapi/drm/drm.h and include/uapi/drm/drm_mode.h), which are
 * licensed GPL-2.0 WITH Linux-syscall-note. That license explicitly permits
 * use of these definitions by any program. */
#ifndef NIXPKG_DRM_UAPI_H
#define NIXPKG_DRM_UAPI_H

#include <stdint.h>
#include <sys/ioctl.h>

/* ----- ioctl direction helpers (present in <sys/ioctl.h> via <asm/ioctl.h>) ----- */
/* _IOC, _IOC_READ, _IOC_WRITE, _IO, _IOW, _IOR, _IOWR, _IOC_NRBITS, ... come from
   <asm-generic/ioctl.h>, which <sys/ioctl.h> includes. */

/* ----- base ioctl numbers ----- */
#define DRM_IOCTL_BASE          'd'

/* These two are the version + capability ioctls; not used by us but part of the ABI. */
#define DRM_IO(nr)              _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOWR(nr, type)      _IOWR(DRM_IOCTL_BASE, nr, type)

/* ----- ioctl numbers we actually use ----- */
#define DRM_IOCTL_SET_MASTER        DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER       DRM_IO(0x1f)

#define DRM_IOCTL_MODE_GETRESOURCES DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC      DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC      DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_CURSOR       DRM_IOWR(0xA3, struct drm_mode_cursor)
#define DRM_IOCTL_MODE_GETGAMMA     DRM_IOWR(0xA4, struct drm_mode_crtc_lut)
#define DRM_IOCTL_MODE_SETGAMMA     DRM_IOWR(0xA5, struct drm_mode_crtc_lut)
#define DRM_IOCTL_MODE_GETENCODER   DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_GETPROPERTY  DRM_IOWR(0xAA, struct drm_mode_get_property)
#define DRM_IOCTL_MODE_SETPROPERTY  DRM_IOWR(0xAB, struct drm_mode_connector_set_property)
#define DRM_IOCTL_MODE_GETPROPBLOB  DRM_IOWR(0xAC, struct drm_mode_get_blob)
#define DRM_IOCTL_MODE_ADDFB        DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_RMFB         DRM_IOWR(0xAF, unsigned int)
#define DRM_IOCTL_MODE_DIRTYFB      DRM_IOWR(0xB1, struct drm_mode_fb_dirty_cmd)
#define DRM_IOCTL_MODE_CREATE_DUMB  DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB     DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#define DRM_IOCTL_MODE_DESTROY_DUMB DRM_IOWR(0xB4, struct drm_mode_destroy_dumb)
#define DRM_IOCTL_MODE_PAGE_FLIP    DRM_IOWR(0xB0, struct drm_mode_crtc_page_flip)

/* ----- constants ----- */
#define DRM_MODE_CONNECTED          1
#define DRM_MODE_DISCONNECTED       2
#define DRM_MODE_UNKNOWNCONNECTION  3

#define DRM_MODE_TYPE_PREFERRED     (1 << 3)
#define DRM_MODE_TYPE_DRIVER        (1 << 6)

#define DRM_DISPLAY_MODE_LEN        32
#define DRM_PROP_NAME_LEN           32

#define DRM_MODE_OBJECT_CRTC        0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR   0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER     0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE        0xdededede
#define DRM_MODE_OBJECT_PROPERTY    0xb0b0b0b0
#define DRM_MODE_OBJECT_FB          0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB        0xbbbbbbbb
#define DRM_MODE_OBJECT_PLANE       0xeeeeeeee
#define DRM_MODE_OBJECT_ANY         0

#define DRM_MODE_FLAG_NHSYNC        0x00000001
#define DRM_MODE_FLAG_PHSYNC        0x00000002
#define DRM_MODE_FLAG_NVSYNC        0x00000004
#define DRM_MODE_FLAG_PVSYNC        0x00000008
#define DRM_MODE_FLAG_INTERLACE     0x00000010
#define DRM_MODE_FLAG_DBLSCAN       0x00000020
#define DRM_MODE_FLAG_CSYNC         0x00000040
#define DRM_MODE_FLAG_PCSYNC        0x00000080
#define DRM_MODE_FLAG_NCSYNC        0x00000100
#define DRM_MODE_FLAG_HSKEW         0x00000200
#define DRM_MODE_FLAG_BCAST         0x00000400
#define DRM_MODE_FLAG_PIXMUX        0x00000800
#define DRM_MODE_FLAG_DBLCLK        0x00001000
#define DRM_MODE_FLAG_CLKDIV2       0x00002000

/* ----- structs, field order matching the kernel ABI exactly ----- */

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char     name[DRM_DISPLAY_MODE_LEN];
};

struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;

    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;

    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;

    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

struct drm_mode_get_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;

    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;

    uint32_t gamma_size;

    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;

    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;

    uint32_t gamma_size;

    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct drm_mode_fb_dirty_cmd {
    uint32_t fb_id;
    uint32_t flags;
    uint32_t color;
    uint32_t num_clips;
    uint64_t clips_ptr;
    uint32_t num_rects;
    uint32_t pad;
    uint64_t rects_ptr;
};

struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

/* Extra structs referenced by ioctl number macros above but not used by us.
   Declared so the DRM_IOCTL_MODE_* macros compile cleanly even if we never
   invoke those particular ioctls. */
struct drm_mode_cursor {
    uint32_t flags;
    uint32_t crtc_id;
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint64_t handle;
};

struct drm_mode_crtc_lut {
    uint32_t crtc_id;
    uint32_t gamma_size;
    uint64_t red;
    uint64_t green;
    uint64_t blue;
};

struct drm_mode_get_property {
    uint64_t values_ptr;
    uint64_t enum_blob_ptr;
    uint32_t prop_id;
    uint32_t flags;
    char     name[DRM_PROP_NAME_LEN];
    uint32_t count_values;
    uint32_t count_enum_blobs;
};

struct drm_mode_connector_set_property {
    uint64_t value;
    uint32_t prop_id;
    uint32_t connector_id;
};

struct drm_mode_get_blob {
    uint32_t blob_id;
    uint32_t length;
    uint64_t data;
};

#endif /* NIXPKG_DRM_UAPI_H */
