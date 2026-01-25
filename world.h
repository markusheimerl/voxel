#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math.h"

/* -------------------------------------------------------------------------- */
/* Blocks                                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    BLOCK_DIRT  = 0,
    BLOCK_STONE = 1,
    BLOCK_GRASS = 2,
    BLOCK_SAND  = 3,
    BLOCK_WATER = 4,
    BLOCK_WOOD  = 5,
    BLOCK_LEAVES = 6,
    BLOCK_TYPE_COUNT
} BlockType;

enum {
    HIGHLIGHT_TEXTURE_INDEX = BLOCK_STONE,
    CROSSHAIR_TEXTURE_INDEX = BLOCK_TYPE_COUNT,
    INVENTORY_SELECTION_TEXTURE_INDEX = BLOCK_TYPE_COUNT + 1,
    INVENTORY_BG_TEXTURE_INDEX = BLOCK_TYPE_COUNT + 2
};

typedef struct {
    IVec3 pos;
    uint8_t type;
} Block;

typedef struct {
    Vec3 min;
    Vec3 max;
} AABB;

/* -------------------------------------------------------------------------- */
/* World / Chunk Configuration                                                */
/* -------------------------------------------------------------------------- */

#define CHUNK_SIZE 16
#define WORLD_MIN_Y (-8)
#define WORLD_MAX_Y 32
#define CHUNK_HEIGHT (WORLD_MAX_Y - WORLD_MIN_Y + 1)

#define ACTIVE_CHUNK_RADIUS 6
#define CHUNK_UNLOAD_MARGIN 2

#define MAX_LOADED_CHUNKS ((uint32_t)(((ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) * 2 + 1) * ((ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) * 2 + 1)))

#define WORLD_SAVE_FILE "world.vox"
#define WORLD_SAVE_MAGIC 0x58574F56u /* 'VOWX' little-endian (VOXW as bytes) */
#define WORLD_SAVE_VERSION 1u

#define INITIAL_INSTANCE_CAPACITY 200000u
#define MAX_INSTANCE_CAPACITY     1500000u

/* -------------------------------------------------------------------------- */
/* World Save (single file)                                                   */
/* -------------------------------------------------------------------------- */

typedef struct {
    int32_t cx;
    int32_t cz;
    uint8_t *voxels; /* size = CHUNK_SIZE*CHUNK_HEIGHT*CHUNK_SIZE */
} ChunkRecord;

typedef struct {
    ChunkRecord *records;
    int count;
    int capacity;
    bool dirty;
    char path[256];
} WorldSave;

/* -------------------------------------------------------------------------- */
/* World / Chunk Structures                                                   */
/* -------------------------------------------------------------------------- */

typedef struct Chunk Chunk;
typedef struct World World;

struct Chunk {
    int cx;
    int cz;
    uint8_t *voxels;

    Block *blocks;
    int block_count;
    int block_capacity;

    bool dirty;
    bool render_dirty;
};

struct World {
    Chunk **chunks;
    int chunk_count;
    int chunk_capacity;
    Vec3 spawn_position;
    bool spawn_set;
    WorldSave *save;
};

/* -------------------------------------------------------------------------- */
/* API                                                                        */
/* -------------------------------------------------------------------------- */

AABB cell_aabb(IVec3 cell);
bool world_y_in_bounds(int y);

IVec3 world_to_cell(Vec3 p);

void world_save_init(WorldSave *save, const char *path);
bool world_save_load(WorldSave *save);
void world_save_flush(WorldSave *save);
void world_save_destroy(WorldSave *save);

void world_init(World *world, WorldSave *save);
void world_destroy(World *world);
void world_update_chunks(World *world, Vec3 player_pos);

bool world_get_block_type(World *world, IVec3 pos, uint8_t *type_out);
bool world_block_exists(World *world, IVec3 pos);
bool world_add_block(World *world, IVec3 pos, uint8_t type);
bool world_remove_block(World *world, IVec3 pos);
int world_total_render_blocks(World *world);

#endif /* WORLD_H */