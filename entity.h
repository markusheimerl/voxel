#ifndef ENTITY_H
#define ENTITY_H

#include <stdint.h>

#include "math.h"

/* -------------------------------------------------------------------------- */
/* Entity Types                                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
    ENTITY_ZOMBIE = 0
} EntityType;

typedef struct {
    Vec3 pos;
    EntityType type;
} Entity;

typedef struct {
    Vec3 pos;
    Vec3 scale;
    uint8_t type;
} RenderBlock;

/* -------------------------------------------------------------------------- */
/* Rendering Helpers                                                          */
/* -------------------------------------------------------------------------- */

uint32_t entity_render_block_count(const Entity *entity);
uint32_t entity_write_render_blocks(const Entity *entity, RenderBlock *out, uint32_t max);

#endif /* ENTITY_H */
