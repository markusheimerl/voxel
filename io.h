#ifndef IO_H
#define IO_H

#include <stdbool.h>
#include <stdint.h>

typedef struct IOContext IOContext;

typedef enum {
    IO_EVENT_NONE = 0,
    IO_EVENT_QUIT,
    IO_EVENT_RESIZE,
    IO_EVENT_KEY_DOWN,
    IO_EVENT_KEY_UP,
    IO_EVENT_MOUSE_MOVE,
    IO_EVENT_MOUSE_BUTTON
} IOEventType;

typedef enum {
    IO_MOUSE_BUTTON_LEFT = 1,
    IO_MOUSE_BUTTON_MIDDLE = 2,
    IO_MOUSE_BUTTON_RIGHT = 3
} IOMouseButton;

#define IO_KEY_UNKNOWN 0u
#define IO_KEY_ESCAPE 0x100u

typedef struct {
    IOEventType type;
    union {
        struct { uint32_t width, height; } resize;
        struct { uint32_t key; } key;
        struct { int x, y; } mouse_move;
        struct { uint8_t button; int x, y; } mouse_button;
    } data;
} IOEvent;

IOContext *io_create(const char *title);
void io_destroy(IOContext *io);

void *io_get_display(IOContext *io);
unsigned long io_get_window(IOContext *io);

bool io_poll_event(IOContext *io, IOEvent *event);
void io_set_window_title(IOContext *io, const char *title);
void io_get_window_size(IOContext *io, uint32_t *out_width, uint32_t *out_height);
void io_set_mouse_capture(IOContext *io, bool capture);
void io_warp_mouse(IOContext *io, int x, int y);
void io_flush(IOContext *io);

#endif /* IO_H */