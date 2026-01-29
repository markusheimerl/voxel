#include "player.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

static const float PLAYER_GRAVITY = 17.0f;
static const float PLAYER_JUMP_HEIGHT = 1.2f;
static const float PLAYER_EYE_HEIGHT = 1.6f;

static float player_jump_velocity(void) {
    return sqrtf(2.0f * PLAYER_GRAVITY * PLAYER_JUMP_HEIGHT);
}

static bool is_key_pressed(const bool *keys, unsigned char c) {
    return keys && keys[c];
}

static bool is_key_pressed_ci(const bool *keys, char c) {
    return is_key_pressed(keys, (unsigned char)c) ||
           is_key_pressed(keys, (unsigned char)toupper((unsigned char)c));
}

void player_init(Player *player, Vec3 spawn_position) {
    if (!player) return;
    memset(player, 0, sizeof(*player));
    player->position = spawn_position;
    player->inventory_open = false;
    player->selected_slot = 0;
    player->inventory_held_origin_slot = 0;
    player->inventory_held_origin_valid = false;
    player->inventory_mouse_ndc_x = 0.0f;
    player->inventory_mouse_ndc_y = 0.0f;
    player->inventory_mouse_valid = false;
}

float player_eye_height(void) {
    return PLAYER_EYE_HEIGHT;
}

void player_compute_movement(const Player *player,
                             const Camera *camera,
                             const bool *keys,
                             bool movement_enabled,
                             float delta_time,
                             Vec3 *out_move_delta,
                             bool *out_wants_jump) {
    (void)player;
    if (out_move_delta) *out_move_delta = vec3(0.0f, 0.0f, 0.0f);
    if (out_wants_jump) *out_wants_jump = false;

    if (!player || !camera || !movement_enabled || !out_move_delta || !out_wants_jump) return;

    Vec3 forward = camera->front;
    forward.y = 0.0f;
    forward = vec3_normalize(forward);

    Vec3 right = camera->right;
    right.y = 0.0f;
    right = vec3_normalize(right);

    Vec3 movement_dir = vec3(0.0f, 0.0f, 0.0f);
    if (is_key_pressed_ci(keys, 'w')) movement_dir = vec3_add(movement_dir, forward);
    if (is_key_pressed_ci(keys, 's')) movement_dir = vec3_sub(movement_dir, forward);
    if (is_key_pressed_ci(keys, 'a')) movement_dir = vec3_sub(movement_dir, right);
    if (is_key_pressed_ci(keys, 'd')) movement_dir = vec3_add(movement_dir, right);

    if (vec3_length(movement_dir) > 0.0f) movement_dir = vec3_normalize(movement_dir);

    *out_move_delta = vec3_scale(movement_dir, camera->movement_speed * delta_time);
    *out_wants_jump = is_key_pressed(keys, ' ');
}

void player_apply_physics(Player *player,
                          World *world,
                          float delta_time,
                          Vec3 move_delta,
                          bool wants_jump) {
    if (!player || !world) return;

    if (wants_jump && player->on_ground) {
        player->velocity_y = player_jump_velocity();
        player->on_ground = false;
    }

    if (!player->on_ground) player->velocity_y -= PLAYER_GRAVITY * delta_time;
    else player->velocity_y = 0.0f;

    player->position.x += move_delta.x;
    resolve_collision_axis(world, &player->position, move_delta.x, 0);

    player->position.z += move_delta.z;
    resolve_collision_axis(world, &player->position, move_delta.z, 2);

    player->position.y += player->velocity_y * delta_time;
    resolve_collision_y(world, &player->position, &player->velocity_y, &player->on_ground);
}

void player_handle_block_interaction(Player *player,
                                     World *world,
                                     RayHit ray_hit,
                                     bool left_click,
                                     bool right_click,
                                     bool interaction_enabled) {
    if (!player || !world || !interaction_enabled) return;
    if (!ray_hit.hit) return;

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
        world_add_block(world, place, place_type);
        player->inventory_counts[slot]--;
        if (player->inventory_counts[slot] == 0) player->inventory[slot] = 0;
    }
}

