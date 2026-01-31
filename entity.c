#include "entity.h"
#include "world.h"

#include <math.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define ZOMBIE_GRAVITY 17.0f
#define ZOMBIE_HALF_WIDTH 0.25f
#define ZOMBIE_HEIGHT 1.8f
#define ZOMBIE_WALK_SPEED 1.0f

/* -------------------------------------------------------------------------- */
/* Zombie Helpers                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;
    Vec3 scale;
    uint32_t type;
    float rot_x;
    float rot_y;
} EntityRenderBlock;

static void zombie_compute_aabb(Vec3 pos, AABB *aabb) {
    aabb->min = vec3(pos.x - ZOMBIE_HALF_WIDTH, pos.y, pos.z - ZOMBIE_HALF_WIDTH);
    aabb->max = vec3(pos.x + ZOMBIE_HALF_WIDTH, pos.y + ZOMBIE_HEIGHT, pos.z + ZOMBIE_HALF_WIDTH);
}

static Vec3 rotate_around_y(Vec3 v, float yaw) {
    float c = cosf(yaw);
    float s = sinf(yaw);
    return vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}

static float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

static float randf_range(float min, float max) {
    return min + randf() * (max - min);
}

static bool zombie_find_ground(World *world, Vec3 pos, float *out_ground_y) {
    float ground_y = -INFINITY;
    const float offsets[2] = {-ZOMBIE_HALF_WIDTH, ZOMBIE_HALF_WIDTH};

    for (int xi = 0; xi < 2; ++xi) {
        for (int zi = 0; zi < 2; ++zi) {
            Vec3 test = vec3(pos.x + offsets[xi], pos.y - 0.51f, pos.z + offsets[zi]);
            IVec3 cell = world_to_cell(test);

            uint8_t type;
            if (!world_get_block_type(world, cell, &type)) continue;
            if (type == BLOCK_WATER) continue;

            float top = (float)cell.y + 0.5f;
            if (top > ground_y) ground_y = top;
        }
    }

    if (ground_y > -INFINITY) {
        *out_ground_y = ground_y;
        return true;
    }
    return false;
}

static void zombie_resolve_horizontal_collision(World *world, Vec3 *pos) {
    for (int iter = 0; iter < 4; ++iter) {
        AABB zombie_aabb;
        zombie_compute_aabb(*pos, &zombie_aabb);

        int min_x = (int)floorf(zombie_aabb.min.x - 0.5f);
        int max_x = (int)floorf(zombie_aabb.max.x + 0.5f);
        int min_y = (int)floorf(zombie_aabb.min.y - 0.5f);
        int max_y = (int)floorf(zombie_aabb.max.y + 0.5f);
        int min_z = (int)floorf(zombie_aabb.min.z - 0.5f);
        int max_z = (int)floorf(zombie_aabb.max.z + 0.5f);

        bool resolved_any = false;

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                if (!world_y_in_bounds(y)) continue;
                for (int z = min_z; z <= max_z; ++z) {
                    uint8_t type;
                    if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                    if (type == BLOCK_WATER) continue;

                    AABB block_aabb = cell_aabb((IVec3){x, y, z});

                    zombie_compute_aabb(*pos, &zombie_aabb);
                    if (!(zombie_aabb.min.x < block_aabb.max.x && zombie_aabb.max.x > block_aabb.min.x &&
                          zombie_aabb.min.y < block_aabb.max.y && zombie_aabb.max.y > block_aabb.min.y &&
                          zombie_aabb.min.z < block_aabb.max.z && zombie_aabb.max.z > block_aabb.min.z)) {
                        continue;
                    }

                    float overlap_x = fminf(zombie_aabb.max.x, block_aabb.max.x) -
                                     fmaxf(zombie_aabb.min.x, block_aabb.min.x);
                    float overlap_z = fminf(zombie_aabb.max.z, block_aabb.max.z) -
                                     fmaxf(zombie_aabb.min.z, block_aabb.min.z);

                    float block_cx = (block_aabb.min.x + block_aabb.max.x) * 0.5f;
                    float block_cz = (block_aabb.min.z + block_aabb.max.z) * 0.5f;

                    if (overlap_x < overlap_z) {
                        pos->x += (pos->x < block_cx) ? -overlap_x - 0.001f : overlap_x + 0.001f;
                    } else {
                        pos->z += (pos->z < block_cz) ? -overlap_z - 0.001f : overlap_z + 0.001f;
                    }

                    resolved_any = true;
                }
            }
        }

        if (!resolved_any) break;
    }
}

static void zombie_update_ai(Entity *entity, float delta_time) {
    const float WALK_TIME_MIN = 1.2f, WALK_TIME_MAX = 3.0f;
    const float IDLE_TIME_MIN = 0.8f, IDLE_TIME_MAX = 2.0f;
    const float TURN_DEG_MIN = 30.0f, TURN_DEG_MAX = 180.0f;
    const float TURN_SPEED_MIN = 0.7f, TURN_SPEED_MAX = 1.8f;
    const float TURN_CHAIN_CHANCE = 0.5f;
    const int TURN_CHAIN_EXTRA_MIN = 1;
    const int TURN_CHAIN_EXTRA_MAX = 3;

    if (entity->data.zombie.is_walking) {
        entity->data.zombie.state_timer -= delta_time;
        entity->position.x += entity->data.zombie.walk_direction.x * ZOMBIE_WALK_SPEED * delta_time;
        entity->position.z += entity->data.zombie.walk_direction.z * ZOMBIE_WALK_SPEED * delta_time;
        entity->data.zombie.animation_time += delta_time;

        if (entity->data.zombie.state_timer <= 0.0f) {
            entity->data.zombie.is_walking = false;
            entity->data.zombie.is_turning = false;
            int extra = 0;
            if (randf() < TURN_CHAIN_CHANCE) {
                extra = TURN_CHAIN_EXTRA_MIN + (int)floorf(randf_range(0.0f, (float)(TURN_CHAIN_EXTRA_MAX - TURN_CHAIN_EXTRA_MIN + 1)));
            }
            entity->data.zombie.turn_chain_remaining = 1 + extra;
            entity->data.zombie.state_timer = randf_range(IDLE_TIME_MIN, IDLE_TIME_MAX);
        }
    } else if (entity->data.zombie.is_turning) {
        float remaining = entity->data.zombie.turn_remaining;
        float step = entity->data.zombie.turn_speed * delta_time;
        float direction = (remaining >= 0.0f) ? 1.0f : -1.0f;

        if (fabsf(remaining) <= step) {
            step = fabsf(remaining);
        }

        entity->data.zombie.yaw += step * direction;
        entity->data.zombie.turn_remaining -= step * direction;

        if (fabsf(entity->data.zombie.turn_remaining) <= 0.0001f) {
            entity->data.zombie.turn_remaining = 0.0f;
            entity->data.zombie.is_turning = false;
            entity->data.zombie.state_timer = randf_range(IDLE_TIME_MIN, IDLE_TIME_MAX);
            if (entity->data.zombie.turn_chain_remaining > 0) {
                entity->data.zombie.turn_chain_remaining -= 1;
            }
        }
    } else {
        entity->data.zombie.state_timer -= delta_time;
        if (entity->data.zombie.state_timer <= 0.0f) {
            if (entity->data.zombie.turn_chain_remaining > 0) {
                entity->data.zombie.is_turning = true;
                float turn_deg = randf_range(TURN_DEG_MIN, TURN_DEG_MAX);
                float turn_sign = (randf() < 0.5f) ? -1.0f : 1.0f;
                entity->data.zombie.turn_remaining = (turn_deg * (float)M_PI / 180.0f) * turn_sign;
                entity->data.zombie.turn_speed = randf_range(TURN_SPEED_MIN, TURN_SPEED_MAX);
            } else {
                float angle = entity->data.zombie.yaw + (float)M_PI / 2.0f;
                entity->data.zombie.walk_direction = vec3(cosf(angle), 0.0f, sinf(angle));
                entity->data.zombie.is_walking = true;
                entity->data.zombie.state_timer = randf_range(WALK_TIME_MIN, WALK_TIME_MAX);
                entity->data.zombie.animation_time = 0.0f;
            }
        }
    }
}

static uint32_t zombie_get_render_blocks(const Entity *entity, EntityRenderBlock *out, uint32_t max) {
    if (max < 6) return 0;

    const float LEG_H = 0.6f, TORSO_H = 0.8f, HEAD_H = 0.4f;
    const float GAIT_SPEED = 4.0f, GAIT_ANGLE = 0.6f;

    float gait = entity->data.zombie.is_walking ?
        sinf(entity->data.zombie.animation_time * GAIT_SPEED) * GAIT_ANGLE : 0.0f;
    float yaw = entity->data.zombie.yaw;
    Vec3 base = entity->position;

    uint32_t idx = 0;

    /* Left leg (hip pivot) */
    Vec3 local = vec3(-0.13f, LEG_H * 0.5f, 0.0f);
    Vec3 offset = rotate_around_y(local, yaw);
    out[idx++] = (EntityRenderBlock){
        vec3_add(base, offset), vec3(0.25f, LEG_H, 0.25f), BLOCK_SAND, gait, yaw
    };

    /* Right leg (hip pivot) */
    local = vec3(0.13f, LEG_H * 0.5f, 0.0f);
    offset = rotate_around_y(local, yaw);
    out[idx++] = (EntityRenderBlock){
        vec3_add(base, offset), vec3(0.25f, LEG_H, 0.25f), BLOCK_SAND, -gait, yaw
    };

    /* Torso */
    local = vec3(0.0f, LEG_H + TORSO_H * 0.5f, 0.0f);
    offset = rotate_around_y(local, yaw);
    out[idx++] = (EntityRenderBlock){
        vec3_add(base, offset), vec3(0.5f, TORSO_H, 0.35f), BLOCK_SAND, 0.0f, yaw
    };

    /* Left arm */
    local = vec3(-0.275f, LEG_H + TORSO_H * 0.88f, 0.35f);
    offset = rotate_around_y(local, yaw);
    out[idx++] = (EntityRenderBlock){
        vec3_add(base, offset), vec3(0.16f, 0.16f, 0.7f), BLOCK_SAND, 0.0f, yaw
    };

    /* Right arm */
    local = vec3(0.275f, LEG_H + TORSO_H * 0.88f, 0.35f);
    offset = rotate_around_y(local, yaw);
    out[idx++] = (EntityRenderBlock){
        vec3_add(base, offset), vec3(0.16f, 0.16f, 0.7f), BLOCK_SAND, 0.0f, yaw
    };

    /* Head */
    local = vec3(0.0f, LEG_H + TORSO_H + HEAD_H * 0.5f, 0.0f);
    offset = rotate_around_y(local, yaw);
    out[idx++] = (EntityRenderBlock){
        vec3_add(base, offset), vec3(0.4f, HEAD_H, 0.4f), BLOCK_SAND, 0.0f, yaw
    };

    return idx;
}

