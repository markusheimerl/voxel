#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math.h"
#include "world.h"

typedef struct Renderer Renderer;
typedef struct Player Player;
typedef struct Camera Camera;

/* -------------------------------------------------------------------------- */
/* Vertex Formats                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 pos;
    Vec2 uv;
} Vertex;

typedef struct {
    float x, y, z;
    uint32_t type;
    float sx, sy, sz;
    float rot_x;
    float rot_y;
} InstanceData;

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

Renderer *renderer_create(void *display, unsigned long window, uint32_t width, uint32_t height);
void renderer_destroy(Renderer *renderer);
void renderer_resize(Renderer *renderer, uint32_t width, uint32_t height);
void renderer_draw_frame(Renderer *renderer, World *world, const Player *player, Camera *camera,
                         bool highlight, IVec3 highlight_cell);

#endif /* RENDERER_H */