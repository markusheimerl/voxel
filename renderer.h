#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math.h"
#include "world.h"

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

typedef struct Renderer Renderer;
typedef struct Player Player;
typedef struct Camera Camera;

void die(const char *message);

Renderer *renderer_create(void *display, unsigned long window, uint32_t framebuffer_width, uint32_t framebuffer_height);
void renderer_destroy(Renderer *renderer);
void renderer_draw_frame(Renderer *renderer, World *world, const Player *player, Camera *camera, bool highlight, IVec3 highlight_cell);

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
} InstanceData;


#endif /* RENDERER_H */