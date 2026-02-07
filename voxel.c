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
#include "entity.h"
#include "renderer.h"
#include "player.h"

/* -------------------------------------------------------------------------- */
/* Time Management                                                            */
/* -------------------------------------------------------------------------- */

typedef struct {
    struct timespec last_frame;
    struct timespec last_autosave;
} TimeState;

static void time_state_init(TimeState *ts) {
    clock_gettime(CLOCK_MONOTONIC, &ts->last_frame);
    ts->last_autosave = ts->last_frame;
}

static float time_state_delta(TimeState *ts) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    float delta = (now.tv_sec - ts->last_frame.tv_sec) +
                  (now.tv_nsec - ts->last_frame.tv_nsec) / 1000000000.0f;
    ts->last_frame = now;
    
    return delta < 0.1f ? delta : 0.1f;
}

static bool time_state_should_autosave(TimeState *ts) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    double elapsed = (now.tv_sec - ts->last_autosave.tv_sec) +
                     (now.tv_nsec - ts->last_autosave.tv_nsec) / 1000000000.0;
    
    if (elapsed > 5.0) {
        ts->last_autosave = now;
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Mouse State                                                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool captured;
    bool first_mouse;
    float last_x;
    float last_y;
} MouseState;

static void mouse_state_init(MouseState *ms) {
    ms->captured = false;
    ms->first_mouse = true;
    ms->last_x = 0.0f;
    ms->last_y = 0.0f;
}

static void mouse_state_capture(MouseState *ms, IOContext *io) {
    io_set_mouse_capture(io, true);
    ms->captured = true;
    ms->first_mouse = true;
}

static void mouse_state_release(MouseState *ms, IOContext *io) {
    io_set_mouse_capture(io, false);
    ms->captured = false;
    ms->first_mouse = true;
}

static void mouse_state_process_movement(MouseState *ms, IOContext *io, Camera *camera,
                                         uint32_t window_width, uint32_t window_height,
                                         float mouse_x, float mouse_y) {
    if (!ms->captured) return;
    
    int center_x = (int)window_width / 2;
    int center_y = (int)window_height / 2;
    
    if (ms->first_mouse) {
        ms->last_x = (float)center_x;
        ms->last_y = (float)center_y;
        ms->first_mouse = false;
    }
    
    float x_offset = mouse_x - ms->last_x;
    float y_offset = ms->last_y - mouse_y;
    
    camera_process_mouse(camera, x_offset, y_offset);
    
    io_warp_mouse(io, center_x, center_y);
    io_flush(io);
    
    ms->last_x = (float)center_x;
    ms->last_y = (float)center_y;
}

/* -------------------------------------------------------------------------- */
/* Event Handlers                                                             */
/* -------------------------------------------------------------------------- */

static void handle_key_down(Player *player, MouseState *ms, IOContext *io,
                           bool *keys, uint32_t key, bool *running) {
    if (key == IO_KEY_ESCAPE) {
        if (ms->captured) {
            mouse_state_release(ms, io);
        } else {
            *running = false;
        }
        return;
    }
    
    if (key == 'E' || key == 'e') {
        if (keys[key]) return;
        
        player->inventory_open = !player->inventory_open;
        
        if (!player->inventory_open) {
            player_return_crafting_to_inventory(player);
            player_inventory_cancel_held(player);
            if (!ms->captured) mouse_state_capture(ms, io);
        } else {
            if (ms->captured) mouse_state_release(ms, io);
            player->inventory_mouse_valid = false;
        }
        return;
    }
    
    if (!player->inventory_open && key >= '1' && key <= '9') {
        player->selected_slot = (uint8_t)(key - '1');
    }
    
    if (key < 256) keys[key] = true;
}

static void handle_mouse_button_game(MouseState *ms, IOContext *io,
                                    bool *left_click, bool *right_click,
                                    uint8_t button) {
    if (button == IO_MOUSE_BUTTON_LEFT) {
        if (!ms->captured) {
            mouse_state_capture(ms, io);
        } else {
            *left_click = true;
        }
    } else if (button == IO_MOUSE_BUTTON_RIGHT && ms->captured) {
        *right_click = true;
    }
}

