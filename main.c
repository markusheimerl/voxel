#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "math.h"
#include "camera.h"
#include "world.h"
#include "voxel.h"
#include "player.h"

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    uint32_t window_width = 800;
    uint32_t window_height = 600;

    /* ---------------------------------------------------------------------- */
    /* X11 Setup                                                              */
    /* ---------------------------------------------------------------------- */

    Display *display = XOpenDisplay(NULL);
    if (!display) die("Failed to open X11 display");

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window window = XCreateSimpleWindow(display, root,
                                        0, 0,
                                        window_width,
                                        window_height,
                                        1,
                                        BlackPixel(display, screen),
                                        WhitePixel(display, screen));

    const char *window_title_game = "Voxel Engine";
    XStoreName(display, window, window_title_game);
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);

    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XMapWindow(display, window);

    char empty_cursor_data[8] = {0};
    Pixmap blank = XCreateBitmapFromData(display, window, empty_cursor_data, 8, 8);
    Colormap colormap = DefaultColormap(display, screen);
    XColor black, dummy;
    XAllocNamedColor(display, colormap, "black", &black, &dummy);
    Cursor invisible_cursor = XCreatePixmapCursor(display, blank, blank, &black, &black, 0, 0);
    XFreePixmap(display, blank);

    /* ---------------------------------------------------------------------- */
    /* Renderer (Vulkan)                                                       */
    /* ---------------------------------------------------------------------- */

    VoxelRenderer *renderer = voxel_renderer_create(display, window, window_width, window_height);
    bool swapchain_needs_recreate = false;

    /* ---------------------------------------------------------------------- */
    /* World Save + Voxel World                                                */
    /* ---------------------------------------------------------------------- */

    WorldSave save;
    world_save_init(&save, WORLD_SAVE_FILE);
    (void)world_save_load(&save);

    World world;
    world_init(&world, &save);
    world_update_chunks(&world, world.spawn_position);
    if (!world.spawn_set) world.spawn_position = vec3(0.0f, 4.5f, 0.0f);

    /* ---------------------------------------------------------------------- */
    /* Player / Camera                                                        */
    /* ---------------------------------------------------------------------- */

    Player player;
    player_init(&player, world.spawn_position);

    Camera camera;
    camera_init(&camera);
    camera_follow_player(&camera, &player);

    world_update_chunks(&world, player.position);

    /* Input State */
    bool keys[256] = {0};
    bool mouse_captured = false;
    bool first_mouse = true;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;


    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    struct timespec last_autosave;
    clock_gettime(CLOCK_MONOTONIC, &last_autosave);

    /* ---------------------------------------------------------------------- */
    /* Main Loop                                                               */
    /* ---------------------------------------------------------------------- */

    bool running = true;

    while (running) {
        if (swapchain_needs_recreate) {
            XWindowAttributes attrs;
            XGetWindowAttributes(display, window, &attrs);
            if (attrs.width == 0 || attrs.height == 0) {
                swapchain_needs_recreate = false;
                continue;
            }

            window_width = (uint32_t)attrs.width;
            window_height = (uint32_t)attrs.height;
            voxel_renderer_request_resize(renderer, window_width, window_height);
            swapchain_needs_recreate = false;
        }

        world_update_chunks(&world, player.position);

        bool mouse_moved = false;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        bool left_click = false;
        bool right_click = false;

        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);

            switch (event.type) {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == wm_delete_window) running = false;
                break;

            case ConfigureNotify:
                if (event.xconfigure.width != (int)window_width ||
                    event.xconfigure.height != (int)window_height)
                {
                    swapchain_needs_recreate = true;
                }
                break;

            case KeyPress: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                bool was_down = (sym < 256) ? keys[sym] : false;
                if (sym == XK_Escape) {
                    if (mouse_captured) {
                        XUngrabPointer(display, CurrentTime);
                        XUndefineCursor(display, window);
                        mouse_captured = false;
                        first_mouse = true;
                    }
                } else if (sym == XK_E || sym == XK_e) {
                    if (!was_down) {
                        player.inventory_open = !player.inventory_open;
                        if (player.inventory_open && mouse_captured) {
                            XUngrabPointer(display, CurrentTime);
                            XUndefineCursor(display, window);
                            mouse_captured = false;
                            first_mouse = true;
                        } else if (!player.inventory_open && !mouse_captured) {
                            XGrabPointer(display, window, True, PointerMotionMask,
                                         GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
                            XDefineCursor(display, window, invisible_cursor);
                            mouse_captured = true;
                            first_mouse = true;
                        }
                        XStoreName(display, window, window_title_game);
                    }
                } else if (!player.inventory_open && sym >= XK_1 && sym <= XK_9) {
                    player.selected_slot = (uint8_t)(sym - XK_1);
                }

                if (sym < 256) keys[sym] = true;
            } break;

            case KeyRelease: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym < 256) keys[sym] = false;
            } break;

            case MotionNotify:
                if (mouse_captured) {
                    mouse_x = (float)event.xmotion.x;
                    mouse_y = (float)event.xmotion.y;
                    mouse_moved = true;
                }
                break;

            case ButtonPress:
                if (player.inventory_open) {
                    if (event.xbutton.button == Button1) {
                        float aspect = (float)window_height / (float)window_width;
                        int slot = player_inventory_slot_from_mouse(aspect,
                                                                     (float)event.xbutton.x,
                                                                     (float)event.xbutton.y,
                                                                     (float)window_width,
                                                                     (float)window_height);
                        if (slot >= 0) player.selected_slot = (uint8_t)slot;
                    }
                    break;
                }
                if (event.xbutton.button == Button1) {
                    if (!mouse_captured) {
                        XGrabPointer(display, window, True, PointerMotionMask,
                                     GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
                        XDefineCursor(display, window, invisible_cursor);
                        mouse_captured = true;
                        first_mouse = true;
                    } else {
                        left_click = true;
                    }
                } else if (event.xbutton.button == Button3) {
                    if (mouse_captured) right_click = true;
                }
                break;

            default:
                break;
            }
        }

        if (mouse_captured && mouse_moved) {
            int center_x = (int)window_width / 2;
            int center_y = (int)window_height / 2;

            if (first_mouse) {
                last_mouse_x = (float)center_x;
                last_mouse_y = (float)center_y;
                first_mouse = false;
            }

            float x_offset = mouse_x - last_mouse_x;
            float y_offset = last_mouse_y - mouse_y;

            camera_process_mouse(&camera, x_offset, y_offset);

            XWarpPointer(display, None, window, 0, 0, 0, 0, center_x, center_y);
            XFlush(display);

            last_mouse_x = (float)center_x;
            last_mouse_y = (float)center_y;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_time =
            (now.tv_sec - last_time.tv_sec) +
            (now.tv_nsec - last_time.tv_nsec) / 1000000000.0f;
        last_time = now;

        const float MAX_DELTA_TIME = 0.1f;
        if (delta_time > MAX_DELTA_TIME) delta_time = MAX_DELTA_TIME;

        Vec3 move_delta;
        bool wants_jump;
        player_compute_movement(&player,
                                &camera,
                                keys,
                                mouse_captured && !player.inventory_open,
                                delta_time,
                                &move_delta,
                                &wants_jump);

        player_apply_physics(&player, &world, delta_time, move_delta, wants_jump);
        camera_follow_player(&camera, &player);

        RayHit ray_hit = raycast_blocks(&world, camera.position, camera.front, 6.0f);

        player_handle_block_interaction(&player,
                                        &world,
                                        ray_hit,
                                        left_click,
                                        right_click,
                                        mouse_captured && !player.inventory_open);

        /* Periodic autosave (single-file) */
        double since_autosave =
            (now.tv_sec - last_autosave.tv_sec) +
            (now.tv_nsec - last_autosave.tv_nsec) / 1000000000.0;

        if (since_autosave > 5.0) {
            world_save_flush(&save);
            last_autosave = now;
        }

        if (swapchain_needs_recreate) continue;

        bool needs_recreate = voxel_renderer_draw_frame(renderer,
                                                        &world,
                                                        &player,
                                                        &camera,
                                                        ray_hit.hit,
                                                        ray_hit.cell);
        if (needs_recreate) swapchain_needs_recreate = true;
    }

    /* ---------------------------------------------------------------------- */
    /* Cleanup                                                                 */
    /* ---------------------------------------------------------------------- */
    /* Flush all chunks into the single save file */
    world_destroy(&world);
    world_save_destroy(&save);

    voxel_renderer_destroy(renderer);

    XFreeCursor(display, invisible_cursor);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}