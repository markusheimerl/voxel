#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "math.h"
#include "io.h"
#include "camera.h"
#include "world.h"
#include "renderer.h"
#include "player.h"

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {

    /* ---------------------------------------------------------------------- */
    /* IO / Window                                                            */
    /* ---------------------------------------------------------------------- */

    const char *window_title_game = "Voxel Engine";
    IOContext *io = io_create(window_title_game);
    void *display = io_get_display(io);
    unsigned long window = io_get_window(io);
    uint32_t window_width, window_height;
    io_get_window_size(io, &window_width, &window_height);

    /* ---------------------------------------------------------------------- */
    /* Renderer                                                               */
    /* ---------------------------------------------------------------------- */

    Renderer *renderer = renderer_create(display, window, window_width, window_height);

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
        world_update_chunks(&world, player.position);

        bool mouse_moved = false;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        bool left_click = false;
        bool right_click = false;

        IOEvent event;
        while (io_poll_event(io, &event)) {
            switch (event.type) {
            case IO_EVENT_QUIT:
                running = false;
                break;

            case IO_EVENT_KEY_DOWN: {
                uint32_t key = event.data.key.key;
                bool was_down = (key != IO_KEY_UNKNOWN && key < 256) ? keys[key] : false;
                if (key == IO_KEY_ESCAPE) {
                    if (mouse_captured) {
                        io_set_mouse_capture(io, false);
                        mouse_captured = false;
                        first_mouse = true;
                    }
                } else if (key == 'E' || key == 'e') {
                    if (!was_down) {
                        player.inventory_open = !player.inventory_open;
                        if (player.inventory_open && mouse_captured) {
                            io_set_mouse_capture(io, false);
                            mouse_captured = false;
                            first_mouse = true;
                        } else if (!player.inventory_open && !mouse_captured) {
                            io_set_mouse_capture(io, true);
                            mouse_captured = true;
                            first_mouse = true;
                        }
                        io_set_window_title(io, window_title_game);
                    }
                } else if (!player.inventory_open && key >= '1' && key <= '9') {
                    player.selected_slot = (uint8_t)(key - '1');
                }

                if (key != IO_KEY_UNKNOWN && key < 256) keys[key] = true;
            } break;

            case IO_EVENT_KEY_UP: {
                uint32_t key = event.data.key.key;
                if (key != IO_KEY_UNKNOWN && key < 256) keys[key] = false;
            } break;

            case IO_EVENT_MOUSE_MOVE:
                if (mouse_captured) {
                    mouse_x = (float)event.data.mouse_move.x;
                    mouse_y = (float)event.data.mouse_move.y;
                    mouse_moved = true;
                }
                break;

            case IO_EVENT_MOUSE_BUTTON:
                if (player.inventory_open) {
                    if (event.data.mouse_button.button == IO_MOUSE_BUTTON_LEFT) {
                        float aspect = (float)window_height / (float)window_width;
                        int slot = player_inventory_slot_from_mouse(aspect,
                                                                     (float)event.data.mouse_button.x,
                                                                     (float)event.data.mouse_button.y,
                                                                     (float)window_width,
                                                                     (float)window_height);
                        if (slot >= 0) player.selected_slot = (uint8_t)slot;
                    }
                    break;
                }
                if (event.data.mouse_button.button == IO_MOUSE_BUTTON_LEFT) {
                    if (!mouse_captured) {
                        io_set_mouse_capture(io, true);
                        mouse_captured = true;
                        first_mouse = true;
                    } else {
                        left_click = true;
                    }
                } else if (event.data.mouse_button.button == IO_MOUSE_BUTTON_RIGHT) {
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

            io_warp_mouse(io, center_x, center_y);
            io_flush(io);

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

        /* Periodic autosave */
        double since_autosave =
            (now.tv_sec - last_autosave.tv_sec) +
            (now.tv_nsec - last_autosave.tv_nsec) / 1000000000.0;

        if (since_autosave > 5.0) {
            world_save_flush(&save);
            last_autosave = now;
        }

        renderer_draw_frame(renderer,
                            &world,
                            &player,
                            &camera,
                            ray_hit.hit,
                            ray_hit.cell);
    }

    /* ---------------------------------------------------------------------- */
    /* Cleanup                                                                 */
    /* ---------------------------------------------------------------------- */

    world_destroy(&world);
    world_save_destroy(&save);

    renderer_destroy(renderer);

    io_destroy(io);

    return 0;
}