#include "entity.h"
#include "world.h"

#include <math.h>
#include <string.h>

static const float ZOMBIE_GRAVITY = 17.0f;
static const float ZOMBIE_HALF_WIDTH = 0.25f;
static const float ZOMBIE_HEIGHT = 1.8f;

static void zombie_compute_aabb(Vec3 pos, AABB *aabb) {
    aabb->min = vec3(pos.x - ZOMBIE_HALF_WIDTH, pos.y, pos.z - ZOMBIE_HALF_WIDTH);
    aabb->max = vec3(pos.x + ZOMBIE_HALF_WIDTH, pos.y + ZOMBIE_HEIGHT, pos.z + ZOMBIE_HALF_WIDTH);
}

static uint32_t zombie_block_count(void) {
    return 6u;
}

static Vec3 rotate_yaw(Vec3 v, float yaw) {
    float c = cosf(yaw);
    float s = sinf(yaw);
    return vec3(v.x * c - v.z * s, v.y, v.x * s + v.z * c);
}

static uint32_t zombie_write_blocks(Vec3 pos, float time, float yaw, bool walking, RenderBlock *out, uint32_t max) {
    uint32_t count = 0;

    if (max == 0) return 0;

    const float base_x = pos.x;
    const float base_y = pos.y;
    const float base_z = pos.z;

    const uint8_t type = BLOCK_SAND;

    const float leg_h = 0.6f;
    const float torso_h = 0.8f;
    const float head_h = 0.4f;

    const float gait_speed = 4.0f;
    const float gait_angle = 0.6f;
    const float gait = walking ? sinf(time * gait_speed) * gait_angle : 0.0f;

    /* Legs (rotate around hip joint) */
    if (count < max) {
        Vec3 local = vec3(-0.13f, leg_h * 0.5f, 0.0f);
        Vec3 world = vec3(base_x, base_y, base_z);
        Vec3 offset = rotate_yaw(local, yaw);
        out[count++] = (RenderBlock){
            .pos = vec3(world.x + offset.x, world.y + offset.y, world.z + offset.z),
            .scale = vec3(0.25f, leg_h, 0.25f),
            .type = type,
            .rot_x = gait,
            .rot_y = yaw
        };
    }
    if (count < max) {
        Vec3 local = vec3(0.13f, leg_h * 0.5f, 0.0f);
        Vec3 world = vec3(base_x, base_y, base_z);
        Vec3 offset = rotate_yaw(local, yaw);
        out[count++] = (RenderBlock){
            .pos = vec3(world.x + offset.x, world.y + offset.y, world.z + offset.z),
            .scale = vec3(0.25f, leg_h, 0.25f),
            .type = type,
            .rot_x = -gait,
            .rot_y = yaw
        };
    }

    /* Torso (narrower and thinner) */
    if (count < max) {
        Vec3 local = vec3(0.0f, leg_h + torso_h * 0.5f, 0.0f);
        Vec3 world = vec3(base_x, base_y, base_z);
        Vec3 offset = rotate_yaw(local, yaw);
        out[count++] = (RenderBlock){
            .pos = vec3(world.x + offset.x, world.y + offset.y, world.z + offset.z),
            .scale = vec3(0.5f, torso_h, 0.35f),
            .type = type,
            .rot_x = 0.0f,
            .rot_y = yaw
        };
    }

    /* Arms (same cross-section as legs) */
    if (count < max) {
        Vec3 local = vec3(-0.275f, leg_h + torso_h * 0.88f, 0.35f);
        Vec3 world = vec3(base_x, base_y, base_z);
        Vec3 offset = rotate_yaw(local, yaw);
        out[count++] = (RenderBlock){
            .pos = vec3(world.x + offset.x, world.y + offset.y, world.z + offset.z),
            .scale = vec3(0.16f, 0.16f, 0.7f),
            .type = type,
            .rot_x = 0.0f,
            .rot_y = yaw
        };
    }
    if (count < max) {
        Vec3 local = vec3(0.275f, leg_h + torso_h * 0.88f, 0.35f);
        Vec3 world = vec3(base_x, base_y, base_z);
        Vec3 offset = rotate_yaw(local, yaw);
        out[count++] = (RenderBlock){
            .pos = vec3(world.x + offset.x, world.y + offset.y, world.z + offset.z),
            .scale = vec3(0.16f, 0.16f, 0.7f),
            .type = type,
            .rot_x = 0.0f,
            .rot_y = yaw
        };
    }

    /* Head (cube) */
    if (count < max) {
        Vec3 local = vec3(0.0f, leg_h + torso_h + head_h * 0.5f, 0.0f);
        Vec3 world = vec3(base_x, base_y, base_z);
        Vec3 offset = rotate_yaw(local, yaw);
        out[count++] = (RenderBlock){
            .pos = vec3(world.x + offset.x, world.y + offset.y, world.z + offset.z),
            .scale = vec3(0.4f, head_h, 0.4f),
            .type = type,
            .rot_x = 0.0f,
            .rot_y = yaw
        };
    }

    return count;
}

