/* core.c — glibc-only core: process spawning, config mutation, nix search, details. */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define NIXPKG_MAX_LINE 4096

/* ------------------------------------------------------------------ */
/* process helpers                                                     */
/* ------------------------------------------------------------------ */

int nixpkg_run(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return 1;
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        fputs(buf, stdout);
        fflush(stdout);
    }
    int status = pclose(fp);
    if (status == -1) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

int nixpkg_is_flatpak(const char *name) {
    return name && strncmp(name, "flatpak:", 8) == 0;
}

/* ------------------------------------------------------------------ */
/* validation                                                          */
/* ------------------------------------------------------------------ */

static int valid_app_name(const char *s) {
    if (!s || !*s) return 0;
    for (const unsigned char *p = (const unsigned char*)s; *p; p++) {
        if (!(isalnum(*p) || *p=='.' || *p=='_' || *p=='+' || *p=='-')) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* installed check                                                     */
/* ------------------------------------------------------------------ */

int nixpkg_check_installed(const char *app_id, pkg_source_t src) {
    char cmd[1024];
    if (src == SRC_FLATPAK) {
        if (!valid_app_name(app_id)) return 0;
        snprintf(cmd, sizeof(cmd),
                 "PATH=$PATH:/run/current-system/sw/bin "
                 "flatpak list --app --columns=application 2>/dev/null "
                 "| grep -qxF '%s'", app_id);
    } else {
        if (!valid_app_name(app_id)) return 0;
        snprintf(cmd, sizeof(cmd),
                 "grep -qE '^[[:space:]]*%s[[:space:]]*$' " NIXPKG_CFG, app_id);
    }
    int rc = system(cmd);
    return rc == 0;
}

/* ------------------------------------------------------------------ */
/* config mutation (Nix branch)                                        */
/* ------------------------------------------------------------------ */

static int mutate_nix(const char *action, const char *app_name) {
    FILE *fp = fopen(NIXPKG_CFG, "r");
    if (!fp) {
        fprintf(stderr, "nixpkg: cannot open %s: %s\n", NIXPKG_CFG, strerror(errno));
        return 1;
    }

    size_t cap = 64, n = 0;
    char **lines = malloc(cap * sizeof(*lines));
    if (!lines) { fclose(fp); return 1; }

    long start_idx = -1;
    int found = 0;
    char buf[NIXPKG_MAX_LINE];

    while (fgets(buf, sizeof(buf), fp)) {
        if (strstr(buf, "# NIXPKG_START")) start_idx = (long)n;

        char tok[NIXPKG_MAX_APP] = {0};
        if (sscanf(buf, " %255s ", tok) == 1 && strcmp(tok, app_name) == 0) {
            found = 1;
            if (strcmp(action, "remove") == 0) continue;
        }

        if (n == cap) {
            cap *= 2;
            char **nl = realloc(lines, cap * sizeof(*nl));
            if (!nl) goto oom;
            lines = nl;
        }
        lines[n] = strdup(buf);
        if (!lines[n]) goto oom;
        n++;
    }
    fclose(fp);
    fp = NULL;

    if (strcmp(action, "install") == 0 && found) goto done_ok;
    if (strcmp(action, "remove")  == 0 && !found) goto fail;
    if (strcmp(action, "install") == 0 && start_idx < 0) {
        fprintf(stderr, "nixpkg: %s has no '# NIXPKG_START' marker\n", NIXPKG_CFG);
        goto fail;
    }

    char tmp[] = NIXPKG_CFG ".tmpXXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) goto fail;
    FILE *out = fdopen(fd, "w");
    if (!out) { close(fd); unlink(tmp); goto fail; }

    for (size_t i = 0; i < n; i++) {
        fputs(lines[i], out);
        if (strcmp(action, "install") == 0 && (long)i == start_idx) {
            fprintf(out, "    %s\n", app_name);
        }
    }
    if (fflush(out) != 0 || fsync(fileno(out)) != 0) {
        fclose(out); unlink(tmp); goto fail;
    }
    fclose(out);

    if (rename(tmp, NIXPKG_CFG) != 0) {
        unlink(tmp); goto fail;
    }

done_ok:
    for (size_t i = 0; i < n; i++) free(lines[i]);
    free(lines);
    printf("nixpkg: updated %s (%s %s)\n", NIXPKG_CFG, action, app_name);
    return 0;

oom:
    fprintf(stderr, "nixpkg: out of memory\n");
fail:
    if (fp) fclose(fp);
    for (size_t i = 0; i < n; i++) free(lines[i]);
    free(lines);
    return 1;
}

/* ------------------------------------------------------------------ */
/* mutate (dispatch)                                                   */
/* ------------------------------------------------------------------ */

int nixpkg_mutate(const char *action, const char *app_name) {
    if (!action || !app_name) return 1;

    if (nixpkg_is_flatpak(app_name)) {
        const char *id = app_name + 8;
        if (!valid_app_name(id)) {
            fprintf(stderr, "nixpkg: invalid flatpak id\n");
            return 1;
        }
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "PATH=$PATH:/run/current-system/sw/bin "
                 "flatpak %s -y flathub %s 2>&1",
                 strcmp(action, "install") == 0 ? "install" : "uninstall",
                 id);
        return nixpkg_run(cmd);
    }

    if (!valid_app_name(app_name)) {
        fprintf(stderr, "nixpkg: invalid nix attribute name\n");
        return 1;
    }
    return mutate_nix(action, app_name);
}

/* ------------------------------------------------------------------ */
/* system ops                                                          */
/* ------------------------------------------------------------------ */

int nixpkg_gc(void) {
    printf("nixpkg: deleting old generations...\n");
    nixpkg_run("PATH=$PATH:/run/current-system/sw/bin "
               "nix-env --delete-generations old -p /nix/var/nix/profiles/system 2>&1");
    printf("nixpkg: collecting garbage...\n");
    return nixpkg_run("PATH=$PATH:/run/current-system/sw/bin "
                      "nix-collect-garbage -d 2>&1");
}

int nixpkg_rebuild(const char *mode) {
    if (strcmp(mode,"test") && strcmp(mode,"switch") && strcmp(mode,"boot")) return 1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "PATH=$PATH:/run/current-system/sw/bin nixos-rebuild %s 2>&1", mode);
    return nixpkg_run(cmd);
}

