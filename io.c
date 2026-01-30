#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

/* -------------------------------------------------------------------------- */
/* IOContext Structure                                                        */
/* -------------------------------------------------------------------------- */

typedef struct IOContext {
    Display *display;
    Window window;
    Atom wm_delete_window;
    Cursor invisible_cursor;
} IOContext;

/* -------------------------------------------------------------------------- */
/* Error Handling & Utilities                                                 */
/* -------------------------------------------------------------------------- */

static void io_die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

static uint32_t io_keysym_to_key(KeySym sym) {
    if (sym == XK_Escape) return IO_KEY_ESCAPE;
    if (sym < 256) return (uint32_t)sym;
    return IO_KEY_UNKNOWN;
}

/* -------------------------------------------------------------------------- */
/* Window & Cursor Creation Helpers                                           */
/* -------------------------------------------------------------------------- */

static Window create_window(Display *display, int screen) {
    Window root = RootWindow(display, screen);
    return XCreateSimpleWindow(display, root, 0, 0, 800, 600, 1,
                               BlackPixel(display, screen),
                               WhitePixel(display, screen));
}

static void maximize_window(Display *display, Window window) {
    /* Wait for MapNotify */
    XEvent xev;
    while (1) {
        XNextEvent(display, &xev);
        if (xev.type == MapNotify && xev.xmap.window == window) break;
    }

    /* Send maximize request */
    Window root = DefaultRootWindow(display);
    Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom max_vert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom max_horz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);

    XEvent ev = {0};
    ev.type = ClientMessage;
    ev.xclient.window = window;
    ev.xclient.message_type = wm_state;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;
    ev.xclient.data.l[1] = (long)max_horz;
    ev.xclient.data.l[2] = (long)max_vert;
    ev.xclient.data.l[3] = 1;

    XSendEvent(display, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(display);
}

static Cursor create_invisible_cursor(Display *display, Window window, int screen) {
    char empty_data[8] = {0};
    Pixmap blank = XCreateBitmapFromData(display, window, empty_data, 8, 8);
    
    XColor black;
    XAllocNamedColor(display, DefaultColormap(display, screen),
                     "black", &black, &black);
    
    Cursor cursor = XCreatePixmapCursor(display, blank, blank,
                                        &black, &black, 0, 0);
    XFreePixmap(display, blank);
    return cursor;
}

/* -------------------------------------------------------------------------- */
/* IOContext Creation & Destruction                                           */
/* -------------------------------------------------------------------------- */

IOContext *io_create(const char *title) {
    IOContext *io = calloc(1, sizeof(*io));
    if (!io) io_die("Failed to allocate IOContext");

    /* Open display */
    io->display = XOpenDisplay(NULL);
    if (!io->display) io_die("Failed to open X11 display");

    int screen = DefaultScreen(io->display);

    /* Create window */
    io->window = create_window(io->display, screen);
    if (title) XStoreName(io->display, io->window, title);

    /* Select input events */
    XSelectInput(io->display, io->window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);

    /* Setup WM protocols */
    io->wm_delete_window = XInternAtom(io->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(io->display, io->window, &io->wm_delete_window, 1);

    /* Map and maximize window */
    XMapWindow(io->display, io->window);
    maximize_window(io->display, io->window);

    /* Create invisible cursor */
    io->invisible_cursor = create_invisible_cursor(io->display, io->window, screen);

    return io;
}

void io_destroy(IOContext *io) {
    if (!io) return;

    if (io->display) {
        if (io->invisible_cursor) XFreeCursor(io->display, io->invisible_cursor);
        if (io->window) XDestroyWindow(io->display, io->window);
        XCloseDisplay(io->display);
    }

    free(io);
}

/* -------------------------------------------------------------------------- */
/* Accessors                                                                  */
/* -------------------------------------------------------------------------- */

void *io_get_display(IOContext *io) {
    return io ? (void *)io->display : NULL;
}

unsigned long io_get_window(IOContext *io) {
    return io ? (unsigned long)io->window : 0UL;
}

/* -------------------------------------------------------------------------- */
/* Event Polling                                                              */
/* -------------------------------------------------------------------------- */

bool io_poll_event(IOContext *io, IOEvent *event) {
    if (!io || !event) return false;
    if (XPending(io->display) <= 0) return false;

    XEvent xevent;
    XNextEvent(io->display, &xevent);

    event->type = IO_EVENT_NONE;

    switch (xevent.type) {
    case ClientMessage:
        if ((Atom)xevent.xclient.data.l[0] == io->wm_delete_window) {
            event->type = IO_EVENT_QUIT;
        }
        break;

    case ConfigureNotify:
        event->type = IO_EVENT_RESIZE;
        event->data.resize.width = (uint32_t)xevent.xconfigure.width;
        event->data.resize.height = (uint32_t)xevent.xconfigure.height;
        break;

    case KeyPress:
        event->type = IO_EVENT_KEY_DOWN;
        event->data.key.key = io_keysym_to_key(XLookupKeysym(&xevent.xkey, 0));
        break;

    case KeyRelease:
        event->type = IO_EVENT_KEY_UP;
        event->data.key.key = io_keysym_to_key(XLookupKeysym(&xevent.xkey, 0));
        break;

    case MotionNotify:
        event->type = IO_EVENT_MOUSE_MOVE;
        event->data.mouse_move.x = xevent.xmotion.x;
        event->data.mouse_move.y = xevent.xmotion.y;
        break;

    case ButtonPress:
        event->type = IO_EVENT_MOUSE_BUTTON;
        event->data.mouse_button.button = (uint8_t)xevent.xbutton.button;
        event->data.mouse_button.x = xevent.xbutton.x;
        event->data.mouse_button.y = xevent.xbutton.y;
        break;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Window Control                                                             */
/* -------------------------------------------------------------------------- */

void io_set_window_title(IOContext *io, const char *title) {
    if (!io || !title) return;
    XStoreName(io->display, io->window, title);
}

void io_get_window_size(IOContext *io, uint32_t *out_width, uint32_t *out_height) {
    if (!io) return;

    XWindowAttributes attrs;
    XGetWindowAttributes(io->display, io->window, &attrs);

    if (out_width) *out_width = (uint32_t)attrs.width;
    if (out_height) *out_height = (uint32_t)attrs.height;
}

/* -------------------------------------------------------------------------- */
/* Mouse Control                                                              */
/* -------------------------------------------------------------------------- */

void io_set_mouse_capture(IOContext *io, bool capture) {
    if (!io) return;

    if (capture) {
        XGrabPointer(io->display, io->window, True, PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, io->window, None, CurrentTime);
        XDefineCursor(io->display, io->window, io->invisible_cursor);
    } else {
        XUngrabPointer(io->display, CurrentTime);
        XUndefineCursor(io->display, io->window);
    }
}

void io_warp_mouse(IOContext *io, int x, int y) {
    if (!io) return;
    XWarpPointer(io->display, None, io->window, 0, 0, 0, 0, x, y);
}

void io_flush(IOContext *io) {
    if (!io) return;
    XFlush(io->display);
}