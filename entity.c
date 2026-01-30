#include "entity.h"
#include "world.h"

#include <math.h>
#include <string.h>

static uint32_t zombie_block_count(void) {
    return 6u;
}

static uint32_t zombie_write_blocks(Vec3 pos, float time, RenderBlock *out, uint32_t max) {
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
    const float gait = sinf(time * gait_speed) * gait_angle;

    /* Legs (rotate around hip joint) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x - 0.13f, base_y + leg_h * 0.5f, base_z),
            .scale = vec3(0.25f, leg_h, 0.25f),
            .type = type,
            .rot_x = gait
        };
    }
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x + 0.13f, base_y + leg_h * 0.5f, base_z),
            .scale = vec3(0.25f, leg_h, 0.25f),
            .type = type,
            .rot_x = -gait
        };
    }

    /* Torso (narrower and thinner) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x, base_y + leg_h + torso_h * 0.5f, base_z),
            .scale = vec3(0.5f, torso_h, 0.35f),
            .type = type,
            .rot_x = 0.0f
        };
    }

    /* Arms (same cross-section as legs) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x - 0.275f, base_y + leg_h + torso_h * 0.88f, base_z + 0.35f),
            .scale = vec3(0.16f, 0.16f, 0.7f),
            .type = type,
            .rot_x = 0.0f
        };
    }
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x + 0.275f, base_y + leg_h + torso_h * 0.88f, base_z + 0.35f),
            .scale = vec3(0.16f, 0.16f, 0.7f),
            .type = type,
            .rot_x = 0.0f
        };
    }

    /* Head (cube) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x, base_y + leg_h + torso_h + head_h * 0.5f, base_z),
            .scale = vec3(0.4f, head_h, 0.4f),
            .type = type,
            .rot_x = 0.0f
        };
    }

    return count;
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

    switch (entity->type) {
    case ENTITY_ZOMBIE:
    default:
        return zombie_write_blocks(entity->pos, time, out, max);
    }
}