int nixpkg_rollback(void) {
    return nixpkg_run("PATH=$PATH:/run/current-system/sw/bin "
                      "nixos-rebuild switch --rollback 2>&1");
}

/* ------------------------------------------------------------------ */
/* capture stdout of a command into a malloc'd buffer                  */
/* ------------------------------------------------------------------ */

static char *capture(const char *cmd, size_t *out_len) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    size_t cap = 262144, n = 0;
    char *buf = malloc(cap);
    if (!buf) { pclose(fp); return NULL; }
    size_t r;
    while ((r = fread(buf + n, 1, cap - n - 1, fp)) > 0) {
        n += r;
        if (n + 1 >= cap) break;
    }
    pclose(fp);
    buf[n] = 0;
    if (out_len) *out_len = n;
    return buf;
}

/* ------------------------------------------------------------------ */
/* details                                                             */
/* ------------------------------------------------------------------ */

static void json_str_into(char *dst, size_t dstsz, json_value_t *obj, const char *key) {
    json_value_t *v = json_obj_get(obj, key);
    if (!v || v->type != JSON_STR) return;
    const char *src = v->u.str;
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstsz; i++) {
        char c = src[i];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        dst[j++] = c;
    }
    dst[j] = 0;
}

static void json_license_into(char *dst, size_t dstsz, json_value_t *meta) {
    json_value_t *lic = json_obj_get(meta, "license");
    if (!lic) return;
    if (lic->type == JSON_STR) {
        snprintf(dst, dstsz, "%s", lic->u.str);
        return;
    }
    if (lic->type == JSON_OBJ) {
        json_str_into(dst, dstsz, lic, "fullName");
        if (!dst[0]) json_str_into(dst, dstsz, lic, "shortName");
        return;
    }
    if (lic->type == JSON_ARR && lic->u.arr.n > 0) {
        json_value_t *first = lic->u.arr.items[0];
        if (first->type == JSON_STR) snprintf(dst, dstsz, "%s", first->u.str);
        else if (first->type == JSON_OBJ) {
            json_str_into(dst, dstsz, first, "fullName");
            if (!dst[0]) json_str_into(dst, dstsz, first, "shortName");
        }
    }
}

