/* main.c — CLI, TUI, and framebuffer dispatcher */
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int fb_main(int argc, char **argv);

static void usage(void) {
    fputs(
        "usage: nixpkg <command> [args]\n"
        "  install <attr>     add to " NIXPKG_CFG "\n"
        "  remove  <attr>     remove from " NIXPKG_CFG "\n"
        "  gc                 garbage collect\n"
        "  test|switch|boot   nixos-rebuild <mode>\n"
        "  rollback           nixos-rebuild switch --rollback\n"
        "  search <query>     print results\n"
        "  tui                terminal UI (ANSI)\n"
        "  fb                 framebuffer UI (requires /dev/fb0 + evdev)\n",
        stderr);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *c = argv[1];

    if (!strcmp(c, "install") && argc >= 3) return nixpkg_mutate("install", argv[2]);
    if (!strcmp(c, "remove")  && argc >= 3) return nixpkg_mutate("remove",  argv[2]);
    if (!strcmp(c, "gc"))       return nixpkg_gc();
    if (!strcmp(c, "test"))     return nixpkg_rebuild("test");
    if (!strcmp(c, "switch"))   return nixpkg_rebuild("switch");
    if (!strcmp(c, "boot"))     return nixpkg_rebuild("boot");
    if (!strcmp(c, "rollback")) return nixpkg_rollback();
    if (!strcmp(c, "tui"))      return tui_main(argc - 1, argv + 1);
    if (!strcmp(c, "fb"))       return fb_main(argc - 1, argv + 1);

    if (!strcmp(c, "search") && argc >= 3) {
        pkg_result_t *r = NULL; size_t n = 0;
        int rc = nixpkg_search(argv[2], &r, &n);
        if (rc) return rc;
        for (size_t i = 0; i < n; i++)
            printf("%s|%s|%s\n", r[i].name, r[i].description, r[i].app_id);
        free(r);
        return 0;
    }

    usage();
    return 1;
}