static bool zombie_support_height(World *world, Vec3 pos, float *out_support_y) {
    float support_y = -INFINITY;
    const float offsets[2] = {-ZOMBIE_HALF_WIDTH, ZOMBIE_HALF_WIDTH};

    for (int xi = 0; xi < 2; ++xi) {
        for (int zi = 0; zi < 2; ++zi) {
            Vec3 sample = vec3(pos.x + offsets[xi], pos.y - 0.51f, pos.z + offsets[zi]);
            IVec3 cell = world_to_cell(sample);

            uint8_t type;
            if (!world_get_block_type(world, cell, &type)) continue;
            if (type == BLOCK_WATER) continue;

            float top_y = (float)cell.y + 0.5f;
            if (top_y > support_y) support_y = top_y;
        }
    }

    if (support_y > -INFINITY) {
        if (out_support_y) *out_support_y = support_y;
        return true;
    }

    return false;
}

static void zombie_resolve_collision_xz(World *world, Vec3 *pos) {
    if (!world || !pos) return;

    for (int iter = 0; iter < 4; ++iter) {
        bool resolved_any = false;
        AABB zombie_box;
        zombie_compute_aabb(*pos, &zombie_box);

        int min_x = (int)floorf(zombie_box.min.x - 0.5f);
        int max_x = (int)floorf(zombie_box.max.x + 0.5f);
        int min_y = (int)floorf(zombie_box.min.y - 0.5f);
        int max_y = (int)floorf(zombie_box.max.y + 0.5f);
        int min_z = (int)floorf(zombie_box.min.z - 0.5f);
        int max_z = (int)floorf(zombie_box.max.z + 0.5f);

        for (int x = min_x; x <= max_x; ++x) {
            for (int y = min_y; y <= max_y; ++y) {
                if (!world_y_in_bounds(y)) continue;
                for (int z = min_z; z <= max_z; ++z) {
                    uint8_t type;
                    if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                    if (type == BLOCK_WATER) continue;

                    zombie_compute_aabb(*pos, &zombie_box);
                    AABB block_box = cell_aabb((IVec3){x, y, z});

                    if (!((zombie_box.min.x < block_box.max.x && zombie_box.max.x > block_box.min.x) &&
                          (zombie_box.min.y < block_box.max.y && zombie_box.max.y > block_box.min.y) &&
                          (zombie_box.min.z < block_box.max.z && zombie_box.max.z > block_box.min.z))) {
                        continue;
                    }

                    float overlap_x = fminf(zombie_box.max.x, block_box.max.x) - fmaxf(zombie_box.min.x, block_box.min.x);
                    float overlap_z = fminf(zombie_box.max.z, block_box.max.z) - fmaxf(zombie_box.min.z, block_box.min.z);

                    float block_center_x = (block_box.min.x + block_box.max.x) * 0.5f;
                    float block_center_z = (block_box.min.z + block_box.max.z) * 0.5f;

                    if (overlap_x < overlap_z) {
                        if (pos->x < block_center_x) pos->x -= overlap_x + 0.001f;
                        else pos->x += overlap_x + 0.001f;
                    } else {
                        if (pos->z < block_center_z) pos->z -= overlap_z + 0.001f;
                        else pos->z += overlap_z + 0.001f;
                    }

                    resolved_any = true;
                }
            }
        }

        if (!resolved_any) break;
    }
}

void entity_apply_physics(Entity *entity, World *world, float delta_time) {
    if (!entity || !world) return;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
    default:
        break;
    }

    float support_y = 0.0f;
    bool has_support = zombie_support_height(world, entity->pos, &support_y);

    if (!has_support || entity->pos.y > support_y + 0.001f) {
        entity->on_ground = false;
        entity->velocity_y -= ZOMBIE_GRAVITY * delta_time;
        entity->pos.y += entity->velocity_y * delta_time;
    } else {
        entity->on_ground = true;
        entity->velocity_y = 0.0f;
        entity->pos.y = support_y;
    }

    has_support = zombie_support_height(world, entity->pos, &support_y);
    if (has_support && entity->velocity_y <= 0.0f && entity->pos.y < support_y) {
        entity->pos.y = support_y;
        entity->velocity_y = 0.0f;
        entity->on_ground = true;
    }

    zombie_resolve_collision_xz(world, &entity->pos);
}

uint32_t entity_render_block_count(const Entity *entity) {
    if (!entity) return 0;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
    default:
        return zombie_block_count();
    }
}

uint32_t entity_write_render_blocks(const Entity *entity, float time, RenderBlock *out, uint32_t max) {
    if (!entity || !out || max == 0) return 0;
    (void)time;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
    default:
        return zombie_write_blocks(entity->pos, entity->anim_time, entity->yaw, entity->is_walking, out, max);
    }
}
