/* common.h — shared types and prototypes for nixpkg */
#ifndef NIXPKG_COMMON_H
#define NIXPKG_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <stddef.h>

#define NIXPKG_CFG      "/etc/nixos/nixpkg-apps.nix"
#define NIXPKG_MAX_APP  256
#define NIXPKG_MAX_DESC 512

/* Package source */
typedef enum {
    SRC_NIX = 0,
    SRC_FLATPAK = 1
} pkg_source_t;

/* Rich metadata for a package. Populated by nixpkg_details(). */
typedef struct {
    char description[1024];
    char license[128];
    char homepage[256];
    char version[128];
    char developer[128];
    char size[64];
    char safety_status[64];
    char safety_desc[128];
    char age_rating[32];
    char release_notes[2048];
    char bugtracker[256];
    char help[256];
    char translate[256];
    int  is_gui;
    int  loaded;
} pkg_details_t;

/* One search result */
typedef struct {
    pkg_source_t source;
    char name[NIXPKG_MAX_APP];
    char app_id[NIXPKG_MAX_APP];
    char description[NIXPKG_MAX_DESC];
    int installed;

    pkg_source_t alt_source;
    char alt_app_id[NIXPKG_MAX_APP];
    int  has_alt;

    pkg_details_t details;
} pkg_result_t;

/* core.c API */
int  nixpkg_run(const char *cmd);
int  nixpkg_is_flatpak(const char *name);
int  nixpkg_mutate(const char *action, const char *app_name);
int  nixpkg_gc(void);
int  nixpkg_rebuild(const char *mode);
int  nixpkg_rollback(void);
int  nixpkg_check_installed(const char *app_id, pkg_source_t src);
int  nixpkg_search(const char *query, pkg_result_t **out, size_t *n_out);
int  nixpkg_details(const char *app_id, pkg_source_t src, pkg_details_t *out);

/* tui.c API */
int  tui_main(int argc, char **argv);

/* json.c API */
typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } json_type_t;
typedef struct json_value json_value_t;
struct json_value {
    json_type_t type;
    union {
        int         b;
        double      num;
        char       *str;
        struct { json_value_t **items; size_t n; } arr;
        struct { char **keys; json_value_t **vals; size_t n; } obj;
    } u;
};
json_value_t *json_parse(const char *text, size_t len);
void          json_free(json_value_t *v);
json_value_t *json_obj_get(json_value_t *obj, const char *key);
json_value_t *json_arr_get(json_value_t *arr, size_t idx);

#endif
