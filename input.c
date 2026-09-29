/* input.c — evdev input reader, glibc-only */
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define MAX_DEVS 32

/* Renamed from dev_t to avoid clashing with sys/types.h's dev_t. */
typedef struct {
    int fd;
    int is_mouse;
    int abs_x, abs_y;
    int rel_x, rel_y;
    int abs_max_x, abs_max_y;
    int btn_left;
} evdev_t;

static evdev_t g_devs[MAX_DEVS];
static int     g_ndevs = 0;

static int looks_like_keyboard(int fd) {
    unsigned long bits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), &bits) < 0) return 0;
    return (bits & (1UL << EV_KEY)) != 0;
}

static int looks_like_mouse(int fd) {
    unsigned long bits = 0;
    if (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), &bits) < 0) return 0;
    int has_rel = (bits & (1UL << EV_REL)) != 0;
    int has_abs = (bits & (1UL << EV_ABS)) != 0;
    return has_rel || has_abs;
}

static void query_abs_range(int fd, evdev_t *d) {
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0 && ai.maximum > ai.minimum) {
        d->abs_max_x = ai.maximum;
    }
    if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0 && ai.maximum > ai.minimum) {
        d->abs_max_y = ai.maximum;
    }
}

int input_open(void) {
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        fprintf(stderr, "nixpkg-fb: cannot open /dev/input: %s\n", strerror(errno));
        return -1;
    }
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && g_ndevs < MAX_DEVS) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[320];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        int is_kb = looks_like_keyboard(fd);
        int is_mo = looks_like_mouse(fd);
        if (!is_kb && !is_mo) { close(fd); continue; }
        evdev_t *d = &g_devs[g_ndevs++];
        memset(d, 0, sizeof(*d));
        d->fd = fd;
        d->is_mouse = is_mo && !is_kb;
        if (d->is_mouse) query_abs_range(fd, d);
    }
    closedir(dir);
    if (g_ndevs == 0) {
        fprintf(stderr, "nixpkg-fb: no input devices found (are you in group 'input'?)\n");
        return -1;
    }
    return 0;
}

void input_close(void) {
    for (int i = 0; i < g_ndevs; i++) if (g_devs[i].fd >= 0) close(g_devs[i].fd);
    g_ndevs = 0;
}

/* Map evdev KEY_* codes we care about to our key enum. */
static ikey_t map_key(uint16_t code) {
    switch (code) {
        case KEY_UP:        return IKEY_UP;
        case KEY_DOWN:      return IKEY_DOWN;
        case KEY_LEFT:      return IKEY_LEFT;
        case KEY_RIGHT:     return IKEY_RIGHT;
        case KEY_ENTER:     return IKEY_ENTER;
        case KEY_ESC:       return IKEY_ESC;
        case KEY_BACKSPACE: return IKEY_BACKSPACE;
        case KEY_Q:         return IKEY_CHAR;
    }
    if (code >= KEY_1 && code <= KEY_9) return IKEY_CHAR;
    if (code >= KEY_A && code <= KEY_Z) return IKEY_CHAR;
    if (code == KEY_0)     return IKEY_CHAR;
    if (code == KEY_SPACE) return IKEY_CHAR;
    if (code == KEY_DOT)   return IKEY_CHAR;
    if (code == KEY_MINUS) return IKEY_CHAR;
    if (code == KEY_SLASH) return IKEY_CHAR;
    return IKEY_NONE;
}

static char key_to_char(uint16_t code) {
    if (code >= KEY_A && code <= KEY_Z) return (char)('a' + (code - KEY_A));
    if (code >= KEY_1 && code <= KEY_9) return (char)('1' + (code - KEY_1));
    if (code == KEY_0)     return '0';
    if (code == KEY_SPACE) return ' ';
    if (code == KEY_DOT)   return '.';
    if (code == KEY_MINUS) return '-';
    if (code == KEY_SLASH) return '/';
    if (code == KEY_Q)     return 'q';
    return 0;
}

int input_poll(input_event_t *ev, int timeout_ms) {
    memset(ev, 0, sizeof(*ev));

    struct pollfd pfds[MAX_DEVS];
    for (int i = 0; i < g_ndevs; i++) {
        pfds[i].fd = g_devs[i].fd;
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }
    int pr = poll(pfds, (nfds_t)g_ndevs, timeout_ms);
    if (pr < 0) return -1;
    if (pr == 0) return 0;

    for (int i = 0; i < g_ndevs; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;
        evdev_t *d = &g_devs[i];
        struct input_event ie;
        while (read(d->fd, &ie, sizeof(ie)) == (ssize_t)sizeof(ie)) {
            if (ie.type == EV_KEY && ie.value == 1) {
                if (d->is_mouse && ie.code == BTN_LEFT) {
                    ev->key = IKEY_MOUSE_LEFT;
                    ev->mx = d->abs_max_x ? d->abs_x * 1920 / d->abs_max_x : d->abs_x;
                    ev->my = d->abs_max_y ? d->abs_y * 1080 / d->abs_max_y : d->abs_y;
                    return 1;
                }
                if (!d->is_mouse) {
                    ikey_t k = map_key(ie.code);
                    if (k != IKEY_NONE) {
                        ev->key = k;
                        if (k == IKEY_CHAR) ev->ch = key_to_char(ie.code);
                        return 1;
                    }
                }
            } else if (ie.type == EV_REL && d->is_mouse) {
                if (ie.code == REL_X) d->rel_x += ie.value;
                if (ie.code == REL_Y) d->rel_y += ie.value;
            } else if (ie.type == EV_ABS && d->is_mouse) {
                if (ie.code == ABS_X) d->abs_x = ie.value;
                if (ie.code == ABS_Y) d->abs_y = ie.value;
            } else if (ie.type == EV_SYN && d->is_mouse && (d->rel_x || d->rel_y)) {
                d->abs_x += d->rel_x;
                d->abs_y += d->rel_y;
                d->rel_x = d->rel_y = 0;
                if (d->abs_x < 0) d->abs_x = 0;
                if (d->abs_y < 0) d->abs_y = 0;
                ev->key = IKEY_MOUSE_MOVE;
                ev->mx = d->abs_x;
                ev->my = d->abs_y;
                return 1;
            }
        }
    }
    return 0;
}
