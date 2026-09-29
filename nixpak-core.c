#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

#define CONFIG_FILE "/etc/nixos/nixpkg-apps.nix"
#define MAX_LINES 1000

int run_with_spinner(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return 1;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        printf("%s", buffer);
        fflush(stdout);
    }
    return WEXITSTATUS(pclose(fp));
}

bool is_flatpak(const char *name) { 
    return strncmp(name, "flatpak:", 8) == 0; 
}

int mutate_package(const char *action, const char *app_name) {
    if (is_flatpak(app_name)) {
        char cmd[512];
        if (strcmp(action, "install") == 0) {
            snprintf(cmd, sizeof(cmd), "PATH=$PATH:/run/current-system/sw/bin flatpak install -y flathub %s 2>&1", app_name + 8);
        } else {
            snprintf(cmd, sizeof(cmd), "PATH=$PATH:/run/current-system/sw/bin flatpak uninstall -y %s 2>&1", app_name + 8);
        }
        return run_with_spinner(cmd);
    }

    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return 1;
    
    char *lines[MAX_LINES];
    int line_count = 0;
    int start_idx = -1;
    bool found = false;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), fp) && line_count < MAX_LINES) {
        if (strstr(buffer, "# NIXPKG_START") != NULL) {
            start_idx = line_count;
        }
        char trimmed[256] = {0};
        if (sscanf(buffer, " %255s ", trimmed) == 1 && strcmp(trimmed, app_name) == 0) {
            found = true;
            if (strcmp(action, "remove") == 0) continue; 
        }
        lines[line_count++] = strdup(buffer);
    }
    fclose(fp);

    if (strcmp(action, "install") == 0 && found) { 
        for(int i=0; i<line_count; i++) free(lines[i]); 
        return 0; 
    }
    if (strcmp(action, "remove") == 0 && !found) { 
        for(int i=0; i<line_count; i++) free(lines[i]); 
        return 1; 
    }
    if (strcmp(action, "install") == 0 && start_idx == -1) { 
        for(int i=0; i<line_count; i++) free(lines[i]); 
        return 1; 
    }

    fp = fopen(CONFIG_FILE, "w");
    for (int i = 0; i < line_count; i++) {
        fprintf(fp, "%s", lines[i]);
        if (strcmp(action, "install") == 0 && i == start_idx) {
            fprintf(fp, "    %s\n", app_name);
        }
        free(lines[i]);
    }
    fclose(fp);
    
    printf("Successfully updated %s.\n", CONFIG_FILE);
    return 0;
}

int garbage_collect() {
    printf("Sweeping old generations...\n");
    run_with_spinner("PATH=$PATH:/run/current-system/sw/bin nix-env --delete-generations old -p /nix/var/nix/profiles/system 2>&1");
    printf("Collecting garbage from /nix/store...\n");
    return run_with_spinner("PATH=$PATH:/run/current-system/sw/bin nix-collect-garbage -d 2>&1");
}

int do_rebuild(const char *mode) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "PATH=$PATH:/run/current-system/sw/bin nixos-rebuild %s 2>&1", mode);
    return run_with_spinner(cmd);
}

int do_rollback() {
    return run_with_spinner("PATH=$PATH:/run/current-system/sw/bin nixos-rebuild switch --rollback 2>&1");
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    
    if (strcmp(argv[1], "gc") == 0) return garbage_collect();
    if (strcmp(argv[1], "test") == 0) return do_rebuild("test");
    if (strcmp(argv[1], "switch") == 0) return do_rebuild("switch");
    if (strcmp(argv[1], "boot") == 0) return do_rebuild("boot");
    if (strcmp(argv[1], "rollback") == 0) return do_rollback();
    
    if (argc < 3) return 1;
    if (strcmp(argv[1], "install") == 0) return mutate_package("install", argv[2]);
    if (strcmp(argv[1], "remove") == 0) return mutate_package("remove", argv[2]);
    
    return 1;
}
