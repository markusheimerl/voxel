#ifndef ENTITY_H
#define ENTITY_H

#include <stdbool.h>
#include <stdint.h>

#include "math.h"

/* -------------------------------------------------------------------------- */
/* Entity Types                                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
    ENTITY_ZOMBIE = 0
} EntityType;

typedef struct World World;

typedef struct {
    Vec3 pos;
    float velocity_y;
    bool on_ground;
    EntityType type;
} Entity;

typedef struct {
    Vec3 pos;
    Vec3 scale;
    uint8_t type;
    float rot_x;
} RenderBlock;

/* -------------------------------------------------------------------------- */
/* Rendering Helpers                                                          */
/* -------------------------------------------------------------------------- */

uint32_t entity_render_block_count(const Entity *entity);
uint32_t entity_write_render_blocks(const Entity *entity, float time, RenderBlock *out, uint32_t max);
void entity_apply_physics(Entity *entity, World *world, float delta_time);

#endif /* ENTITY_H */