static void player_compute_aabb(Vec3 pos, AABB *aabb) {
    const float half_width = 0.4f;
    const float height = 1.8f;

    aabb->min = vec3(pos.x - half_width, pos.y, pos.z - half_width);
    aabb->max = vec3(pos.x + half_width, pos.y + height, pos.z + half_width);
}

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

RayHit raycast_blocks(World *world, Vec3 origin, Vec3 direction, float max_distance) {
    const float step = 0.05f;
    RayHit result = {0};
    Vec3 dir = vec3_normalize(direction);

    IVec3 previous_cell = world_to_cell(origin);

    for (float t = 0.0f; t <= max_distance; t += step) {
        Vec3 point = vec3_add(origin, vec3_scale(dir, t));
        IVec3 cell = world_to_cell(point);

        if (!ivec3_equal(cell, previous_cell)) {
            uint8_t type = 0;
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
/* Player Collision Helpers                                                   */
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
                uint8_t type = 0;
                if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                if (type == BLOCK_WATER) continue;

                player_compute_aabb(*position, &player_box);
                AABB block_box = cell_aabb((IVec3){x, y, z});

                if (!((player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
                      (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
                      (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z)))
                {
                    continue;
                }

                switch (axis) {
                case 0: /* X axis */
                    if (delta > 0.0f) position->x = block_box.min.x - 0.4f - 0.001f;
                    else position->x = block_box.max.x + 0.4f + 0.001f;
                    break;
                case 2: /* Z axis */
                    if (delta > 0.0f) position->z = block_box.min.z - 0.4f - 0.001f;
                    else position->z = block_box.max.z + 0.4f + 0.001f;
                    break;
                default:
                    break;
                }

                player_compute_aabb(*position, &player_box);
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
                uint8_t type = 0;
                if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                if (type == BLOCK_WATER) continue;

                player_compute_aabb(*position, &player_box);
                AABB block_box = cell_aabb((IVec3){x, y, z});

                if (!((player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
                      (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
                      (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z)))
                {
                    continue;
                }

                if (*velocity_y < 0.0f) {
                    position->y = block_box.max.y;
                    *velocity_y = 0.0f;
                    *on_ground = true;
                } else if (*velocity_y > 0.0f) {
                    position->y = block_box.min.y - 1.8f - 0.001f;
                    *velocity_y = 0.0f;
                }

                player_compute_aabb(*position, &player_box);
            }
        }
    }
}

bool block_overlaps_player(const Player *player, IVec3 cell) {
    AABB player_box;
    player_compute_aabb(player->position, &player_box);

    AABB block_box = cell_aabb(cell);
    bool intersect =
        (player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
        (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
        (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z);

    return intersect;
}

/* -------------------------------------------------------------------------- */
/* Inventory                                                                  */
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
    if (!player) return;
    if (slot < 0 || slot >= INVENTORY_SIZE) return;

    uint8_t held_count = player->inventory_held_count;
    uint8_t held_type = player->inventory_held_type;

    if (held_count == 0) {
        if (player->inventory_counts[slot] == 0) return;
        player->inventory_held_type = player->inventory[slot];
        player->inventory_held_count = player->inventory_counts[slot];
        player->inventory[slot] = 0;
        player->inventory_counts[slot] = 0;
        player->inventory_held_origin_slot = (uint8_t)slot;
        player->inventory_held_origin_valid = true;
        return;
    }

    if (player->inventory_counts[slot] == 0) {
        player->inventory[slot] = held_type;
        player->inventory_counts[slot] = held_count;
        player->inventory_held_type = 0;
        player->inventory_held_count = 0;
        player->inventory_held_origin_valid = false;
        return;
    }

    if (player->inventory[slot] == held_type) {
        uint16_t total = (uint16_t)player->inventory_counts[slot] + (uint16_t)held_count;
        if (total <= UINT8_MAX) {
            player->inventory_counts[slot] = (uint8_t)total;
            player->inventory_held_type = 0;
            player->inventory_held_count = 0;
            player->inventory_held_origin_valid = false;
        } else {
            player->inventory_counts[slot] = UINT8_MAX;
            player->inventory_held_count = (uint8_t)(total - UINT8_MAX);
        }
        return;
    }

    uint8_t swap_type = player->inventory[slot];
    uint8_t swap_count = player->inventory_counts[slot];
    player->inventory[slot] = held_type;
    player->inventory_counts[slot] = held_count;
    player->inventory_held_type = swap_type;
    player->inventory_held_count = swap_count;
}

void player_inventory_handle_right_click(Player *player, int slot) {
    if (!player) return;
    if (slot < 0 || slot >= INVENTORY_SIZE) return;

    if (player->inventory_held_count != 0) {
        if (player->inventory_counts[slot] == 0) {
            player->inventory[slot] = player->inventory_held_type;
            player->inventory_counts[slot] = 1;
            player->inventory_held_count--;
        } else if (player->inventory[slot] == player->inventory_held_type &&
                   player->inventory_counts[slot] < UINT8_MAX) {
            player->inventory_counts[slot]++;
            player->inventory_held_count--;
        } else {
            return;
        }

        if (player->inventory_held_count == 0) {
            player->inventory_held_type = 0;
            player->inventory_held_origin_valid = false;
        }
        return;
    }

    if (player->inventory_counts[slot] == 0) return;

    uint8_t slot_count = player->inventory_counts[slot];
    uint8_t take_count = (uint8_t)((slot_count + 1) / 2);
    uint8_t remain_count = (uint8_t)(slot_count - take_count);

    player->inventory_held_type = player->inventory[slot];
    player->inventory_held_count = take_count;
    player->inventory_held_origin_slot = (uint8_t)slot;
    player->inventory_held_origin_valid = true;

    player->inventory_counts[slot] = remain_count;
    if (remain_count == 0) player->inventory[slot] = 0;
}

static bool inventory_place_stack_skip(Player *player, uint8_t type, uint8_t count, int skip_slot) {
    if (!player || count == 0) return true;

    for (int i = 0; i < INVENTORY_SIZE && count > 0; ++i) {
        if (i == skip_slot) continue;
        if (player->inventory_counts[i] == 0) continue;
        if (player->inventory[i] != type) continue;

        uint16_t total = (uint16_t)player->inventory_counts[i] + (uint16_t)count;
        if (total <= UINT8_MAX) {
            player->inventory_counts[i] = (uint8_t)total;
            count = 0;
            break;
        }
        uint8_t space = (uint8_t)(UINT8_MAX - player->inventory_counts[i]);
        player->inventory_counts[i] = UINT8_MAX;
        count = (uint8_t)(count - space);
    }

    if (count == 0) return true;

    for (int i = 0; i < INVENTORY_SIZE && count > 0; ++i) {
        if (i == skip_slot) continue;
        if (player->inventory_counts[i] != 0) continue;
        player->inventory[i] = type;
        player->inventory_counts[i] = count;
        count = 0;
    }

    return count == 0;
}

void player_inventory_cancel_held(Player *player) {
    if (!player) return;
    if (player->inventory_held_count == 0 || !player->inventory_held_origin_valid) return;

    int origin = (int)player->inventory_held_origin_slot;
    if (origin < 0 || origin >= INVENTORY_SIZE) return;

    uint8_t held_type = player->inventory_held_type;
    uint8_t held_count = player->inventory_held_count;

    if (player->inventory_counts[origin] == 0) {
        player->inventory[origin] = held_type;
        player->inventory_counts[origin] = held_count;
        player->inventory_held_type = 0;
        player->inventory_held_count = 0;
        player->inventory_held_origin_valid = false;
        return;
    }

    if (player->inventory[origin] == held_type) {
        uint16_t total = (uint16_t)player->inventory_counts[origin] + (uint16_t)held_count;
        if (total <= UINT8_MAX) {
            player->inventory_counts[origin] = (uint8_t)total;
            player->inventory_held_type = 0;
            player->inventory_held_count = 0;
            player->inventory_held_origin_valid = false;
        } else {
            player->inventory_counts[origin] = UINT8_MAX;
            uint8_t remaining = (uint8_t)(total - UINT8_MAX);
            if (inventory_place_stack_skip(player, held_type, remaining, origin)) {
                player->inventory_held_type = 0;
                player->inventory_held_count = 0;
                player->inventory_held_origin_valid = false;
            } else {
                player->inventory_held_count = remaining;
            }
        }
        return;
    }

    uint8_t displaced_type = player->inventory[origin];
    uint8_t displaced_count = player->inventory_counts[origin];
    if (!inventory_place_stack_skip(player, displaced_type, displaced_count, origin)) {
        return;
    }

    player->inventory[origin] = held_type;
    player->inventory_counts[origin] = held_count;
    player->inventory_held_type = 0;
    player->inventory_held_count = 0;
    player->inventory_held_origin_valid = false;
}

static void append_line(Vertex *verts, uint32_t *count, uint32_t max,
                        float x0, float y0, float x1, float y1) {
    if (*count + 2 > max) return;
    verts[(*count)++] = (Vertex){{x0, y0, 0.0f}, {0.0f, 0.0f}};
    verts[(*count)++] = (Vertex){{x1, y1, 0.0f}, {0.0f, 0.0f}};
}

/* -------------------------------------------------------------------------- */
/* Inventory Layout System                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    float inv_left, inv_right, inv_bottom, inv_top;
    float craft_left, craft_right, craft_bottom, craft_top;
    float arrow_left, arrow_right, arrow_bottom, arrow_top;
    float result_left, result_right, result_bottom, result_top;
    float cell_w, cell_h;
} InventoryLayout;

static InventoryLayout calculate_inventory_layout(float aspect) {
    InventoryLayout layout;

    /* Base inventory dimensions */
    const float inv_base_half_height = 0.13f;
    const float inv_aspect = (float)INVENTORY_COLS / (float)INVENTORY_ROWS;
    const float inv_half_width = inv_base_half_height * aspect * inv_aspect;
    const float inv_half_height = inv_base_half_height;

    /* Cell size */
    layout.cell_w = (inv_half_width * 2.0f) / (float)INVENTORY_COLS;
    layout.cell_h = (inv_half_height * 2.0f) / (float)INVENTORY_ROWS;

    /* Gap between crafting and inventory */
    const float gap = layout.cell_h * 0.7f;

    /* Crafting dimensions (3x3, same cell size) */
    const float craft_width = layout.cell_w * (float)CRAFTING_COLS;
    const float craft_height = layout.cell_h * (float)CRAFTING_ROWS;

    /* Arrow dimensions */
    const float arrow_width = layout.cell_w * 1.2f;
    const float arrow_height = layout.cell_h;

    /* Result slot dimensions */
    const float result_width = layout.cell_w;
    const float result_height = layout.cell_h;

    /* Calculate total height */
    const float total_height = craft_height + gap + inv_half_height * 2.0f;

    /* Center everything vertically */
    const float top_edge = total_height * 0.5f;

    /* Main inventory position (bottom part) */
    layout.inv_left = -inv_half_width;
    layout.inv_right = inv_half_width;
    layout.inv_bottom = top_edge - total_height;
    layout.inv_top = layout.inv_bottom + inv_half_height * 2.0f;

    /* Crafting grid position (top part, left-aligned with inventory) */
    layout.craft_left = layout.inv_left;
    layout.craft_right = layout.craft_left + craft_width;
    layout.craft_bottom = layout.inv_top + gap;
    layout.craft_top = layout.craft_bottom + craft_height;

    /* Arrow position (to the right of crafting grid) */
    layout.arrow_left = layout.craft_right + layout.cell_w * 0.4f;
    layout.arrow_right = layout.arrow_left + arrow_width;
    float craft_center_y = (layout.craft_top + layout.craft_bottom) * 0.5f;
    layout.arrow_bottom = craft_center_y - arrow_height * 0.5f;
    layout.arrow_top = craft_center_y + arrow_height * 0.5f;

    /* Result slot position (to the right of arrow) */
    layout.result_left = layout.arrow_right + layout.cell_w * 0.4f;
    layout.result_right = layout.result_left + result_width;
    layout.result_bottom = craft_center_y - result_height * 0.5f;
    layout.result_top = craft_center_y + result_height * 0.5f;

    return layout;
}

void player_inventory_background_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices, uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices < 18) return;
    
    InventoryLayout layout = calculate_inventory_layout(aspect);
    
    uint32_t count = 0;
    
    // Inventory background (exact grid size)
    float inv_left = layout.inv_left;
    float inv_right = layout.inv_right;
    float inv_bottom = layout.inv_bottom;
    float inv_top = layout.inv_top;
    
    out_vertices[count++] = (Vertex){{inv_left, inv_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{inv_right, inv_bottom, 0}, {1, 1}};
    out_vertices[count++] = (Vertex){{inv_right, inv_top, 0}, {1, 0}};
    out_vertices[count++] = (Vertex){{inv_left, inv_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{inv_left, inv_bottom, 0}, {0, 1}};
    out_vertices[count++] = (Vertex){{inv_right, inv_bottom, 0}, {1, 1}};
    
    // Crafting grid background (exact grid size)
    float craft_left = layout.craft_left;
    float craft_right = layout.craft_right;
    float craft_bottom = layout.craft_bottom;
    float craft_top = layout.craft_top;
    
    out_vertices[count++] = (Vertex){{craft_left, craft_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{craft_right, craft_bottom, 0}, {1, 1}};
    out_vertices[count++] = (Vertex){{craft_right, craft_top, 0}, {1, 0}};
    out_vertices[count++] = (Vertex){{craft_left, craft_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{craft_left, craft_bottom, 0}, {0, 1}};
    out_vertices[count++] = (Vertex){{craft_right, craft_bottom, 0}, {1, 1}};
    
    // Result slot background (exact slot size)
    float result_left = layout.result_left;
    float result_right = layout.result_right;
    float result_bottom = layout.result_bottom;
    float result_top = layout.result_top;
    
    out_vertices[count++] = (Vertex){{result_left, result_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{result_right, result_bottom, 0}, {1, 1}};
    out_vertices[count++] = (Vertex){{result_right, result_top, 0}, {1, 0}};
    out_vertices[count++] = (Vertex){{result_left, result_top, 0}, {0, 0}};
    out_vertices[count++] = (Vertex){{result_left, result_bottom, 0}, {0, 1}};
    out_vertices[count++] = (Vertex){{result_right, result_bottom, 0}, {1, 1}};
    
    *out_count = count;
}

void player_crafting_grid_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices,
                                   uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices == 0) return;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.craft_left;
    float right = layout.craft_right;
    float bottom = layout.craft_bottom;
    float top = layout.craft_top;
    float h_step = (right - left) / (float)CRAFTING_COLS;
    float v_step = (top - bottom) / (float)CRAFTING_ROWS;

    uint32_t count = 0;

    /* Border */
    append_line(out_vertices, &count, max_vertices, left, bottom, right, bottom);
    append_line(out_vertices, &count, max_vertices, right, bottom, right, top);
    append_line(out_vertices, &count, max_vertices, right, top, left, top);
    append_line(out_vertices, &count, max_vertices, left, top, left, bottom);

    /* Vertical grid lines */
    for (int col = 1; col < CRAFTING_COLS; ++col) {
        float x = left + (float)col * h_step;
        append_line(out_vertices, &count, max_vertices, x, bottom, x, top);
    }

    /* Horizontal grid lines */
    for (int row = 1; row < CRAFTING_ROWS; ++row) {
        float y = bottom + (float)row * v_step;
        append_line(out_vertices, &count, max_vertices, left, y, right, y);
    }

    *out_count = count;
}

void player_crafting_arrow_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices,
                                    uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices == 0) return;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.arrow_left;
    float right = layout.arrow_right;
    float bottom = layout.arrow_bottom;
    float top = layout.arrow_top;
    float mid_y = (top + bottom) * 0.5f;

    uint32_t count = 0;

    /* Arrow shaft */
    append_line(out_vertices, &count, max_vertices, left, mid_y, right, mid_y);

    /* Arrow head */
    float head_size = (right - left) * 0.3f;
    append_line(out_vertices, &count, max_vertices, right, mid_y, right - head_size, top);
    append_line(out_vertices, &count, max_vertices, right, mid_y, right - head_size, bottom);

    *out_count = count;
}

void player_crafting_result_slot_vertices(float aspect, Vertex *out_vertices, uint32_t max_vertices,
                                          uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices == 0) return;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.result_left;
    float right = layout.result_right;
    float bottom = layout.result_bottom;
    float top = layout.result_top;

    uint32_t count = 0;

    /* Result slot border */
    append_line(out_vertices, &count, max_vertices, left, bottom, right, bottom);
    append_line(out_vertices, &count, max_vertices, right, bottom, right, top);
    append_line(out_vertices, &count, max_vertices, right, top, left, top);
    append_line(out_vertices, &count, max_vertices, left, top, left, bottom);

    *out_count = count;
}

int player_inventory_slot_from_mouse(float aspect,
                                     float mouse_x,
                                     float mouse_y,
                                     float window_w,
                                     float window_h) {
    if (window_w <= 0.0f || window_h <= 0.0f) return -1;

    float ndc_x = (mouse_x / window_w) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (mouse_y / window_h) * 2.0f;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.inv_left;
    float right = layout.inv_right;
    float bottom = layout.inv_bottom;
    float top = layout.inv_top;

    if (ndc_x < left || ndc_x > right || ndc_y < bottom || ndc_y > top) return -1;

    const float h_step = (right - left) / (float)INVENTORY_COLS;
    const float v_step = (top - bottom) / (float)INVENTORY_ROWS;

    int col = (int)((ndc_x - left) / h_step);
    int row = (int)((top - ndc_y) / v_step);

    if (col < 0 || col >= INVENTORY_COLS || row < 0 || row >= INVENTORY_ROWS) return -1;

    return row * INVENTORY_COLS + col;
}

void player_inventory_selection_vertices(int slot,
                                         float aspect,
                                         Vertex *out_vertices,
                                         uint32_t max_vertices,
                                         uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices == 0) return;
    *out_count = 0;

    if (slot < 0 || slot >= INVENTORY_SIZE) return;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.inv_left;
    float right = layout.inv_right;
    float bottom = layout.inv_bottom;
    float top = layout.inv_top;
    float h_step = (right - left) / (float)INVENTORY_COLS;
    float v_step = (top - bottom) / (float)INVENTORY_ROWS;

    int row = slot / INVENTORY_COLS;
    int col = slot % INVENTORY_COLS;

    float cell_left = left + (float)col * h_step;
    float cell_right = cell_left + h_step;
    float cell_top = top - (float)row * v_step;
    float cell_bottom = cell_top - v_step;

    float pad = fminf(h_step, v_step) * 0.04f;
    cell_left += pad;
    cell_right -= pad;
    cell_top -= pad;
    cell_bottom += pad;

    append_line(out_vertices, out_count, max_vertices, cell_left, cell_top, cell_right, cell_top);
    append_line(out_vertices, out_count, max_vertices, cell_right, cell_top, cell_right, cell_bottom);
    append_line(out_vertices, out_count, max_vertices, cell_right, cell_bottom, cell_left, cell_bottom);
    append_line(out_vertices, out_count, max_vertices, cell_left, cell_bottom, cell_left, cell_top);
}

