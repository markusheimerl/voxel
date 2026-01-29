#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "camera.h"
#include "math.h"
#include "world.h"
#include "renderer.h"

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

#define INVENTORY_COLS 9
#define INVENTORY_ROWS 3
#define INVENTORY_SIZE (INVENTORY_COLS * INVENTORY_ROWS)
#define CRAFTING_COLS 3
#define CRAFTING_ROWS 3
#define CRAFTING_SIZE (CRAFTING_COLS * CRAFTING_ROWS)

typedef struct Player {
    Vec3 position;    /* feet center */
    float velocity_y;
    bool on_ground;
    bool inventory_open;
    uint8_t selected_slot;
    uint8_t inventory[INVENTORY_SIZE];
    uint8_t inventory_counts[INVENTORY_SIZE];
    uint8_t crafting_grid[CRAFTING_SIZE];
    uint8_t crafting_grid_counts[CRAFTING_SIZE];
    uint8_t inventory_held_type;
    uint8_t inventory_held_count;
    uint8_t inventory_held_origin_slot;
    bool inventory_held_origin_valid;
    bool inventory_held_from_crafting;
    float inventory_mouse_ndc_x;
    float inventory_mouse_ndc_y;
    bool inventory_mouse_valid;
} Player;

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool hit;
    IVec3 cell;
    IVec3 normal;
    uint8_t type;
} RayHit;

void player_init(Player *player, Vec3 spawn_position);
float player_eye_height(void);
void player_compute_movement(const Player *player,
                             const Camera *camera,
                             const bool *keys,
                             bool movement_enabled,
                             float delta_time,
                             Vec3 *out_move_delta,
                             bool *out_wants_jump);
void player_apply_physics(Player *player,
                          World *world,
                          float delta_time,
                          Vec3 move_delta,
                          bool wants_jump);
void player_handle_block_interaction(Player *player,
                                     World *world,
                                     RayHit ray_hit,
                                     bool left_click,
                                     bool right_click,
                                     bool interaction_enabled);

void player_inventory_add(Player *player, uint8_t type);
void player_inventory_handle_click(Player *player, int slot);
void player_inventory_handle_right_click(Player *player, int slot);
void player_inventory_cancel_held(Player *player);

typedef struct {
    bool valid;
    uint8_t result_type;
    uint8_t result_count;
} CraftingResult;

CraftingResult player_get_crafting_result(const Player *player);
int player_crafting_result_slot_from_mouse(float aspect,
                                           float mouse_x,
                                           float mouse_y,
                                           float window_w,
                                           float window_h);
void player_crafting_result_handle_click(Player *player);

RayHit raycast_blocks(World *world, Vec3 origin, Vec3 direction, float max_distance);

/* -------------------------------------------------------------------------- */
/* Player Collision Helpers                                                   */
/* -------------------------------------------------------------------------- */

void resolve_collision_axis(World *world, Vec3 *position, float delta, int axis);
void resolve_collision_y(World *world, Vec3 *position, float *velocity_y, bool *on_ground);
bool block_overlaps_player(const Player *player, IVec3 cell);

/* -------------------------------------------------------------------------- */
/* Inventory Helpers                                                          */
/* -------------------------------------------------------------------------- */

void player_inventory_add(Player *player, uint8_t type);

void player_inventory_grid_vertices(float aspect,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count,
                                    float *out_h_step,
                                    float *out_v_step);

void player_crafting_grid_vertices(float aspect,
                                   Vertex *out_vertices,
                                   uint32_t max_vertices,
                                   uint32_t *out_count);

void player_crafting_arrow_vertices(float aspect,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count);

void player_crafting_result_slot_vertices(float aspect,
                                          Vertex *out_vertices,
                                          uint32_t max_vertices,
                                          uint32_t *out_count);

void player_inventory_background_vertices(float aspect,
                                          Vertex *out_vertices,
                                          uint32_t max_vertices,
                                          uint32_t *out_count);

void player_inventory_icon_vertices(float h_step,
                                    float v_step,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count);

int player_inventory_slot_from_mouse(float aspect,
                                     float mouse_x,
                                     float mouse_y,
                                     float window_w,
                                     float window_h);

void player_inventory_selection_vertices(int slot,
                                         float aspect,
                                         Vertex *out_vertices,
                                         uint32_t max_vertices,
                                         uint32_t *out_count);

uint32_t player_inventory_icon_instances(const Player *player,
                                         float aspect,
                                         InstanceData *out_instances,
                                         uint32_t max_instances);

uint32_t player_inventory_count_vertices(const Player *player,
                                         float aspect,
                                         Vertex *out_vertices,
                                         uint32_t max_vertices);

int player_crafting_slot_from_mouse(float aspect,
                                    float mouse_x,
                                    float mouse_y,
                                    float window_w,
                                    float window_h);
void player_crafting_handle_click(Player *player, int slot);
void player_crafting_handle_right_click(Player *player, int slot);
void player_return_crafting_to_inventory(Player *player);

#endif /* PLAYER_H */