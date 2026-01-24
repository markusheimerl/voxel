#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "math.h"
#include "world.h"

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;    /* feet center */
    float velocity_y;
    bool on_ground;
} Player;

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool hit;
    IVec3 cell;
    IVec3 normal;
    uint8_t type;
} RayHit;

RayHit raycast_blocks(World *world, Vec3 origin, Vec3 direction, float max_distance);

/* -------------------------------------------------------------------------- */
/* Player Collision Helpers                                                   */
/* -------------------------------------------------------------------------- */

void resolve_collision_axis(World *world, Vec3 *position, float delta, int axis);
void resolve_collision_y(World *world, Vec3 *position, float *velocity_y, bool *on_ground);
bool block_overlaps_player(const Player *player, IVec3 cell);

#endif /* PLAYER_H */