static void append_digit(Vertex *verts, uint32_t *count, uint32_t max,
                         int digit, float x, float y, float w, float h) {
    if (digit < 0 || digit > 9) return;

    float x0 = x;
    float x1 = x + w;
    float y0 = y;
    float y1 = y + h;
    float ym = y + h * 0.5f;

    bool seg[7] = {false};
    switch (digit) {
    case 0: seg[0]=seg[1]=seg[2]=seg[3]=seg[4]=seg[5]=true; break;
    case 1: seg[1]=seg[2]=true; break;
    case 2: seg[0]=seg[1]=seg[6]=seg[4]=seg[3]=true; break;
    case 3: seg[0]=seg[1]=seg[6]=seg[2]=seg[3]=true; break;
    case 4: seg[5]=seg[6]=seg[1]=seg[2]=true; break;
    case 5: seg[0]=seg[5]=seg[6]=seg[2]=seg[3]=true; break;
    case 6: seg[0]=seg[5]=seg[4]=seg[3]=seg[2]=seg[6]=true; break;
    case 7: seg[0]=seg[1]=seg[2]=true; break;
    case 8: seg[0]=seg[1]=seg[2]=seg[3]=seg[4]=seg[5]=seg[6]=true; break;
    case 9: seg[0]=seg[1]=seg[2]=seg[3]=seg[5]=seg[6]=true; break;
    default: break;
    }

    if (seg[0]) append_line(verts, count, max, x0, y1, x1, y1); /* A */
    if (seg[1]) append_line(verts, count, max, x1, y1, x1, ym); /* B */
    if (seg[2]) append_line(verts, count, max, x1, ym, x1, y0); /* C */
    if (seg[3]) append_line(verts, count, max, x0, y0, x1, y0); /* D */
    if (seg[4]) append_line(verts, count, max, x0, ym, x0, y0); /* E */
    if (seg[5]) append_line(verts, count, max, x0, y1, x0, ym); /* F */
    if (seg[6]) append_line(verts, count, max, x0, ym, x1, ym); /* G */
}

