#include "entity.h"
#include "world.h"

#include <math.h>
#include <string.h>

static uint32_t zombie_block_count(void) {
    return 6u;
}

static uint32_t zombie_write_blocks(Vec3 pos, RenderBlock *out, uint32_t max) {
    uint32_t count = 0;

    if (max == 0) return 0;

    const float base_x = pos.x;
    const float base_y = pos.y;
    const float base_z = pos.z;

    const uint8_t type = BLOCK_SAND;

    const float leg_h = 0.6f;
    const float torso_h = 0.8f;
    const float head_h = 0.4f;

    /* Legs (square cross-section) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x - 0.2f, base_y + leg_h * 0.5f, base_z),
            .scale = vec3(0.25f, leg_h, 0.25f),
            .type = type
        };
    }
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x + 0.2f, base_y + leg_h * 0.5f, base_z),
            .scale = vec3(0.25f, leg_h, 0.25f),
            .type = type
        };
    }

    /* Torso (narrower and thinner) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x, base_y + leg_h + torso_h * 0.5f, base_z),
            .scale = vec3(0.5f, torso_h, 0.35f),
            .type = type
        };
    }

    /* Arms (same cross-section as legs) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x - 0.275f, base_y + leg_h + torso_h * 0.5f, base_z),
            .scale = vec3(0.25f, 0.7f, 0.25f),
            .type = type
        };
    }
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x + 0.275f, base_y + leg_h + torso_h * 0.5f, base_z),
            .scale = vec3(0.25f, 0.7f, 0.25f),
            .type = type
        };
    }

    /* Head (cube) */
    if (count < max) {
        out[count++] = (RenderBlock){
            .pos = vec3(base_x, base_y + leg_h + torso_h + head_h * 0.5f, base_z),
            .scale = vec3(0.4f, head_h, 0.4f),
            .type = type
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

uint32_t entity_write_render_blocks(const Entity *entity, RenderBlock *out, uint32_t max) {
    if (!entity || !out || max == 0) return 0;

    switch (entity->type) {
    case ENTITY_ZOMBIE:
    default:
        return zombie_write_blocks(entity->pos, out, max);
    }
}