static void json_homepage_into(char *dst, size_t dstsz, json_value_t *meta) {
    json_value_t *h = json_obj_get(meta, "homepage");
    if (!h) return;
    if (h->type == JSON_STR) {
        snprintf(dst, dstsz, "%s", h->u.str);
        return;
    }
    if (h->type == JSON_ARR && h->u.arr.n > 0) {
        json_value_t *first = h->u.arr.items[0];
        if (first->type == JSON_STR) snprintf(dst, dstsz, "%s", first->u.str);
    }
}

static int details_for_nix(const char *app_id, pkg_details_t *out) {
    if (!valid_app_name(app_id)) return -1;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "PATH=$PATH:/run/current-system/sw/bin "
             "nix --extra-experimental-features 'nix-command flakes' "
             "eval --json 'nixpkgs#%s.meta' 2>/dev/null",
             app_id);

    size_t len = 0;
    char *txt = capture(cmd, &len);
    if (!txt || len < 3) { free(txt); return -1; }

    json_value_t *meta = json_parse(txt, len);
    free(txt);
    if (!meta || meta->type != JSON_OBJ) { json_free(meta); return -1; }

    json_str_into(out->description, sizeof(out->description), meta, "description");
    json_license_into(out->license, sizeof(out->license), meta);
    json_homepage_into(out->homepage, sizeof(out->homepage), meta);
    json_str_into(out->version, sizeof(out->version), meta, "version");

    json_value_t *maint = json_obj_get(meta, "maintainers");
    if (maint && maint->type == JSON_ARR && maint->u.arr.n > 0) {
        json_value_t *m0 = maint->u.arr.items[0];
        if (m0->type == JSON_OBJ) {
            json_str_into(out->developer, sizeof(out->developer), m0, "name");
        }
    }
    if (!out->developer[0]) {
        snprintf(out->developer, sizeof(out->developer), "NixOS Community");
    }

    out->is_gui = 1;
    if (out->description[0]) {
        char lower[1024];
        size_t j = 0;
        for (size_t i = 0; out->description[i] && j + 1 < sizeof(lower); i++)
            lower[j++] = (char)tolower((unsigned char)out->description[i]);
        lower[j] = 0;
        if (strstr(lower, "command line") || strstr(lower, "cli ") ||
            strstr(lower, "terminal")) {
            out->is_gui = 0;
        }
    }

    if (!out->safety_status[0]) {
        snprintf(out->safety_status, sizeof(out->safety_status), "System Level");
        snprintf(out->safety_desc, sizeof(out->safety_desc), "Runs with host privileges");
    }
    if (!out->age_rating[0]) {
        snprintf(out->age_rating, sizeof(out->age_rating), "All");
    }

    json_free(meta);
    out->loaded = 1;
    return 0;
}

static int details_for_flatpak(const char *app_id, pkg_details_t *out) {
    if (!valid_app_name(app_id)) return -1;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "PATH=$PATH:/run/current-system/sw/bin "
             "flatpak remote-info --user flathub '%s' 2>/dev/null || "
             "PATH=$PATH:/run/current-system/sw/bin "
             "flatpak remote-info flathub '%s' 2>/dev/null",
             app_id, app_id);

    size_t len = 0;
    char *txt = capture(cmd, &len);
    if (!txt || len < 4) { free(txt); return -1; }

    char *line = txt;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;

        char key[64] = {0}, val[1024] = {0};
        if (sscanf(line, " %63[^:]: %1023[^\n]", key, val) == 2) {
            if      (!strcmp(key, "Summary"))     snprintf(out->description, sizeof(out->description), "%s", val);
            else if (!strcmp(key, "License"))     snprintf(out->license, sizeof(out->license), "%s", val);
            else if (!strcmp(key, "Homepage"))    snprintf(out->homepage, sizeof(out->homepage), "%s", val);
            else if (!strcmp(key, "Version"))     snprintf(out->version, sizeof(out->version), "%s", val);
            else if (!strcmp(key, "Download"))    snprintf(out->size, sizeof(out->size), "%s", val);
            else if (!strcmp(key, "Bug Tracker")) snprintf(out->bugtracker, sizeof(out->bugtracker), "%s", val);
        }
        line = nl ? nl + 1 : NULL;
    }
    free(txt);

    if (!out->developer[0]) snprintf(out->developer, sizeof(out->developer), "Unknown Developer");
    if (!out->safety_status[0]) {
        snprintf(out->safety_status, sizeof(out->safety_status), "Sandboxed");
        snprintf(out->safety_desc, sizeof(out->safety_desc), "Runs in a Flatpak sandbox");
    }
    if (!out->age_rating[0]) snprintf(out->age_rating, sizeof(out->age_rating), "All");
    out->is_gui = 1;
    out->loaded = 1;
    return 0;
}