/* -------------------------------------------------------------------------- */
/* Public API Implementation                                                  */
/* -------------------------------------------------------------------------- */

Entity entity_create_zombie(Vec3 position) {
    Entity entity = {0};
    entity.type = ENTITY_ZOMBIE;
    entity.position = position;
    entity.velocity_y = 0.0f;
    entity.on_ground = false;
    entity.data.zombie.walk_direction = vec3(1.0f, 0.0f, 0.0f);
    entity.data.zombie.yaw = -(float)M_PI / 2.0f;
    entity.data.zombie.animation_time = 0.0f;
    entity.data.zombie.state_timer = 1.5f + ((float)rand() / (float)RAND_MAX) * 2.0f;
    entity.data.zombie.is_walking = true;
    entity.data.zombie.is_turning = false;
    entity.data.zombie.turn_remaining = 0.0f;
    entity.data.zombie.turn_speed = 0.0f;
    entity.data.zombie.turn_chain_remaining = 0;
    return entity;
}

void entity_update(Entity *entity, float delta_time) {
    if (!entity) return;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
        zombie_update_ai(entity, delta_time);
        break;
    }
}

void entity_apply_physics(Entity *entity, World *world, float delta_time) {
    if (!entity || !world) return;

    /* Apply gravity and vertical movement */
    float ground_y;
    bool has_ground = zombie_find_ground(world, entity->position, &ground_y);

    if (!has_ground || entity->position.y > ground_y + 0.001f) {
        entity->on_ground = false;
        entity->velocity_y -= ZOMBIE_GRAVITY * delta_time;
        entity->position.y += entity->velocity_y * delta_time;
    } else {
        entity->on_ground = true;
        entity->velocity_y = 0.0f;
        entity->position.y = ground_y;
    }

    /* Snap to ground if falling through */
    has_ground = zombie_find_ground(world, entity->position, &ground_y);
    if (has_ground && entity->velocity_y <= 0.0f && entity->position.y < ground_y) {
        entity->position.y = ground_y;
        entity->velocity_y = 0.0f;
        entity->on_ground = true;
    }

    /* Resolve horizontal collisions */
    zombie_resolve_horizontal_collision(world, &entity->position);
}

