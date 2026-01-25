#include "player.h"
#include <math.h>

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

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
    for (int i = 0; i < 9; ++i) {
        if (player->inventory_counts[i] > 0 && player->inventory[i] == type) {
            if (player->inventory_counts[i] < UINT8_MAX) {
                player->inventory_counts[i]++;
            }
            return;
        }
    }

    for (int i = 0; i < 9; ++i) {
        if (player->inventory_counts[i] == 0) {
            player->inventory[i] = type;
            player->inventory_counts[i] = 1;
            return;
        }
    }
}

static void append_line(Vertex *verts, uint32_t *count, uint32_t max,
                        float x0, float y0, float x1, float y1) {
    if (*count + 2 > max) return;
    verts[(*count)++] = (Vertex){{x0, y0, 0.0f}, {0.0f, 0.0f}};
    verts[(*count)++] = (Vertex){{x1, y1, 0.0f}, {0.0f, 0.0f}};
}

int player_inventory_slot_from_mouse(float aspect,
                                     float mouse_x,
                                     float mouse_y,
                                     float window_w,
                                     float window_h) {
    if (window_w <= 0.0f || window_h <= 0.0f) return -1;

    float ndc_x = (mouse_x / window_w) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (mouse_y / window_h) * 2.0f;

    const float inv_half = 0.6f;
    const float inv_half_x = inv_half * aspect;
    const float left = -inv_half_x;
    const float right = inv_half_x;
    const float bottom = -inv_half;
    const float top = inv_half;

    if (ndc_x < left || ndc_x > right || ndc_y < bottom || ndc_y > top) return -1;

    const float h_step = (right - left) / 3.0f;
    const float v_step = (top - bottom) / 3.0f;

    int col = (int)((ndc_x - left) / h_step);
    int row = (int)((top - ndc_y) / v_step);

    if (col < 0 || col > 2 || row < 0 || row > 2) return -1;

    return row * 3 + col;
}

void player_inventory_selection_vertices(int slot,
                                         float aspect,
                                         Vertex *out_vertices,
                                         uint32_t max_vertices,
                                         uint32_t *out_count) {
    if (!out_vertices || !out_count || max_vertices == 0) return;
    *out_count = 0;

    if (slot < 0 || slot > 8) return;

    const float inv_half = 0.6f;
    const float inv_half_x = inv_half * aspect;
    const float left = -inv_half_x;
    const float right = inv_half_x;
    const float bottom = -inv_half;
    const float top = inv_half;
    const float h_step = (right - left) / 3.0f;
    const float v_step = (top - bottom) / 3.0f;

    int row = slot / 3;
    int col = slot % 3;

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

    const float inv_half = 0.6f;
    const float inv_half_x = inv_half * aspect;
    const float left = -inv_half_x;
    const float right = inv_half_x;
    const float bottom = -inv_half;
    const float top = inv_half;
    const float h_step = (right - left) / 3.0f;
    const float v_step = (top - bottom) / 3.0f;

    Vertex inventory_vertices[16] = {
        {{left,  bottom, 0.0f}, {0.0f, 0.0f}}, {{right, bottom, 0.0f}, {0.0f, 0.0f}},
        {{right, bottom, 0.0f}, {0.0f, 0.0f}}, {{right, top,    0.0f}, {0.0f, 0.0f}},
        {{right, top,    0.0f}, {0.0f, 0.0f}}, {{left,  top,    0.0f}, {0.0f, 0.0f}},
        {{left,  top,    0.0f}, {0.0f, 0.0f}}, {{left,  bottom, 0.0f}, {0.0f, 0.0f}},

        {{left + h_step, bottom, 0.0f}, {0.0f, 0.0f}}, {{left + h_step, top, 0.0f}, {0.0f, 0.0f}},
        {{left + 2.0f * h_step, bottom, 0.0f}, {0.0f, 0.0f}}, {{left + 2.0f * h_step, top, 0.0f}, {0.0f, 0.0f}},

        {{left, bottom + v_step, 0.0f}, {0.0f, 0.0f}}, {{right, bottom + v_step, 0.0f}, {0.0f, 0.0f}},
        {{left, bottom + 2.0f * v_step, 0.0f}, {0.0f, 0.0f}}, {{right, bottom + 2.0f * v_step, 0.0f}, {0.0f, 0.0f}}
    };

    uint32_t count = (uint32_t)(sizeof(inventory_vertices) / sizeof(inventory_vertices[0]));
    if (count > max_vertices) count = max_vertices;
    for (uint32_t i = 0; i < count; ++i) out_vertices[i] = inventory_vertices[i];
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

    const float inv_half = 0.6f;
    const float inv_half_x = inv_half * aspect;
    const float left = -inv_half_x;
    const float right = inv_half_x;
    const float bottom = -inv_half;
    const float top = inv_half;
    const float h_step = (right - left) / 3.0f;
    const float v_step = (top - bottom) / 3.0f;

    uint32_t icon_index = 0;
    for (int slot = 0; slot < 9; ++slot) {
        if (player->inventory_counts[slot] == 0) continue;
        if (out_instances && icon_index < max_instances) {
            int row = slot / 3;
            int col = slot % 3;
            float center_x = left + h_step * 0.5f + (float)col * h_step;
            float center_y = top - v_step * 0.5f - (float)row * v_step;
            out_instances[icon_index] = (InstanceData){
                center_x, center_y, 0.0f, (uint32_t)player->inventory[slot]
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

    const float inv_half = 0.6f;
    const float inv_half_x = inv_half * aspect;
    const float left = -inv_half_x;
    const float right = inv_half_x;
    const float bottom = -inv_half;
    const float top = inv_half;
    const float h_step = (right - left) / 3.0f;
    const float v_step = (top - bottom) / 3.0f;

    uint32_t count_vertices = 0;

    for (int slot = 0; slot < 9; ++slot) {
        if (player->inventory_counts[slot] == 0) continue;

        int row = slot / 3;
        int col = slot % 3;
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

    return count_vertices;
}