int nixpkg_details(const char *app_id, pkg_source_t src, pkg_details_t *out) {
    if (!app_id || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (src == SRC_FLATPAK) return details_for_flatpak(app_id, out);
    return details_for_nix(app_id, out);
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */

int nixpkg_search(const char *query, pkg_result_t **out, size_t *n_out) {
    *out = NULL; *n_out = 0;
    if (!query || !*query) return 0;

    for (const unsigned char *p = (const unsigned char*)query; *p; p++) {
        if (!(isalnum(*p) || *p==' ' || *p=='.' || *p=='_' || *p=='+' || *p=='-')) {
            fprintf(stderr, "nixpkg: invalid search query\n");
            return 1;
        }
    }

    char cmd[1200];
    snprintf(cmd, sizeof(cmd),
             "PATH=$PATH:/run/current-system/sw/bin "
             "nix --extra-experimental-features 'nix-command flakes' "
             "search --json nixpkgs '%s' 2>/dev/null | head -c 262144",
             query);

    size_t len = 0;
    char *json_text = capture(cmd, &len);
    if (!json_text || len < 3) { free(json_text); return 0; }

    if (json_text[len - 1] != '}') {
        ssize_t cut = -1;
        int depth = 0;
        int in_str = 0;
        for (size_t i = 0; i < len; i++) {
            char c = json_text[i];
            if (in_str) {
                if (c == '\\') { i++; continue; }
                if (c == '"') in_str = 0;
                continue;
            }
            if (c == '"') in_str = 1;
            else if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) cut = (ssize_t)i;
            }
        }
        if (cut > 0) {
            json_text[cut + 1] = 0;
            len = (size_t)cut + 1;
        } else {
            free(json_text);
            return 0;
        }
    }

    json_value_t *root = json_parse(json_text, len);
    free(json_text);
    if (!root || root->type != JSON_OBJ) { json_free(root); return 0; }

    size_t cap = 64, n = 0;
    pkg_result_t *res = malloc(cap * sizeof(*res));
    if (!res) { json_free(root); return 1; }

    const size_t MAX_KEEP = 200;

    for (size_t i = 0; i < root->u.obj.n && n < MAX_KEEP; i++) {
        const char *full_attr = root->u.obj.keys[i];
        json_value_t *val = root->u.obj.vals[i];

        const char *name = NULL;
        const char *desc = NULL;
        json_value_t *pname = json_obj_get(val, "pname");
        json_value_t *nm    = json_obj_get(val, "name");
        json_value_t *meta  = json_obj_get(val, "meta");

        if (pname && pname->type == JSON_STR) name = pname->u.str;
        else if (nm && nm->type == JSON_STR) name = nm->u.str;
        else name = full_attr;

        if (meta && meta->type == JSON_OBJ) {
            json_value_t *d = json_obj_get(meta, "description");
            if (d && d->type == JSON_STR) desc = d->u.str;
        }
        if (!desc) {
            json_value_t *d = json_obj_get(val, "description");
            if (d && d->type == JSON_STR) desc = d->u.str;
        }
        if (!name) continue;

        if (n == cap) {
            cap *= 2;
            pkg_result_t *nr = realloc(res, cap * sizeof(*res));
            if (!nr) { free(res); json_free(root); return 1; }
            res = nr;
        }
        pkg_result_t *r = &res[n++];
        memset(r, 0, sizeof(*r));
        r->source = SRC_NIX;
        snprintf(r->name, sizeof(r->name), "%s", name);

        const char *attr = full_attr;
        const char *dot = strstr(attr, "legacyPackages.");
        if (dot == attr) {
            const char *second = strchr(dot + 15, '.');
            if (second) attr = second + 1;
        }
        snprintf(r->app_id, sizeof(r->app_id), "%s", attr);
        if (desc) snprintf(r->description, sizeof(r->description), "%s", desc);
        r->installed = 0;
    }

    json_free(root);
    *out = res;
    *n_out = n;
    return 0;
}