void player_inventory_grid_vertices(float aspect,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count,
                                    float *out_h_step,
                                    float *out_v_step) {
    if (!out_vertices || !out_count || max_vertices == 0) return;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.inv_left;
    float right = layout.inv_right;
    float bottom = layout.inv_bottom;
    float top = layout.inv_top;
    float h_step = (right - left) / (float)INVENTORY_COLS;
    float v_step = (top - bottom) / (float)INVENTORY_ROWS;

    uint32_t count = 0;

    /* Border */
    append_line(out_vertices, &count, max_vertices, left, bottom, right, bottom);
    append_line(out_vertices, &count, max_vertices, right, bottom, right, top);
    append_line(out_vertices, &count, max_vertices, right, top, left, top);
    append_line(out_vertices, &count, max_vertices, left, top, left, bottom);

    /* Vertical grid lines */
    for (int col = 1; col < INVENTORY_COLS; ++col) {
        float x = left + (float)col * h_step;
        append_line(out_vertices, &count, max_vertices, x, bottom, x, top);
    }

    /* Horizontal grid lines */
    for (int row = 1; row < INVENTORY_ROWS; ++row) {
        float y = bottom + (float)row * v_step;
        append_line(out_vertices, &count, max_vertices, left, y, right, y);
    }

    *out_count = count;
    if (out_h_step) *out_h_step = h_step;
    if (out_v_step) *out_v_step = v_step;
}

