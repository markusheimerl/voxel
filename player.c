#include "player.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

static const float PLAYER_GRAVITY = 17.0f;
static const float PLAYER_JUMP_HEIGHT = 1.2f;
static const float PLAYER_EYE_HEIGHT = 1.6f;
static const float PLAYER_HALF_WIDTH = 0.4f;
static const float PLAYER_HEIGHT = 1.8f;
static const float PLAYER_FALL_DAMAGE_SAFE_DISTANCE = 3.0f;
static const float PLAYER_FALL_DAMAGE_PER_UNIT = 1.0f;

/* -------------------------------------------------------------------------- */
/* Inventory Layout                                                           */
/* -------------------------------------------------------------------------- */

typedef struct {
    float inv_left, inv_right, inv_bottom, inv_top;
    float craft_left, craft_right, craft_bottom, craft_top;
    float arrow_left, arrow_right, arrow_bottom, arrow_top;
    float result_left, result_right, result_bottom, result_top;
    float cell_w, cell_h;
} InventoryLayout;

static InventoryLayout calculate_layout(float aspect) {
    InventoryLayout layout;
    
    const float inv_base_half_height = 0.13f;
    const float inv_aspect = (float)INVENTORY_COLS / (float)INVENTORY_ROWS;
    const float inv_half_width = inv_base_half_height * aspect * inv_aspect;
    const float inv_half_height = inv_base_half_height;
    
    layout.cell_w = (inv_half_width * 2.0f) / (float)INVENTORY_COLS;
    layout.cell_h = (inv_half_height * 2.0f) / (float)INVENTORY_ROWS;
    
    const float gap = layout.cell_h * 0.7f;
    const float craft_width = layout.cell_w * (float)CRAFTING_COLS;
    const float craft_height = layout.cell_h * (float)CRAFTING_ROWS;
    const float total_height = craft_height + gap + inv_half_height * 2.0f;
    const float top_edge = total_height * 0.5f;
    
    layout.inv_left = -inv_half_width;
    layout.inv_right = inv_half_width;
    layout.inv_bottom = top_edge - total_height;
    layout.inv_top = layout.inv_bottom + inv_half_height * 2.0f;
    
    layout.craft_left = layout.inv_left;
    layout.craft_right = layout.craft_left + craft_width;
    layout.craft_bottom = layout.inv_top + gap;
    layout.craft_top = layout.craft_bottom + craft_height;
    
    const float arrow_width = layout.cell_w * 1.2f;
    const float arrow_height = layout.cell_h;
    const float craft_center_y = (layout.craft_top + layout.craft_bottom) * 0.5f;
    
    layout.arrow_left = layout.craft_right + layout.cell_w * 0.4f;
    layout.arrow_right = layout.arrow_left + arrow_width;
    layout.arrow_bottom = craft_center_y - arrow_height * 0.5f;
    layout.arrow_top = craft_center_y + arrow_height * 0.5f;
    
    layout.result_left = layout.arrow_right + layout.cell_w * 0.4f;
    layout.result_right = layout.result_left + layout.cell_w;
    layout.result_bottom = craft_center_y - layout.cell_h * 0.5f;
    layout.result_top = craft_center_y + layout.cell_h * 0.5f;
    
    return layout;
}

/* -------------------------------------------------------------------------- */
/* Helper Functions                                                           */
/* -------------------------------------------------------------------------- */

static inline float player_jump_velocity(void) {
    return sqrtf(2.0f * PLAYER_GRAVITY * PLAYER_JUMP_HEIGHT);
}

static inline bool is_key_pressed(const bool *keys, unsigned char c) {
    return keys && keys[c];
}

static inline bool is_key_pressed_ci(const bool *keys, char c) {
    return is_key_pressed(keys, (unsigned char)c) ||
           is_key_pressed(keys, (unsigned char)toupper((unsigned char)c));
}

static void player_compute_aabb(Vec3 pos, AABB *aabb) {
    aabb->min = vec3(pos.x - PLAYER_HALF_WIDTH, pos.y, pos.z - PLAYER_HALF_WIDTH);
    aabb->max = vec3(pos.x + PLAYER_HALF_WIDTH, pos.y + PLAYER_HEIGHT, pos.z + PLAYER_HALF_WIDTH);
}

static void append_line(Vertex *verts, uint32_t *count, uint32_t max,
                        float x0, float y0, float x1, float y1) {
    if (*count + 2 > max) return;
    verts[(*count)++] = (Vertex){{x0, y0, 0.0f}, {0.0f, 0.0f}};
    verts[(*count)++] = (Vertex){{x1, y1, 0.0f}, {0.0f, 0.0f}};
}

static void append_digit(Vertex *verts, uint32_t *count, uint32_t max,
                         int digit, float x, float y, float w, float h) {
    if (digit < 0 || digit > 9) return;
    
    const float x0 = x, x1 = x + w, y0 = y, y1 = y + h, ym = y + h * 0.5f;
    
    static const bool segments[10][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1}, {0,1,1,0,0,1,1},
        {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0}, {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
    };
    
    const bool *seg = segments[digit];
    if (seg[0]) append_line(verts, count, max, x0, y1, x1, y1);
    if (seg[1]) append_line(verts, count, max, x1, y1, x1, ym);
    if (seg[2]) append_line(verts, count, max, x1, ym, x1, y0);
    if (seg[3]) append_line(verts, count, max, x0, y0, x1, y0);
    if (seg[4]) append_line(verts, count, max, x0, ym, x0, y0);
    if (seg[5]) append_line(verts, count, max, x0, y1, x0, ym);
    if (seg[6]) append_line(verts, count, max, x0, ym, x1, ym);
}

static void draw_number(Vertex *verts, uint32_t *count, uint32_t max,
                        uint8_t number, float cell_left, float cell_top,
                        float cell_w, float cell_h) {
    const float digit_w = cell_w * 0.16f;
    const float digit_h = cell_h * 0.35f;
    const float gap = digit_w * 0.25f;
    
    int digits[3], digit_count = 0;
    if (number >= 100) digits[digit_count++] = (number / 100) % 10;
    if (number >= 10) digits[digit_count++] = (number / 10) % 10;
    digits[digit_count++] = number % 10;
    
    const float total_w = (float)digit_count * digit_w + (float)(digit_count - 1) * gap;
    const float start_x = cell_left + cell_w - total_w - cell_w * 0.08f;
    const float start_y = cell_top - cell_h + cell_h * 0.12f;
    
    for (int i = 0; i < digit_count; ++i) {
        append_digit(verts, count, max, digits[i],
                    start_x + i * (digit_w + gap), start_y, digit_w, digit_h);
    }
}

