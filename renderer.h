#ifndef VOXEL_H
#define VOXEL_H

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

Renderer *renderer_create(void *display,
                                     unsigned long window,
                                     uint32_t framebuffer_width,
                                     uint32_t framebuffer_height);
void renderer_destroy(Renderer *renderer);
void renderer_request_resize(Renderer *renderer,
                                   uint32_t framebuffer_width,
                                   uint32_t framebuffer_height);
bool renderer_draw_frame(Renderer *renderer,
                               World *world,
                               const Player *player,
                               Camera *camera,
                               bool highlight,
                               IVec3 highlight_cell);

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

typedef struct {
    Mat4 view;
    Mat4 proj;
} PushConstants;

static const Vertex BLOCK_VERTICES[] = {
    /* Front */
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},

    /* Back */
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}},
    {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}},

    /* Top */
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},

    /* Bottom */
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}},

    /* Right */
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}},
    {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}},
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f}},

    /* Left */
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}},
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f}},
};

static const uint16_t BLOCK_INDICES[] = {
     0,  1,  2,  2,  3,  0, /* Front */
     6,  5,  4,  4,  7,  6, /* Back */
     8, 11, 10, 10,  9,  8, /* Top */
    12, 13, 14, 14, 15, 12, /* Bottom */
    16, 17, 18, 18, 19, 16, /* Right */
    22, 21, 20, 20, 23, 22  /* Left */
};

static const Vertex EDGE_VERTICES[] = {
    {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}}, /* 0 */
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}}, /* 1 */
    {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}}, /* 2 */
    {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}}, /* 3 */
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}, /* 4 */
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}}, /* 5 */
    {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}}, /* 6 */
    {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f}}, /* 7 */
};

static const uint16_t EDGE_INDICES[] = {
    0, 1,  1, 2,  2, 3,  3, 0, /* Front */
    4, 5,  5, 6,  6, 7,  7, 4, /* Back */
    0, 4,  1, 5,  2, 6,  3, 7  /* Connecting edges */
};


#endif /* VOXEL_H */