void player_inventory_icon_vertices(float h_step,
                                    float v_step,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices == 0) return;

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

    uint32_t count = (uint32_t)(sizeof(icon_vertices) / sizeof(icon_vertices[0]));
    if (count > max_vertices) count = max_vertices;
    for (uint32_t i = 0; i < count; ++i) out_vertices[i] = icon_vertices[i];
    *out_count = count;
}

uint32_t player_inventory_icon_instances(const Player *player,
                                         float aspect,
                                         InstanceData *out_instances,
                                         uint32_t max_instances) {
    if (!player) return 0;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.inv_left;
    float right = layout.inv_right;
    float bottom = layout.inv_bottom;
    float top = layout.inv_top;
    float h_step = (right - left) / (float)INVENTORY_COLS;
    float v_step = (top - bottom) / (float)INVENTORY_ROWS;

    uint32_t icon_index = 0;
    for (int slot = 0; slot < INVENTORY_SIZE; ++slot) {
        if (player->inventory_counts[slot] == 0) continue;
        if (out_instances && icon_index < max_instances) {
            int row = slot / INVENTORY_COLS;
            int col = slot % INVENTORY_COLS;
            float center_x = left + h_step * 0.5f + (float)col * h_step;
            float center_y = top - v_step * 0.5f - (float)row * v_step;
            out_instances[icon_index] = (InstanceData){
                center_x, center_y, 0.0f, (uint32_t)player->inventory[slot]
            };
        }
        icon_index++;
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

uint32_t player_inventory_count_vertices(const Player *player,
                                         float aspect,
                                         Vertex *out_vertices,
                                         uint32_t max_vertices) {
    if (!player || !out_vertices || max_vertices == 0) return 0;

    InventoryLayout layout = calculate_inventory_layout(aspect);

    float left = layout.inv_left;
    float right = layout.inv_right;
    float bottom = layout.inv_bottom;
    float top = layout.inv_top;
    float h_step = (right - left) / (float)INVENTORY_COLS;
    float v_step = (top - bottom) / (float)INVENTORY_ROWS;

    uint32_t count_vertices = 0;

    for (int slot = 0; slot < INVENTORY_SIZE; ++slot) {
        if (player->inventory_counts[slot] == 0) continue;

        int row = slot / INVENTORY_COLS;
        int col = slot % INVENTORY_COLS;
        float cell_left = left + (float)col * h_step;
        float cell_top = top - (float)row * v_step;

        float digit_w = h_step * 0.16f;
        float digit_h = v_step * 0.35f;
        float gap = digit_w * 0.25f;

        uint8_t count = player->inventory_counts[slot];
        int d0 = count % 10;
        int d1 = (count / 10) % 10;
        int d2 = (count / 100) % 10;

        int digits[3];
        int digit_count = 0;
        if (count >= 100) { digits[digit_count++] = d2; }
        if (count >= 10) { digits[digit_count++] = d1; }
        digits[digit_count++] = d0;

        float total_w = (float)digit_count * digit_w + (float)(digit_count - 1) * gap;
        float start_x = cell_left + h_step - total_w - h_step * 0.08f;
        float start_y = cell_top - v_step + v_step * 0.12f;

        for (int i = 0; i < digit_count; ++i) {
            append_digit(out_vertices, &count_vertices, max_vertices,
                         digits[i], start_x + i * (digit_w + gap), start_y, digit_w, digit_h);
        }
    }

    if (player->inventory_open && player->inventory_mouse_valid && player->inventory_held_count > 0) {
        uint8_t count = player->inventory_held_count;
        int d0 = count % 10;
        int d1 = (count / 10) % 10;
        int d2 = (count / 100) % 10;

        int digits[3];
        int digit_count = 0;
        if (count >= 100) { digits[digit_count++] = d2; }
        if (count >= 10) { digits[digit_count++] = d1; }
        digits[digit_count++] = d0;

        float center_x = player->inventory_mouse_ndc_x;
        float center_y = player->inventory_mouse_ndc_y;
        float cell_left = center_x - h_step * 0.5f;
        float cell_top = center_y + v_step * 0.5f;

        float digit_w = h_step * 0.16f;
        float digit_h = v_step * 0.35f;
        float gap = digit_w * 0.25f;

        float total_w = (float)digit_count * digit_w + (float)(digit_count - 1) * gap;
        float start_x = cell_left + h_step - total_w - h_step * 0.08f;
        float start_y = cell_top - v_step + v_step * 0.12f;

        for (int i = 0; i < digit_count; ++i) {
            append_digit(out_vertices, &count_vertices, max_vertices,
                         digits[i], start_x + i * (digit_w + gap), start_y, digit_w, digit_h);
        }
    }

    return count_vertices;
}