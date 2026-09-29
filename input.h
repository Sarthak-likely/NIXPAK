#ifndef NIXPKG_INPUT_H
#define NIXPKG_INPUT_H

#include <stdint.h>

/* Key events we care about, normalized from evdev. */
typedef enum {
    IKEY_NONE = 0,
    IKEY_UP, IKEY_DOWN, IKEY_LEFT, IKEY_RIGHT,
    IKEY_ENTER, IKEY_ESC, IKEY_BACKSPACE,
    IKEY_CHAR,
    IKEY_MOUSE_LEFT, IKEY_MOUSE_MOVE,
    IKEY_QUIT
} ikey_t;

typedef struct {
    ikey_t key;
    char ch;
    int mx, my;    /* absolute cursor position for mouse events */
} input_event_t;

int  input_open(void);           /* opens /dev/input/event*, returns 0 ok */
void input_close(void);
int  input_poll(input_event_t *ev, int timeout_ms);  /* 1 = got event, 0 = timeout, -1 = error */

#endif