uint32_t entity_get_render_block_count(const Entity *entity) {
    if (!entity) return 0;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
        return 6;
    default:
        return 0;
    }
}

uint32_t entity_write_render_blocks(const Entity *entity, void *out_data,
                                    uint32_t offset, uint32_t max) {
    if (!entity || !out_data) return 0;

    EntityRenderBlock blocks[6];
    uint32_t count = 0;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
        count = zombie_get_render_blocks(entity, blocks, 6);
        break;
    }

    /* Copy to output buffer at offset */
    uint8_t *dest = (uint8_t *)out_data + offset;
    for (uint32_t i = 0; i < count && i < max; ++i) {
        /* Format: x, y, z, type, sx, sy, sz, rot_x, rot_y */
        float *f = (float *)dest;
        f[0] = blocks[i].position.x;
        f[1] = blocks[i].position.y;
        f[2] = blocks[i].position.z;
        ((uint32_t *)dest)[3] = blocks[i].type;
        f[4] = blocks[i].scale.x;
        f[5] = blocks[i].scale.y;
        f[6] = blocks[i].scale.z;
        f[7] = blocks[i].rot_x;
        f[8] = blocks[i].rot_y;
        dest += ENTITY_INSTANCE_STRIDE_BYTES;
    }

    return count;
}
