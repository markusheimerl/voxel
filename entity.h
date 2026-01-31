#ifndef ENTITY_H
#define ENTITY_H

#include <stdbool.h>
#include <stdint.h>

#include "math.h"

typedef struct World World;

/* -------------------------------------------------------------------------- */
/* Entity Types                                                               */
/* -------------------------------------------------------------------------- */

typedef enum {
    ENTITY_ZOMBIE
} EntityType;

typedef struct Entity {
    EntityType type;
    Vec3 position;
    float velocity_y;
    bool on_ground;

    /* Type-specific data */
    union {
        struct {
            Vec3 walk_direction;
            float yaw;
            float animation_time;
            float state_timer;
            bool is_walking;
        } zombie;
    } data;
} Entity;

/* -------------------------------------------------------------------------- */
/* Rendering Layout                                                            */
/* -------------------------------------------------------------------------- */

enum { ENTITY_INSTANCE_STRIDE_BYTES = sizeof(float) * 9 };

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

/* Create entities */
Entity entity_create_zombie(Vec3 position);

/* Update entity (AI, animation) */
void entity_update(Entity *entity, float delta_time);

/* Apply physics (gravity, collision) - called by World */
void entity_apply_physics(Entity *entity, World *world, float delta_time);

/* Get number of blocks needed to render this entity */
uint32_t entity_get_render_block_count(const Entity *entity);

/* Write render blocks to output buffer */
uint32_t entity_write_render_blocks(const Entity *entity, void *out_data,
                                    uint32_t offset, uint32_t max);

#endif /* ENTITY_H */
