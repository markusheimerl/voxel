#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "math.h"
#include "world.h"
#include "voxel.h"

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;    /* feet center */
    float velocity_y;
    bool on_ground;
    bool inventory_open;
    uint8_t inventory[9];
    uint8_t inventory_counts[9];
} Player;

void player_inventory_add(Player *player, uint8_t type);

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

/* -------------------------------------------------------------------------- */
/* Inventory Helpers                                                          */
/* -------------------------------------------------------------------------- */

void player_inventory_add(Player *player, uint8_t type);

void player_inventory_grid_vertices(float aspect,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count,
                                    float *out_h_step,
                                    float *out_v_step);

void player_inventory_icon_vertices(float h_step,
                                    float v_step,
                                    Vertex *out_vertices,
                                    uint32_t max_vertices,
                                    uint32_t *out_count);

uint32_t player_inventory_icon_instances(const Player *player,
                                         float aspect,
                                         InstanceData *out_instances,
                                         uint32_t max_instances);

uint32_t player_inventory_count_vertices(const Player *player,
                                         float aspect,
                                         Vertex *out_vertices,
                                         uint32_t max_vertices);

#endif /* PLAYER_H */