/* -------------------------------------------------------------------------- */
/* Stack Management                                                           */
/* -------------------------------------------------------------------------- */

static bool try_place_stack(Player *player, uint8_t type, uint8_t count, int skip_slot) {
    if (count == 0) return true;
    
    for (int i = 0; i < INVENTORY_SIZE && count > 0; ++i) {
        if (i == skip_slot || player->inventory_counts[i] == 0) continue;
        if (player->inventory[i] != type) continue;
        
        uint16_t total = (uint16_t)player->inventory_counts[i] + count;
        if (total <= UINT8_MAX) {
            player->inventory_counts[i] = (uint8_t)total;
            return true;
        }
        uint8_t space = UINT8_MAX - player->inventory_counts[i];
        player->inventory_counts[i] = UINT8_MAX;
        count -= space;
    }
    
    for (int i = 0; i < INVENTORY_SIZE && count > 0; ++i) {
        if (i == skip_slot || player->inventory_counts[i] != 0) continue;
        player->inventory[i] = type;
        player->inventory_counts[i] = count;
        count = 0;
    }
    
    return count == 0;
}

/* -------------------------------------------------------------------------- */
/* Slot Interaction (unified for inventory and crafting)                     */
/* -------------------------------------------------------------------------- */

static void handle_slot_click(uint8_t *slot_type, uint8_t *slot_count,
                              uint8_t *held_type, uint8_t *held_count,
                              uint8_t *origin_slot, bool *origin_valid, bool *from_crafting,
                              int slot_index, bool is_crafting_grid) {
    if (*held_count == 0) {
        if (*slot_count == 0) return;
        *held_type = *slot_type;
        *held_count = *slot_count;
        *slot_type = 0;
        *slot_count = 0;
        *origin_slot = (uint8_t)slot_index;
        *origin_valid = true;
        *from_crafting = is_crafting_grid;
    } else if (*slot_count == 0) {
        *slot_type = *held_type;
        *slot_count = *held_count;
        *held_type = 0;
        *held_count = 0;
        *origin_valid = false;
        *from_crafting = false;
    } else if (*slot_type == *held_type) {
        uint16_t total = (uint16_t)*slot_count + *held_count;
        if (total <= UINT8_MAX) {
            *slot_count = (uint8_t)total;
            *held_type = 0;
            *held_count = 0;
            *origin_valid = false;
            *from_crafting = false;
        } else {
            *slot_count = UINT8_MAX;
            *held_count = (uint8_t)(total - UINT8_MAX);
        }
    } else {
        uint8_t temp_type = *slot_type, temp_count = *slot_count;
        *slot_type = *held_type;
        *slot_count = *held_count;
        *held_type = temp_type;
        *held_count = temp_count;
    }
}

