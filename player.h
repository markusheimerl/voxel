#ifndef PLAYER_H
#define PLAYER_H

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;    /* feet center */
    float velocity_y;
    bool on_ground;
} Player;

static void player_compute_aabb(Vec3 pos, AABB *aabb) {
    const float half_width = 0.4f;
    const float height = 1.8f;

    aabb->min = vec3(pos.x - half_width, pos.y, pos.z - half_width);
    aabb->max = vec3(pos.x + half_width, pos.y + height, pos.z + half_width);
}

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool hit;
    IVec3 cell;
    IVec3 normal;
    uint8_t type;
} RayHit;

static RayHit raycast_blocks(World *world, Vec3 origin, Vec3 direction, float max_distance) {
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

static void resolve_collision_axis(World *world, Vec3 *position, float delta, int axis) {
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

static void resolve_collision_y(World *world, Vec3 *position, float *velocity_y, bool *on_ground) {
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

static bool block_overlaps_player(const Player *player, IVec3 cell) {
    AABB player_box;
    player_compute_aabb(player->position, &player_box);

    AABB block_box = cell_aabb(cell);
    bool intersect =
        (player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
        (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
        (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z);

    return intersect;
}

#endif /* PLAYER_H */