#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "math.h"
#include "entity.h"

/* -------------------------------------------------------------------------- */
/* Block Types                                                                */
/* -------------------------------------------------------------------------- */

typedef enum {
    BLOCK_DIRT = 0,
    BLOCK_STONE = 1,
    BLOCK_GRASS = 2,
    BLOCK_SAND = 3,
    BLOCK_WATER = 4,
    BLOCK_WOOD = 5,
    BLOCK_LEAVES = 6,
    BLOCK_PLANKS = 7,
    ITEM_STICK = 8,
    ITEM_TYPE_COUNT
} BlockType;

enum {
    CROSSHAIR_TEXTURE_INDEX = ITEM_TYPE_COUNT,
    INVENTORY_SELECTION_TEXTURE_INDEX = ITEM_TYPE_COUNT + 1,
    INVENTORY_BG_TEXTURE_INDEX = ITEM_TYPE_COUNT + 2,
    HIGHLIGHT_TEXTURE_INDEX = ITEM_TYPE_COUNT + 3,
    HEALTH_BAR_INDEX = ITEM_TYPE_COUNT + 4
};

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define CHUNK_SIZE 16
#define WORLD_MIN_Y (-8)
#define WORLD_MAX_Y 32
#define CHUNK_HEIGHT (WORLD_MAX_Y - WORLD_MIN_Y + 1)

#define ACTIVE_CHUNK_RADIUS 6
#define CHUNK_UNLOAD_MARGIN 2
#define MAX_LOADED_CHUNKS ((uint32_t)(((ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) * 2 + 1) * \
                                       ((ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) * 2 + 1)))

#define WORLD_SAVE_FILE "world.vox"
#define WORLD_SAVE_MAGIC 0x58574F56u
#define WORLD_SAVE_VERSION 1u

#define INITIAL_INSTANCE_CAPACITY 200000u
#define MAX_INSTANCE_CAPACITY 1500000u

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 min;
    Vec3 max;
} AABB;

typedef struct {
    IVec3 pos;
    uint8_t type;
} Block;

typedef struct {
    int32_t cx;
    int32_t cz;
    uint8_t *voxels;
} ChunkRecord;

typedef struct Player Player;

typedef struct {
    ChunkRecord *records;
    int count;
    int capacity;
    bool dirty;
    char path[256];

    /* Player save data */
    bool has_player_data;
    Vec3 player_position;
    uint8_t player_health;
    uint8_t player_selected_slot;
    uint8_t player_inventory[27];
    uint8_t player_inventory_counts[27];
} WorldSave;

typedef struct Chunk {
    int cx, cz;
    uint8_t *voxels;
    
    Block *blocks;
    int block_count;
    int block_capacity;
    
    bool dirty;
    bool render_dirty;
} Chunk;

typedef struct World {
    Chunk **chunks;
    int chunk_count;
    int chunk_capacity;
    
    Vec3 spawn_position;
    bool spawn_set;
    
    WorldSave *save;

    Entity *entities;
    int entity_count;
    int entity_capacity;
} World;

/* -------------------------------------------------------------------------- */
/* Utility Functions                                                          */
/* -------------------------------------------------------------------------- */

IVec3 world_to_cell(Vec3 p);
AABB cell_aabb(IVec3 cell);
bool world_y_in_bounds(int y);
bool item_is_placeable(uint8_t type);

/* -------------------------------------------------------------------------- */
/* World Save API                                                             */
/* -------------------------------------------------------------------------- */

void world_save_init(WorldSave *save, const char *path);
bool world_save_load(WorldSave *save);
void world_save_flush(WorldSave *save);
void world_save_destroy(WorldSave *save);

/* -------------------------------------------------------------------------- */
/* Player Save API                                                            */
/* -------------------------------------------------------------------------- */

void world_save_store_player(WorldSave *save, const Player *player);
bool world_save_load_player(const WorldSave *save, Player *player);

/* -------------------------------------------------------------------------- */
/* World API                                                                  */
/* -------------------------------------------------------------------------- */

void world_init(World *world, WorldSave *save);
void world_destroy(World *world);
void world_update_chunks(World *world, Vec3 player_pos);

bool world_get_block_type(World *world, IVec3 pos, uint8_t *type_out);
bool world_block_exists(World *world, IVec3 pos);
bool world_add_block(World *world, IVec3 pos, uint8_t type);
bool world_remove_block(World *world, IVec3 pos);

int world_total_render_blocks(World *world);

/* -------------------------------------------------------------------------- */
/* Entity Management                                                          */
/* -------------------------------------------------------------------------- */

bool world_add_entity(World *world, Entity entity);
void world_update_entities(World *world, float delta_time);
uint32_t world_get_entity_render_block_count(const World *world);
uint32_t world_write_entity_render_blocks(const World *world, void *out_data,
                                          uint32_t offset, uint32_t max);

#endif /* WORLD_H */