static void handle_slot_right_click(uint8_t *slot_type, uint8_t *slot_count,
                                    uint8_t *held_type, uint8_t *held_count,
                                    uint8_t *origin_slot, bool *origin_valid, bool *from_crafting,
                                    int slot_index, bool is_crafting_grid) {
    if (*held_count != 0) {
        if (*slot_count == 0) {
            *slot_type = *held_type;
            *slot_count = 1;
            (*held_count)--;
        } else if (*slot_type == *held_type && *slot_count < UINT8_MAX) {
            (*slot_count)++;
            (*held_count)--;
        } else {
            return;
        }
        
        if (*held_count == 0) {
            *held_type = 0;
            *origin_valid = false;
            *from_crafting = false;
        }
    } else {
        if (*slot_count == 0) return;
        
        uint8_t take = (*slot_count + 1) / 2;
        uint8_t remain = *slot_count - take;
        
        *held_type = *slot_type;
        *held_count = take;
        *slot_count = remain;
        *origin_slot = (uint8_t)slot_index;
        *origin_valid = true;
        *from_crafting = is_crafting_grid;
        
        if (remain == 0) *slot_type = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Mouse to Slot Conversion                                                   */
/* -------------------------------------------------------------------------- */

static int mouse_to_grid_slot(float mouse_x, float mouse_y, float window_w, float window_h,
                              float left, float right, float bottom, float top,
                              int cols, int rows) {
    if (window_w <= 0.0f || window_h <= 0.0f) return -1;
    
    float ndc_x = (mouse_x / window_w) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (mouse_y / window_h) * 2.0f;
    
    if (ndc_x < left || ndc_x > right || ndc_y < bottom || ndc_y > top) return -1;
    
    float h_step = (right - left) / (float)cols;
    float v_step = (top - bottom) / (float)rows;
    
    int col = (int)((ndc_x - left) / h_step);
    int row = (int)((top - ndc_y) / v_step);
    
    if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
    
    return row * cols + col;
}

/* -------------------------------------------------------------------------- */
/* Core Player Functions                                                      */
/* -------------------------------------------------------------------------- */

void player_init(Player *player, Vec3 spawn_position) {
    if (!player) return;
    memset(player, 0, sizeof(*player));
    player->position = spawn_position;
    player->health = 10;
    player->fall_highest_y = spawn_position.y;
}

float player_eye_height(void) {
    return PLAYER_EYE_HEIGHT;
}

void player_compute_movement(const Player *player, const Camera *camera, const bool *keys,
                             bool movement_enabled, float delta_time,
                             Vec3 *out_move_delta, bool *out_wants_jump) {
    *out_move_delta = vec3(0, 0, 0);
    *out_wants_jump = false;
    
    if (!player || !camera || !movement_enabled) return;
    
    Vec3 forward = vec3_normalize(vec3(camera->front.x, 0, camera->front.z));
    Vec3 right = vec3_normalize(vec3(camera->right.x, 0, camera->right.z));
    Vec3 movement = vec3(0, 0, 0);
    
    if (is_key_pressed_ci(keys, 'w')) movement = vec3_add(movement, forward);
    if (is_key_pressed_ci(keys, 's')) movement = vec3_sub(movement, forward);
    if (is_key_pressed_ci(keys, 'a')) movement = vec3_sub(movement, right);
    if (is_key_pressed_ci(keys, 'd')) movement = vec3_add(movement, right);
    
    if (vec3_length(movement) > 0.0f) movement = vec3_normalize(movement);
    
    *out_move_delta = vec3_scale(movement, camera->movement_speed * delta_time);
    *out_wants_jump = is_key_pressed(keys, ' ');
}

void player_apply_physics(Player *player, World *world, float delta_time,
                          Vec3 move_delta, bool wants_jump) {
    if (!player || !world) return;

    bool was_on_ground = player->on_ground;
    
    if (wants_jump && player->on_ground) {
        player->velocity_y = player_jump_velocity();
        player->on_ground = false;
    }
    
    if (!player->on_ground) {
        player->velocity_y -= PLAYER_GRAVITY * delta_time;
    } else {
        player->velocity_y = 0.0f;
    }
    
    player->position.x += move_delta.x;
    resolve_collision_axis(world, &player->position, move_delta.x, 0);
    
    player->position.z += move_delta.z;
    resolve_collision_axis(world, &player->position, move_delta.z, 2);
    
    player->position.y += player->velocity_y * delta_time;
    resolve_collision_y(world, &player->position, &player->velocity_y, &player->on_ground);

    if (was_on_ground && !player->on_ground) {
        player->fall_highest_y = player->position.y;
    }

    if (!player->on_ground && player->position.y > player->fall_highest_y) {
        player->fall_highest_y = player->position.y;
    }

    if (!was_on_ground && player->on_ground) {
        float fall_distance = player->fall_highest_y - player->position.y;
        if (fall_distance > PLAYER_FALL_DAMAGE_SAFE_DISTANCE) {
            float excess = fall_distance - PLAYER_FALL_DAMAGE_SAFE_DISTANCE;
            int damage = (int)floorf(excess * PLAYER_FALL_DAMAGE_PER_UNIT);
            if (damage > 0) {
                if (damage >= player->health) {
                    player->health = 0;
                } else {
                    player->health -= (uint8_t)damage;
                }
            }
        }
        player->fall_highest_y = player->position.y;
    }
}

void player_handle_block_interaction(Player *player, World *world, RayHit ray_hit,
                                     bool left_click, bool right_click, bool interaction_enabled) {
    if (!player || !world || !interaction_enabled || !ray_hit.hit) return;
    
    if (left_click) {
        world_remove_block(world, ray_hit.cell);
        player_inventory_add(player, ray_hit.type);
    }
    
    if (right_click) {
        if (ray_hit.normal.x == 0 && ray_hit.normal.y == 0 && ray_hit.normal.z == 0) return;
        
        IVec3 place = ivec3_add(ray_hit.cell, ray_hit.normal);
        if (world_block_exists(world, place) || block_overlaps_player(player, place)) return;
        
        uint8_t slot = player->selected_slot;
        if (slot >= INVENTORY_SIZE || player->inventory_counts[slot] == 0) return;
        
        uint8_t place_type = player->inventory[slot];
        if (!item_is_placeable(place_type)) return;
        
        world_add_block(world, place, place_type);
        player->inventory_counts[slot]--;
        if (player->inventory_counts[slot] == 0) player->inventory[slot] = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

RayHit raycast_blocks(World *world, Vec3 origin, Vec3 direction, float max_distance) {
    RayHit result = {0};
    Vec3 dir = vec3_normalize(direction);
    IVec3 previous_cell = world_to_cell(origin);
    
    const float step = 0.05f;
    for (float t = 0.0f; t <= max_distance; t += step) {
        Vec3 point = vec3_add(origin, vec3_scale(dir, t));
        IVec3 cell = world_to_cell(point);
        
        if (!ivec3_equal(cell, previous_cell)) {
            uint8_t type;
            if (world_get_block_type(world, cell, &type)) {
                result.hit = true;
                result.cell = cell;
                result.normal = (IVec3){
                    sign_int(previous_cell.x - cell.x),
                    sign_int(previous_cell.y - cell.y),
                    sign_int(previous_cell.z - cell.z)
                };
                result.type = type;
                break;
            }
            previous_cell = cell;
        }
    }
    
    return result;
}

/* -------------------------------------------------------------------------- */
/* Collision                                                                  */
/* -------------------------------------------------------------------------- */

void resolve_collision_axis(World *world, Vec3 *position, float delta, int axis) {
    if (delta == 0.0f) return;
    
    AABB player_box;
    player_compute_aabb(*position, &player_box);
    
    int min_x = (int)floorf(player_box.min.x - 0.5f);
    int max_x = (int)floorf(player_box.max.x + 0.5f);
    int min_y = (int)floorf(player_box.min.y - 0.5f);
    int max_y = (int)floorf(player_box.max.y + 0.5f);
    int min_z = (int)floorf(player_box.min.z - 0.5f);
    int max_z = (int)floorf(player_box.max.z + 0.5f);
    
    for (int x = min_x; x <= max_x; ++x) {
        for (int y = min_y; y <= max_y; ++y) {
            if (!world_y_in_bounds(y)) continue;
            for (int z = min_z; z <= max_z; ++z) {
                uint8_t type;
                if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                if (type == BLOCK_WATER) continue;
                
                player_compute_aabb(*position, &player_box);
                AABB block_box = cell_aabb((IVec3){x, y, z});
                
                if (!((player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
                      (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
                      (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z))) {
                    continue;
                }
                
                if (axis == 0) {
                    if (delta > 0.0f) position->x = block_box.min.x - PLAYER_HALF_WIDTH - 0.001f;
                    else position->x = block_box.max.x + PLAYER_HALF_WIDTH + 0.001f;
                } else if (axis == 2) {
                    if (delta > 0.0f) position->z = block_box.min.z - PLAYER_HALF_WIDTH - 0.001f;
                    else position->z = block_box.max.z + PLAYER_HALF_WIDTH + 0.001f;
                }
            }
        }
    }
}

void resolve_collision_y(World *world, Vec3 *position, float *velocity_y, bool *on_ground) {
    AABB player_box;
    player_compute_aabb(*position, &player_box);
    *on_ground = false;
    
    int min_x = (int)floorf(player_box.min.x - 0.5f);
    int max_x = (int)floorf(player_box.max.x + 0.5f);
    int min_y = (int)floorf(player_box.min.y - 0.5f);
    int max_y = (int)floorf(player_box.max.y + 0.5f);
    int min_z = (int)floorf(player_box.min.z - 0.5f);
    int max_z = (int)floorf(player_box.max.z + 0.5f);
    
    for (int x = min_x; x <= max_x; ++x) {
        for (int y = min_y; y <= max_y; ++y) {
            if (!world_y_in_bounds(y)) continue;
            for (int z = min_z; z <= max_z; ++z) {
                uint8_t type;
                if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                if (type == BLOCK_WATER) continue;
                
                player_compute_aabb(*position, &player_box);
                AABB block_box = cell_aabb((IVec3){x, y, z});
                
                if (!((player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
                      (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
                      (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z))) {
                    continue;
                }
                
                if (*velocity_y < 0.0f) {
                    position->y = block_box.max.y;
                    *velocity_y = 0.0f;
                    *on_ground = true;
                } else if (*velocity_y > 0.0f) {
                    position->y = block_box.min.y - PLAYER_HEIGHT - 0.001f;
                    *velocity_y = 0.0f;
                }
            }
        }
    }
}

bool block_overlaps_player(const Player *player, IVec3 cell) {
    AABB player_box;
    player_compute_aabb(player->position, &player_box);
    
    AABB block_box = cell_aabb(cell);
    return (player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
           (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
           (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z);
}

/* -------------------------------------------------------------------------- */
/* Inventory Management                                                       */
/* -------------------------------------------------------------------------- */

void player_inventory_add(Player *player, uint8_t type) {
    for (int i = 0; i < INVENTORY_SIZE; ++i) {
        if (player->inventory_counts[i] > 0 && player->inventory[i] == type) {
            if (player->inventory_counts[i] < UINT8_MAX) {
                player->inventory_counts[i]++;
            }
            return;
        }
    }
    
    for (int i = 0; i < INVENTORY_SIZE; ++i) {
        if (player->inventory_counts[i] == 0) {
            player->inventory[i] = type;
            player->inventory_counts[i] = 1;
            return;
        }
    }
}

void player_inventory_handle_click(Player *player, int slot) {
    if (!player || slot < 0 || slot >= INVENTORY_SIZE) return;
    handle_slot_click(&player->inventory[slot], &player->inventory_counts[slot],
                     &player->inventory_held_type, &player->inventory_held_count,
                     &player->inventory_held_origin_slot, &player->inventory_held_origin_valid,
                     &player->inventory_held_from_crafting, slot, false);
}

void player_inventory_handle_right_click(Player *player, int slot) {
    if (!player || slot < 0 || slot >= INVENTORY_SIZE) return;
    handle_slot_right_click(&player->inventory[slot], &player->inventory_counts[slot],
                           &player->inventory_held_type, &player->inventory_held_count,
                           &player->inventory_held_origin_slot, &player->inventory_held_origin_valid,
                           &player->inventory_held_from_crafting, slot, false);
}

void player_inventory_cancel_held(Player *player) {
    if (!player || player->inventory_held_count == 0 || !player->inventory_held_origin_valid) return;
    
    int origin = player->inventory_held_origin_slot;
    
    if (player->inventory_held_from_crafting) {
        if (origin < 0 || origin >= CRAFTING_SIZE) return;
        
        if (player->crafting_grid_counts[origin] == 0) {
            player->crafting_grid[origin] = player->inventory_held_type;
            player->crafting_grid_counts[origin] = player->inventory_held_count;
        } else if (player->crafting_grid[origin] == player->inventory_held_type) {
            uint16_t total = (uint16_t)player->crafting_grid_counts[origin] + player->inventory_held_count;
            if (total <= UINT8_MAX) {
                player->crafting_grid_counts[origin] = (uint8_t)total;
            } else {
                return;
            }
        } else {
            return;
        }
        
        player->inventory_held_type = 0;
        player->inventory_held_count = 0;
        player->inventory_held_origin_valid = false;
        player->inventory_held_from_crafting = false;
        return;
    }
    
    if (origin < 0 || origin >= INVENTORY_SIZE) return;
    
    uint8_t held_type = player->inventory_held_type;
    uint8_t held_count = player->inventory_held_count;
    
    if (player->inventory_counts[origin] == 0) {
        player->inventory[origin] = held_type;
        player->inventory_counts[origin] = held_count;
    } else if (player->inventory[origin] == held_type) {
        uint16_t total = (uint16_t)player->inventory_counts[origin] + held_count;
        if (total <= UINT8_MAX) {
            player->inventory_counts[origin] = (uint8_t)total;
        } else {
            player->inventory_counts[origin] = UINT8_MAX;
            uint8_t remaining = (uint8_t)(total - UINT8_MAX);
            if (!try_place_stack(player, held_type, remaining, origin)) {
                player->inventory_held_count = remaining;
                return;
            }
        }
    } else {
        uint8_t displaced_type = player->inventory[origin];
        uint8_t displaced_count = player->inventory_counts[origin];
        if (!try_place_stack(player, displaced_type, displaced_count, origin)) {
            return;
        }
        player->inventory[origin] = held_type;
        player->inventory_counts[origin] = held_count;
    }
    
    player->inventory_held_type = 0;
    player->inventory_held_count = 0;
    player->inventory_held_origin_valid = false;
}

/* -------------------------------------------------------------------------- */
/* Crafting Management                                                        */
/* -------------------------------------------------------------------------- */

CraftingResult player_get_crafting_result(const Player *player) {
    CraftingResult result = {false, 0, 0};
    if (!player) return result;
    
    int wood_count = 0, plank_count = 0, empty_count = 0;
    
    for (int i = 0; i < CRAFTING_SIZE; ++i) {
        if (player->crafting_grid_counts[i] > 0) {
            if (player->crafting_grid[i] == BLOCK_WOOD) wood_count++;
            else if (player->crafting_grid[i] == BLOCK_PLANKS) plank_count++;
        } else {
            empty_count++;
        }
    }
    
    if (wood_count == 1 && empty_count == 8) {
        result.valid = true;
        result.result_type = BLOCK_PLANKS;
        result.result_count = 4;
        return result;
    }
    
    if (plank_count == 2 && empty_count == 7) {
        static const int vertical_pairs[][2] = {
            {0, 3}, {3, 6}, {1, 4}, {4, 7}, {2, 5}, {5, 8}
        };
        
        for (int i = 0; i < 6; ++i) {
            int top = vertical_pairs[i][0];
            int bottom = vertical_pairs[i][1];
            
            if (player->crafting_grid_counts[top] > 0 &&
                player->crafting_grid[top] == BLOCK_PLANKS &&
                player->crafting_grid_counts[bottom] > 0 &&
                player->crafting_grid[bottom] == BLOCK_PLANKS) {
                
                result.valid = true;
                result.result_type = ITEM_STICK;
                result.result_count = 4;
                return result;
            }
        }
    }
    
    return result;
}

void player_crafting_handle_click(Player *player, int slot) {
    if (!player || slot < 0 || slot >= CRAFTING_SIZE) return;
    handle_slot_click(&player->crafting_grid[slot], &player->crafting_grid_counts[slot],
                     &player->inventory_held_type, &player->inventory_held_count,
                     &player->inventory_held_origin_slot, &player->inventory_held_origin_valid,
                     &player->inventory_held_from_crafting, slot, true);
}

void player_crafting_handle_right_click(Player *player, int slot) {
    if (!player || slot < 0 || slot >= CRAFTING_SIZE) return;
    handle_slot_right_click(&player->crafting_grid[slot], &player->crafting_grid_counts[slot],
                           &player->inventory_held_type, &player->inventory_held_count,
                           &player->inventory_held_origin_slot, &player->inventory_held_origin_valid,
                           &player->inventory_held_from_crafting, slot, true);
}

void player_crafting_result_handle_click(Player *player) {
    if (!player) return;
    
    CraftingResult craft_result = player_get_crafting_result(player);
    if (!craft_result.valid) return;
    
    if (player->inventory_held_count > 0) {
        if (player->inventory_held_type != craft_result.result_type) return;
        uint16_t total = (uint16_t)player->inventory_held_count + craft_result.result_count;
        if (total > UINT8_MAX) return;
        player->inventory_held_count = (uint8_t)total;
    } else {
        player->inventory_held_type = craft_result.result_type;
        player->inventory_held_count = craft_result.result_count;
        player->inventory_held_origin_valid = false;
    }
    
    if (craft_result.result_type == BLOCK_PLANKS) {
        for (int i = 0; i < CRAFTING_SIZE; ++i) {
            if (player->crafting_grid_counts[i] > 0 && player->crafting_grid[i] == BLOCK_WOOD) {
                player->crafting_grid_counts[i]--;
                if (player->crafting_grid_counts[i] == 0) player->crafting_grid[i] = 0;
                break;
            }
        }
    } else if (craft_result.result_type == ITEM_STICK) {
        static const int vertical_pairs[][2] = {
            {0, 3}, {3, 6}, {1, 4}, {4, 7}, {2, 5}, {5, 8}
        };
        
        for (int i = 0; i < 6; ++i) {
            int top = vertical_pairs[i][0];
            int bottom = vertical_pairs[i][1];
            
            if (player->crafting_grid_counts[top] > 0 &&
                player->crafting_grid[top] == BLOCK_PLANKS &&
                player->crafting_grid_counts[bottom] > 0 &&
                player->crafting_grid[bottom] == BLOCK_PLANKS) {
                
                player->crafting_grid_counts[top]--;
                if (player->crafting_grid_counts[top] == 0) player->crafting_grid[top] = 0;
                
                player->crafting_grid_counts[bottom]--;
                if (player->crafting_grid_counts[bottom] == 0) player->crafting_grid[bottom] = 0;
                break;
            }
        }
    }
}

void player_return_crafting_to_inventory(Player *player) {
    if (!player) return;
    
    for (int slot = 0; slot < CRAFTING_SIZE; ++slot) {
        if (player->crafting_grid_counts[slot] == 0) continue;
        
        uint8_t type = player->crafting_grid[slot];
        uint8_t count = player->crafting_grid_counts[slot];
        
        for (int i = 0; i < INVENTORY_SIZE && count > 0; ++i) {
            if (player->inventory_counts[i] > 0 && player->inventory[i] == type) {
                uint16_t total = (uint16_t)player->inventory_counts[i] + count;
                if (total <= UINT8_MAX) {
                    player->inventory_counts[i] = (uint8_t)total;
                    count = 0;
                } else {
                    uint8_t space = UINT8_MAX - player->inventory_counts[i];
                    player->inventory_counts[i] = UINT8_MAX;
                    count -= space;
                }
            }
        }
        
        for (int i = 0; i < INVENTORY_SIZE && count > 0; ++i) {
            if (player->inventory_counts[i] == 0) {
                player->inventory[i] = type;
                player->inventory_counts[i] = count;
                count = 0;
            }
        }
        
        player->crafting_grid[slot] = 0;
        player->crafting_grid_counts[slot] = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* UI Helpers                                                                 */
/* -------------------------------------------------------------------------- */

int player_inventory_slot_from_mouse(float aspect, float mouse_x, float mouse_y,
                                     float window_w, float window_h) {
    InventoryLayout layout = calculate_layout(aspect);
    return mouse_to_grid_slot(mouse_x, mouse_y, window_w, window_h,
                              layout.inv_left, layout.inv_right,
                              layout.inv_bottom, layout.inv_top,
                              INVENTORY_COLS, INVENTORY_ROWS);
}

int player_crafting_slot_from_mouse(float aspect, float mouse_x, float mouse_y,
                                    float window_w, float window_h) {
    InventoryLayout layout = calculate_layout(aspect);
    return mouse_to_grid_slot(mouse_x, mouse_y, window_w, window_h,
                              layout.craft_left, layout.craft_right,
                              layout.craft_bottom, layout.craft_top,
                              CRAFTING_COLS, CRAFTING_ROWS);
}

int player_crafting_result_slot_from_mouse(float aspect, float mouse_x, float mouse_y,
                                           float window_w, float window_h) {
    if (window_w <= 0.0f || window_h <= 0.0f) return -1;
    
    float ndc_x = (mouse_x / window_w) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (mouse_y / window_h) * 2.0f;
    
    InventoryLayout layout = calculate_layout(aspect);
    
    if (ndc_x >= layout.result_left && ndc_x <= layout.result_right &&
        ndc_y >= layout.result_bottom && ndc_y <= layout.result_top) {
        return 0;
    }
    
    return -1;
}

/* -------------------------------------------------------------------------- */
/* Rendering Helpers                                                          */
/* -------------------------------------------------------------------------- */

void player_inventory_grid_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices,
                                    uint32_t *out_count, float *out_h_step, float *out_v_step) {
    if (!out_vertices || !out_count) return;
    
    InventoryLayout layout = calculate_layout(aspect);
    uint32_t count = 0;
    
    append_line(out_vertices, &count, max_vertices, layout.inv_left, layout.inv_bottom, layout.inv_right, layout.inv_bottom);
    append_line(out_vertices, &count, max_vertices, layout.inv_right, layout.inv_bottom, layout.inv_right, layout.inv_top);
    append_line(out_vertices, &count, max_vertices, layout.inv_right, layout.inv_top, layout.inv_left, layout.inv_top);
    append_line(out_vertices, &count, max_vertices, layout.inv_left, layout.inv_top, layout.inv_left, layout.inv_bottom);
    
    for (int col = 1; col < INVENTORY_COLS; ++col) {
        float x = layout.inv_left + col * layout.cell_w;
        append_line(out_vertices, &count, max_vertices, x, layout.inv_bottom, x, layout.inv_top);
    }
    
    for (int row = 1; row < INVENTORY_ROWS; ++row) {
        float y = layout.inv_bottom + row * layout.cell_h;
        append_line(out_vertices, &count, max_vertices, layout.inv_left, y, layout.inv_right, y);
    }
    
    *out_count = count;
    if (out_h_step) *out_h_step = layout.cell_w;
    if (out_v_step) *out_v_step = layout.cell_h;
}

void player_inventory_selection_vertices(int slot, float aspect, Vertex *out_vertices,
                                         uint32_t max_vertices, uint32_t *out_count) {
    *out_count = 0;
    if (!out_vertices || slot < 0 || slot >= INVENTORY_SIZE) return;
    
    InventoryLayout layout = calculate_layout(aspect);
    
    int row = slot / INVENTORY_COLS;
    int col = slot % INVENTORY_COLS;
    
    float cell_left = layout.inv_left + col * layout.cell_w;
    float cell_right = cell_left + layout.cell_w;
    float cell_top = layout.inv_top - row * layout.cell_h;
    float cell_bottom = cell_top - layout.cell_h;
    
    float pad = fminf(layout.cell_w, layout.cell_h) * 0.04f;
    cell_left += pad;
    cell_right -= pad;
    cell_top -= pad;
    cell_bottom += pad;
    
    append_line(out_vertices, out_count, max_vertices, cell_left, cell_top, cell_right, cell_top);
    append_line(out_vertices, out_count, max_vertices, cell_right, cell_top, cell_right, cell_bottom);
    append_line(out_vertices, out_count, max_vertices, cell_right, cell_bottom, cell_left, cell_bottom);
    append_line(out_vertices, out_count, max_vertices, cell_left, cell_bottom, cell_left, cell_top);
}

void player_inventory_icon_vertices(float h_step, float v_step, Vertex *out_vertices,
                                    uint32_t max_vertices, uint32_t *out_count) {
    if (!out_vertices || !out_count) return;
    
    const float icon_half_x = h_step * 0.35f;
    const float icon_half_y = v_step * 0.35f;
    
    Vertex icon_vertices[6] = {
        {{-icon_half_x, -icon_half_y, 0.0f}, {0.0f, 1.0f}},
        {{ icon_half_x, -icon_half_y, 0.0f}, {1.0f, 1.0f}},
        {{ icon_half_x,  icon_half_y, 0.0f}, {1.0f, 0.0f}},
        {{-icon_half_x, -icon_half_y, 0.0f}, {0.0f, 1.0f}},
        {{ icon_half_x,  icon_half_y, 0.0f}, {1.0f, 0.0f}},
        {{-icon_half_x,  icon_half_y, 0.0f}, {0.0f, 0.0f}},
    };
    
    uint32_t count = 6;
    if (count > max_vertices) count = max_vertices;
    for (uint32_t i = 0; i < count; ++i) out_vertices[i] = icon_vertices[i];
    *out_count = count;
}

void player_inventory_background_vertices(float aspect, Vertex *out_vertices,
                                          uint32_t max_vertices, uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices < 18) return;
    
    InventoryLayout layout = calculate_layout(aspect);
    uint32_t count = 0;
    
    out_vertices[count++] = (Vertex){{layout.inv_left, layout.inv_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{layout.inv_right, layout.inv_bottom, 0}, {1, 1}};
    out_vertices[count++] = (Vertex){{layout.inv_right, layout.inv_top, 0}, {1, 0}};
    out_vertices[count++] = (Vertex){{layout.inv_left, layout.inv_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{layout.inv_left, layout.inv_bottom, 0}, {0, 1}};
    out_vertices[count++] = (Vertex){{layout.inv_right, layout.inv_bottom, 0}, {1, 1}};
    
    out_vertices[count++] = (Vertex){{layout.craft_left, layout.craft_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{layout.craft_right, layout.craft_bottom, 0}, {1, 1}};
    out_vertices[count++] = (Vertex){{layout.craft_right, layout.craft_top, 0}, {1, 0}};
    out_vertices[count++] = (Vertex){{layout.craft_left, layout.craft_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{layout.craft_left, layout.craft_bottom, 0}, {0, 1}};
    out_vertices[count++] = (Vertex){{layout.craft_right, layout.craft_bottom, 0}, {1, 1}};
    
    out_vertices[count++] = (Vertex){{layout.result_left, layout.result_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{layout.result_right, layout.result_bottom, 0}, {1, 1}};
    out_vertices[count++] = (Vertex){{layout.result_right, layout.result_top, 0}, {1, 0}};
    out_vertices[count++] = (Vertex){{layout.result_left, layout.result_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{layout.result_left, layout.result_bottom, 0}, {0, 1}};
    out_vertices[count++] = (Vertex){{layout.result_right, layout.result_bottom, 0}, {1, 1}};
    
    *out_count = count;
}

void player_crafting_grid_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices,
                                   uint32_t *out_count) {
    if (!out_vertices || !out_count) return;
    
    InventoryLayout layout = calculate_layout(aspect);
    uint32_t count = 0;
    
    append_line(out_vertices, &count, max_vertices, layout.craft_left, layout.craft_bottom, layout.craft_right, layout.craft_bottom);
    append_line(out_vertices, &count, max_vertices, layout.craft_right, layout.craft_bottom, layout.craft_right, layout.craft_top);
    append_line(out_vertices, &count, max_vertices, layout.craft_right, layout.craft_top, layout.craft_left, layout.craft_top);
    append_line(out_vertices, &count, max_vertices, layout.craft_left, layout.craft_top, layout.craft_left, layout.craft_bottom);
    
    float h_step = (layout.craft_right - layout.craft_left) / CRAFTING_COLS;
    float v_step = (layout.craft_top - layout.craft_bottom) / CRAFTING_ROWS;
    
    for (int col = 1; col < CRAFTING_COLS; ++col) {
        float x = layout.craft_left + col * h_step;
        append_line(out_vertices, &count, max_vertices, x, layout.craft_bottom, x, layout.craft_top);
    }
    
    for (int row = 1; row < CRAFTING_ROWS; ++row) {
        float y = layout.craft_bottom + row * v_step;
        append_line(out_vertices, &count, max_vertices, layout.craft_left, y, layout.craft_right, y);
    }
    
    *out_count = count;
}

void player_crafting_arrow_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices,
                                    uint32_t *out_count) {
    if (!out_vertices || !out_count) return;
    
    InventoryLayout layout = calculate_layout(aspect);
    
    float mid_y = (layout.arrow_top + layout.arrow_bottom) * 0.5f;
    float head_size = (layout.arrow_right - layout.arrow_left) * 0.3f;
    
    uint32_t count = 0;
    append_line(out_vertices, &count, max_vertices, layout.arrow_left, mid_y, layout.arrow_right, mid_y);
    append_line(out_vertices, &count, max_vertices, layout.arrow_right, mid_y, layout.arrow_right - head_size, layout.arrow_top);
    append_line(out_vertices, &count, max_vertices, layout.arrow_right, mid_y, layout.arrow_right - head_size, layout.arrow_bottom);
    
    *out_count = count;
}

void player_crafting_result_slot_vertices(float aspect, Vertex *out_vertices,
                                          uint32_t max_vertices, uint32_t *out_count) {
    if (!out_vertices || !out_count) return;
    
    InventoryLayout layout = calculate_layout(aspect);
    uint32_t count = 0;
    
    append_line(out_vertices, &count, max_vertices, layout.result_left, layout.result_bottom, layout.result_right, layout.result_bottom);
    append_line(out_vertices, &count, max_vertices, layout.result_right, layout.result_bottom, layout.result_right, layout.result_top);
    append_line(out_vertices, &count, max_vertices, layout.result_right, layout.result_top, layout.result_left, layout.result_top);
    append_line(out_vertices, &count, max_vertices, layout.result_left, layout.result_top, layout.result_left, layout.result_bottom);
    
    *out_count = count;
}

uint32_t player_inventory_icon_instances(const Player *player, float aspect,
                                         InstanceData *out_instances, uint32_t max_instances) {
    if (!player) return 0;
    
    InventoryLayout layout = calculate_layout(aspect);
    uint32_t icon_index = 0;
    
    for (int slot = 0; slot < INVENTORY_SIZE; ++slot) {
        if (player->inventory_counts[slot] == 0) continue;
        if (out_instances && icon_index < max_instances) {
            int row = slot / INVENTORY_COLS;
            int col = slot % INVENTORY_COLS;
            float center_x = layout.inv_left + layout.cell_w * (0.5f + col);
            float center_y = layout.inv_top - layout.cell_h * (0.5f + row);
            out_instances[icon_index] = (InstanceData){
                center_x, center_y, 0.0f, (uint32_t)player->inventory[slot]
            };
        }
        icon_index++;
    }
    
    float craft_h_step = (layout.craft_right - layout.craft_left) / CRAFTING_COLS;
    float craft_v_step = (layout.craft_top - layout.craft_bottom) / CRAFTING_ROWS;
    
    for (int slot = 0; slot < CRAFTING_SIZE; ++slot) {
        if (player->crafting_grid_counts[slot] == 0) continue;
        if (out_instances && icon_index < max_instances) {
            int row = slot / CRAFTING_COLS;
            int col = slot % CRAFTING_COLS;
            float center_x = layout.craft_left + craft_h_step * (0.5f + col);
            float center_y = layout.craft_top - craft_v_step * (0.5f + row);
            out_instances[icon_index] = (InstanceData){
                center_x, center_y, 0.0f, (uint32_t)player->crafting_grid[slot]
            };
        }
        icon_index++;
    }
    
    if (player->inventory_open) {
        CraftingResult craft_result = player_get_crafting_result(player);
        if (craft_result.valid) {
            if (out_instances && icon_index < max_instances) {
                float center_x = (layout.result_left + layout.result_right) * 0.5f;
                float center_y = (layout.result_bottom + layout.result_top) * 0.5f;
                out_instances[icon_index] = (InstanceData){
                    center_x, center_y, 0.0f, (uint32_t)craft_result.result_type
                };
            }
            icon_index++;
        }
    }
    
    if (player->inventory_open && player->inventory_mouse_valid && player->inventory_held_count > 0) {
        if (out_instances && icon_index < max_instances) {
            out_instances[icon_index] = (InstanceData){
                player->inventory_mouse_ndc_x,
                player->inventory_mouse_ndc_y,
                0.0f,
                (uint32_t)player->inventory_held_type
            };
        }
        icon_index++;
    }
    
    return icon_index;
}

uint32_t player_inventory_count_vertices(const Player *player, float aspect,
                                         Vertex *out_vertices, uint32_t max_vertices) {
    if (!player || !out_vertices) return 0;
    
    InventoryLayout layout = calculate_layout(aspect);
    uint32_t count = 0;
    
    for (int slot = 0; slot < INVENTORY_SIZE; ++slot) {
        if (player->inventory_counts[slot] == 0) continue;
        
        int row = slot / INVENTORY_COLS;
        int col = slot % INVENTORY_COLS;
        float cell_left = layout.inv_left + col * layout.cell_w;
        float cell_top = layout.inv_top - row * layout.cell_h;
        
        draw_number(out_vertices, &count, max_vertices,
                   player->inventory_counts[slot], cell_left, cell_top,
                   layout.cell_w, layout.cell_h);
    }
    
    float craft_h_step = (layout.craft_right - layout.craft_left) / CRAFTING_COLS;
    float craft_v_step = (layout.craft_top - layout.craft_bottom) / CRAFTING_ROWS;
    
    for (int slot = 0; slot < CRAFTING_SIZE; ++slot) {
        if (player->crafting_grid_counts[slot] == 0) continue;
        
        int row = slot / CRAFTING_COLS;
        int col = slot % CRAFTING_COLS;
        float cell_left = layout.craft_left + col * craft_h_step;
        float cell_top = layout.craft_top - row * craft_v_step;
        
        draw_number(out_vertices, &count, max_vertices,
                   player->crafting_grid_counts[slot], cell_left, cell_top,
                   craft_h_step, craft_v_step);
    }
    
    if (player->inventory_open) {
        CraftingResult craft_result = player_get_crafting_result(player);
        if (craft_result.valid && craft_result.result_count > 0) {
            float center_x = (layout.result_left + layout.result_right) * 0.5f;
            float center_y = (layout.result_bottom + layout.result_top) * 0.5f;
            float cell_left = center_x - layout.cell_w * 0.5f;
            float cell_top = center_y + layout.cell_h * 0.5f;
            
            draw_number(out_vertices, &count, max_vertices,
                       craft_result.result_count, cell_left, cell_top,
                       layout.cell_w, layout.cell_h);
        }
    }
    
    if (player->inventory_open && player->inventory_mouse_valid && player->inventory_held_count > 0) {
        float center_x = player->inventory_mouse_ndc_x;
        float center_y = player->inventory_mouse_ndc_y;
        float cell_left = center_x - layout.cell_w * 0.5f;
        float cell_top = center_y + layout.cell_h * 0.5f;
        
        draw_number(out_vertices, &count, max_vertices,
                   player->inventory_held_count, cell_left, cell_top,
                   layout.cell_w, layout.cell_h);
    }
    
    return count;
}

/* -------------------------------------------------------------------------- */
/* Health Bar Rendering                                                       */
/* -------------------------------------------------------------------------- */

void player_health_bar_background_vertices(const Player *player, float aspect,
                                           Vertex *out_vertices,
                                           uint32_t max_vertices, uint32_t *out_count) {
    if (!out_vertices || !out_count || !player) return;
    
    const float heart_width = 0.02f;
    const float heart_height = heart_width / aspect;
    const float gap = 0.005f;
    const float bottom_margin = 0.08f;
    const int num_hearts = 10;
    
    const float total_width = num_hearts * heart_width + (num_hearts - 1) * gap;
    const float start_x = -total_width * 0.5f;
    const float bottom_y = -1.0f + bottom_margin;
    
    uint32_t count = 0;
    int health = player->health > 10 ? 10 : player->health;
    
    for (int i = 0; i < health; ++i) {
        float left = start_x + i * (heart_width + gap);
        float right = left + heart_width;
        float bottom = bottom_y;
        float top = bottom + heart_height;
        
        if (count + 6 > max_vertices) break;
        
        out_vertices[count++] = (Vertex){{left, bottom, 0}, {0, 0}};
        out_vertices[count++] = (Vertex){{right, bottom, 0}, {1, 0}};
        out_vertices[count++] = (Vertex){{right, top, 0}, {1, 1}};
        out_vertices[count++] = (Vertex){{left, bottom, 0}, {0, 0}};
        out_vertices[count++] = (Vertex){{right, top, 0}, {1, 1}};
        out_vertices[count++] = (Vertex){{left, top, 0}, {0, 1}};
    }
    
    *out_count = count;
}

void player_health_bar_border_vertices(float aspect, Vertex *out_vertices,
                                       uint32_t max_vertices, uint32_t *out_count) {
    if (!out_vertices || !out_count) return;
    
    const float heart_width = 0.02f;
    const float heart_height = heart_width / aspect;
    const float gap = 0.005f;
    const float bottom_margin = 0.08f;
    const int num_hearts = 10;
    
    const float total_width = num_hearts * heart_width + (num_hearts - 1) * gap;
    const float start_x = -total_width * 0.5f;
    const float bottom_y = -1.0f + bottom_margin;
    
    uint32_t count = 0;
    
    for (int i = 0; i < num_hearts; ++i) {
        float left = start_x + i * (heart_width + gap);
        float right = left + heart_width;
        float bottom = bottom_y;
        float top = bottom + heart_height;
        
        if (count + 8 > max_vertices) break;
        
        out_vertices[count++] = (Vertex){{left, bottom, 0}, {0, 0}};
        out_vertices[count++] = (Vertex){{right, bottom, 0}, {0, 0}};
        
        out_vertices[count++] = (Vertex){{right, bottom, 0}, {0, 0}};
        out_vertices[count++] = (Vertex){{right, top, 0}, {0, 0}};
        
        out_vertices[count++] = (Vertex){{right, top, 0}, {0, 0}};
        out_vertices[count++] = (Vertex){{left, top, 0}, {0, 0}};
        
        out_vertices[count++] = (Vertex){{left, top, 0}, {0, 0}};
        out_vertices[count++] = (Vertex){{left, bottom, 0}, {0, 0}};
    }
    
    *out_count = count;
}