static void process_events(IOContext *io, Renderer *renderer, Player *player, MouseState *ms, bool *keys,
                          uint32_t *window_width, uint32_t *window_height, Camera *camera,
                          bool *running, bool *left_click, bool *right_click) {
    *left_click = false;
    *right_click = false;
    
    bool mouse_moved = false;
    float mouse_x = 0.0f, mouse_y = 0.0f;
    
    IOEvent event;
    while (io_poll_event(io, &event)) {
        switch (event.type) {
        case IO_EVENT_QUIT:
            *running = false;
            break;

        case IO_EVENT_RESIZE:
            if (event.data.resize.width > 0 && event.data.resize.height > 0) {
                *window_width = event.data.resize.width;
                *window_height = event.data.resize.height;
                renderer_resize(renderer, *window_width, *window_height);
                if (ms->captured) {
                    ms->first_mouse = true;
                }
            }
            break;
            
        case IO_EVENT_KEY_DOWN:
            handle_key_down(player, ms, io, keys, event.data.key.key, running);
            break;
            
        case IO_EVENT_KEY_UP:
            if (event.data.key.key < 256) {
                keys[event.data.key.key] = false;
            }
            break;
            
        case IO_EVENT_MOUSE_MOVE:
            mouse_x = (float)event.data.mouse_move.x;
            mouse_y = (float)event.data.mouse_move.y;
            mouse_moved = true;
            player_update_inventory_mouse_position(player, *window_width, *window_height,
                                                 event.data.mouse_move.x, event.data.mouse_move.y);
            break;
            
        case IO_EVENT_MOUSE_BUTTON:
            if (player->inventory_open) {
                player_handle_mouse_button_inventory(player, *window_width, *window_height,
                                                    event.data.mouse_button.x,
                                                    event.data.mouse_button.y,
                                                    event.data.mouse_button.button);
            } else {
                handle_mouse_button_game(ms, io, left_click, right_click,
                                       event.data.mouse_button.button);
            }
            break;
            
        default:
            break;
        }
    }
    
    if (mouse_moved) {
        mouse_state_process_movement(ms, io, camera, *window_width, *window_height,
                                    mouse_x, mouse_y);
    }
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    IOContext *io = io_create("Voxel Engine");
    void *display = io_get_display(io);
    unsigned long window = io_get_window(io);
    uint32_t window_width, window_height;
    io_get_window_size(io, &window_width, &window_height);
    
    Renderer *renderer = renderer_create(display, window, window_width, window_height);
    
    WorldSave save;
    world_save_init(&save, WORLD_SAVE_FILE);
    world_save_load(&save);
    
    World world;
    world_init(&world, &save);
    world_update_chunks(&world, world.spawn_position);
    if (!world.spawn_set) {
        world.spawn_position = vec3(0.0f, 4.5f, 0.0f);
    }

    Entity zombie = entity_create_zombie(vec3_add(world.spawn_position, vec3(1.0f, 0.0f, 1.0f)));
    world_add_entity(&world, zombie);
    
    Player player;
    player_init(&player, world.spawn_position);

    /* Load saved player state if available */
    if (!world_save_load_player(&save, &player)) {
        /* No saved player data, use spawn position */
        player.position = world.spawn_position;
    }
    
    Camera camera;
    camera_init(&camera);
    camera_follow_player(&camera, &player);
    
    world_update_chunks(&world, player.position);
    
    bool keys[256] = {0};
    MouseState mouse_state;
    mouse_state_init(&mouse_state);
    
    TimeState time_state;
    time_state_init(&time_state);
    
    bool running = true;
    while (running) {
        world_update_chunks(&world, player.position);
        
        bool left_click, right_click;
        process_events(io, renderer, &player, &mouse_state, keys,
                  &window_width, &window_height, &camera, &running,
                      &left_click, &right_click);
        
        float delta_time = time_state_delta(&time_state);
        
        Vec3 move_delta;
        bool wants_jump;
        bool movement_enabled = mouse_state.captured && !player.inventory_open;
        player_compute_movement(&player, &camera, keys, movement_enabled,
                              delta_time, &move_delta, &wants_jump);
        
        bool respawned = player_apply_physics(&player, &world, delta_time, move_delta, wants_jump);
        camera_follow_player(&camera, &player);

        world_update_entities(&world, delta_time);

        if (respawned) {
            camera_reset_view(&camera);
        }
        
        RayHit ray_hit = raycast_blocks(&world, camera.position, camera.front, 6.0f);
        bool interaction_enabled = mouse_state.captured && !player.inventory_open;
        player_handle_block_interaction(&player, &world, ray_hit,
                                       left_click, right_click, interaction_enabled);
        
        if (time_state_should_autosave(&time_state)) {
            world_save_store_player(&save, &player);
            world_save_flush(&save);
        }
        
        renderer_draw_frame(renderer, &world, &player, &camera,
                          ray_hit.hit, ray_hit.cell);
    }
    
    world_save_store_player(&save, &player);
    world_destroy(&world);
    world_save_destroy(&save);
    renderer_destroy(renderer);
    io_destroy(io);
    
    return 0;
}