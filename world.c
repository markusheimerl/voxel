#include "world.h"
#include "player.h"
#include "entity.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */
/* Coordinate Helpers                                                         */
/* -------------------------------------------------------------------------- */

static inline int floor_div(int value, int divisor) {
    int q = value / divisor;
    int r = value % divisor;
    return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}

static inline int cell_to_chunk(int cell_coord) {
    return floor_div(cell_coord, CHUNK_SIZE);
}

static inline int chunk_to_base(int chunk_coord) {
    return chunk_coord * CHUNK_SIZE;
}

static inline size_t voxel_index(int x, int y, int z) {
    return ((size_t)y * CHUNK_SIZE + (size_t)z) * CHUNK_SIZE + (size_t)x;
}

static inline size_t chunk_voxel_count(void) {
    return CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;
}

IVec3 world_to_cell(Vec3 p) {
    return (IVec3){
        (int)floorf(p.x + 0.5f),
        (int)floorf(p.y + 0.5f),
        (int)floorf(p.z + 0.5f)
    };
}

bool world_y_in_bounds(int y) {
    return y >= WORLD_MIN_Y && y <= WORLD_MAX_Y;
}

/* -------------------------------------------------------------------------- */
/* Block Helpers                                                              */
/* -------------------------------------------------------------------------- */

AABB cell_aabb(IVec3 cell) {
    return (AABB){
        .min = vec3(cell.x - 0.5f, cell.y - 0.5f, cell.z - 0.5f),
        .max = vec3(cell.x + 0.5f, cell.y + 0.5f, cell.z + 0.5f)
    };
}

static inline bool is_air(uint8_t type) {
    return type == 255;
}

static inline bool is_water(uint8_t type) {
    return type == BLOCK_WATER;
}

bool item_is_placeable(uint8_t type) {
    return type < ITEM_STICK;
}

/* -------------------------------------------------------------------------- */
/* World Save                                                                 */
/* -------------------------------------------------------------------------- */

static int save_find_chunk(const WorldSave *save, int cx, int cz) {
    for (int i = 0; i < save->count; ++i) {
        if (save->records[i].cx == cx && save->records[i].cz == cz) {
            return i;
        }
    }
    return -1;
}

static void save_ensure_capacity(WorldSave *save, int min_capacity) {
    if (save->capacity >= min_capacity) return;
    
    int new_cap = save->capacity > 0 ? save->capacity : 64;
    while (new_cap < min_capacity) new_cap *= 2;
    
    ChunkRecord *new_records = realloc(save->records, (size_t)new_cap * sizeof(ChunkRecord));
    if (!new_records) die("Failed to grow world save");
    
    save->records = new_records;
    save->capacity = new_cap;
}

void world_save_init(WorldSave *save, const char *path) {
    memset(save, 0, sizeof(*save));
    snprintf(save->path, sizeof(save->path), "%s", path);
}

bool world_save_load(WorldSave *save) {
    FILE *f = fopen(save->path, "rb");
    if (!f) return false;
    
    uint32_t magic, version, file_chunk_size, record_count;
    int32_t file_min_y, file_max_y;
    
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&file_chunk_size, sizeof(file_chunk_size), 1, f) != 1 ||
        fread(&file_min_y, sizeof(file_min_y), 1, f) != 1 ||
        fread(&file_max_y, sizeof(file_max_y), 1, f) != 1 ||
        fread(&record_count, sizeof(record_count), 1, f) != 1) {
        fclose(f);
        return false;
    }
    
    if (magic != WORLD_SAVE_MAGIC ||
        version != WORLD_SAVE_VERSION ||
        file_chunk_size != CHUNK_SIZE ||
        file_min_y != WORLD_MIN_Y ||
        file_max_y != WORLD_MAX_Y) {
        fclose(f);
        return false;
    }

    /* Load player data */
    save->has_player_data = false;
    if (fread(&save->player_position.x, sizeof(float), 1, f) == 1 &&
        fread(&save->player_position.y, sizeof(float), 1, f) == 1 &&
        fread(&save->player_position.z, sizeof(float), 1, f) == 1 &&
        fread(&save->player_health, sizeof(uint8_t), 1, f) == 1 &&
        fread(&save->player_selected_slot, sizeof(uint8_t), 1, f) == 1 &&
        fread(save->player_inventory, sizeof(uint8_t), 27, f) == 27 &&
        fread(save->player_inventory_counts, sizeof(uint8_t), 27, f) == 27) {
        save->has_player_data = true;
    } else {
        fclose(f);
        return false;
    }
    
    /* Free existing records */
    for (int i = 0; i < save->count; ++i) {
        free(save->records[i].voxels);
    }
    free(save->records);
    save->records = NULL;
    save->count = 0;
    save->capacity = 0;
    
    if (record_count == 0) {
        fclose(f);
        save->dirty = false;
        return true;
    }
    
    save->records = calloc(record_count, sizeof(ChunkRecord));
    if (!save->records) die("Failed to allocate save records");
    save->capacity = (int)record_count;
    
    size_t voxel_size = chunk_voxel_count();
    for (uint32_t i = 0; i < record_count; ++i) {
        int32_t cx, cz;
        if (fread(&cx, sizeof(cx), 1, f) != 1 ||
            fread(&cz, sizeof(cz), 1, f) != 1) {
            fclose(f);
            return false;
        }
        
        uint8_t *voxels = malloc(voxel_size);
        if (!voxels || fread(voxels, 1, voxel_size, f) != voxel_size) {
            free(voxels);
            fclose(f);
            return false;
        }
        
        save->records[i] = (ChunkRecord){.cx = cx, .cz = cz, .voxels = voxels};
        save->count++;
    }
    
    fclose(f);
    save->dirty = false;
    return true;
}

void world_save_flush(WorldSave *save) {
    if (!save->dirty) return;
    
    char tmp_path[300];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", save->path);
    
    FILE *f = fopen(tmp_path, "wb");
    if (!f) die("Failed to open temp save file");
    
    uint32_t magic = WORLD_SAVE_MAGIC;
    uint32_t version = WORLD_SAVE_VERSION;
    uint32_t file_chunk_size = CHUNK_SIZE;
    int32_t file_min_y = WORLD_MIN_Y;
    int32_t file_max_y = WORLD_MAX_Y;
    uint32_t record_count = (uint32_t)save->count;
    
    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&file_chunk_size, sizeof(file_chunk_size), 1, f) != 1 ||
        fwrite(&file_min_y, sizeof(file_min_y), 1, f) != 1 ||
        fwrite(&file_max_y, sizeof(file_max_y), 1, f) != 1 ||
        fwrite(&record_count, sizeof(record_count), 1, f) != 1) {
        fclose(f);
        remove(tmp_path);
        die("Failed to write save header");
    }

    /* Write player data */
    if (fwrite(&save->player_position.x, sizeof(float), 1, f) != 1 ||
        fwrite(&save->player_position.y, sizeof(float), 1, f) != 1 ||
        fwrite(&save->player_position.z, sizeof(float), 1, f) != 1 ||
        fwrite(&save->player_health, sizeof(uint8_t), 1, f) != 1 ||
        fwrite(&save->player_selected_slot, sizeof(uint8_t), 1, f) != 1 ||
        fwrite(save->player_inventory, sizeof(uint8_t), 27, f) != 27 ||
        fwrite(save->player_inventory_counts, sizeof(uint8_t), 27, f) != 27) {
        fclose(f);
        remove(tmp_path);
        die("Failed to write player data");
    }
    
    size_t voxel_size = chunk_voxel_count();
    for (int i = 0; i < save->count; ++i) {
        int32_t cx = save->records[i].cx;
        int32_t cz = save->records[i].cz;
        
        if (fwrite(&cx, sizeof(cx), 1, f) != 1 ||
            fwrite(&cz, sizeof(cz), 1, f) != 1 ||
            fwrite(save->records[i].voxels, 1, voxel_size, f) != voxel_size) {
            fclose(f);
            remove(tmp_path);
            die("Failed to write save record");
        }
    }
    
    fclose(f);
    
    if (rename(tmp_path, save->path) != 0) {
        remove(tmp_path);
        die("Failed to replace save file");
    }
    
    save->dirty = false;
}

void world_save_destroy(WorldSave *save) {
    world_save_flush(save);
    for (int i = 0; i < save->count; ++i) {
        free(save->records[i].voxels);
    }
    free(save->records);
    memset(save, 0, sizeof(*save));
}

static void save_store_chunk(WorldSave *save, int cx, int cz, const uint8_t *voxels) {
    int idx = save_find_chunk(save, cx, cz);
    size_t voxel_size = chunk_voxel_count();
    
    if (idx < 0) {
        save_ensure_capacity(save, save->count + 1);
        idx = save->count++;
        save->records[idx].cx = cx;
        save->records[idx].cz = cz;
        save->records[idx].voxels = malloc(voxel_size);
        if (!save->records[idx].voxels) die("Failed to allocate save voxels");
    }
    
    memcpy(save->records[idx].voxels, voxels, voxel_size);
    save->dirty = true;
}

static bool save_load_chunk(const WorldSave *save, int cx, int cz, uint8_t *out_voxels) {
    int idx = save_find_chunk(save, cx, cz);
    if (idx < 0) return false;
    
    memcpy(out_voxels, save->records[idx].voxels, chunk_voxel_count());
    return true;
}

void world_save_store_player(WorldSave *save, const Player *player) {
    if (!save || !player) return;

    save->player_position = player->position;
    save->player_health = player->health;
    save->player_selected_slot = player->selected_slot;

    for (int i = 0; i < 27; ++i) {
        save->player_inventory[i] = player->inventory[i];
        save->player_inventory_counts[i] = player->inventory_counts[i];
    }

    save->has_player_data = true;
    save->dirty = true;
}

bool world_save_load_player(const WorldSave *save, Player *player) {
    if (!save || !player || !save->has_player_data) return false;

    player->position = save->player_position;
    player->health = save->player_health;
    player->selected_slot = save->player_selected_slot;

    for (int i = 0; i < 27; ++i) {
        player->inventory[i] = save->player_inventory[i];
        player->inventory_counts[i] = save->player_inventory_counts[i];
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* Chunk Operations                                                           */
/* -------------------------------------------------------------------------- */

static void chunk_ensure_capacity(Chunk *chunk, int min_capacity) {
    if (chunk->block_capacity >= min_capacity) return;
    
    int new_cap = chunk->block_capacity > 0 ? chunk->block_capacity : 256;
    while (new_cap < min_capacity) new_cap *= 2;
    
    Block *new_blocks = realloc(chunk->blocks, (size_t)new_cap * sizeof(Block));
    if (!new_blocks) die("Failed to allocate chunk blocks");
    
    chunk->blocks = new_blocks;
    chunk->block_capacity = new_cap;
}

static void chunk_init(Chunk *chunk, int cx, int cz) {
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->block_count = 0;
    chunk->block_capacity = 0;
    chunk->blocks = NULL;
    chunk->dirty = false;
    chunk->render_dirty = true;
    
    size_t voxel_size = chunk_voxel_count();
    chunk->voxels = malloc(voxel_size);
    if (!chunk->voxels) die("Failed to allocate chunk voxels");
    memset(chunk->voxels, 255, voxel_size);
}

static void chunk_destroy(Chunk *chunk) {
    if (!chunk) return;
    free(chunk->voxels);
    free(chunk->blocks);
    free(chunk);
}

static inline bool chunk_in_bounds(int lx, int ly, int lz) {
    return lx >= 0 && lx < CHUNK_SIZE &&
           lz >= 0 && lz < CHUNK_SIZE &&
           ly >= 0 && ly < CHUNK_HEIGHT;
}

static uint8_t chunk_get_voxel(const Chunk *chunk, int lx, int ly, int lz) {
    return chunk_in_bounds(lx, ly, lz) ? chunk->voxels[voxel_index(lx, ly, lz)] : 255;
}

static void chunk_set_voxel(Chunk *chunk, int lx, int ly, int lz, uint8_t type) {
    if (chunk_in_bounds(lx, ly, lz)) {
        chunk->voxels[voxel_index(lx, ly, lz)] = type;
    }
}

static IVec3 chunk_local_to_world(const Chunk *chunk, int lx, int ly, int lz) {
    return (IVec3){
        chunk_to_base(chunk->cx) + lx,
        WORLD_MIN_Y + ly,
        chunk_to_base(chunk->cz) + lz
    };
}

static bool chunk_world_to_local(const Chunk *chunk, IVec3 pos, int *lx, int *ly, int *lz) {
    if (!world_y_in_bounds(pos.y)) return false;
    
    *lx = pos.x - chunk_to_base(chunk->cx);
    *lz = pos.z - chunk_to_base(chunk->cz);
    *ly = pos.y - WORLD_MIN_Y;
    
    return chunk_in_bounds(*lx, *ly, *lz);
}

static void chunk_generate(World *world, Chunk *chunk);

static void chunk_rebuild_render_list(World *world, Chunk *chunk) {
    chunk->block_count = 0;
    
    int base_x = chunk_to_base(chunk->cx);
    int base_z = chunk_to_base(chunk->cz);
    
    static const IVec3 neighbors[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    
    for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                uint8_t type = chunk_get_voxel(chunk, lx, ly, lz);
                if (is_air(type)) continue;
                
                IVec3 pos = chunk_local_to_world(chunk, lx, ly, lz);
                
                /* Check if any face is exposed */
                bool visible = false;
                for (int d = 0; d < 6; ++d) {
                    IVec3 npos = ivec3_add(pos, neighbors[d]);
                    
                    uint8_t ntype = 255;
                    if (world_y_in_bounds(npos.y)) {
                        /* Same chunk or cross-chunk */
                        int nlx = npos.x - base_x;
                        int nlz = npos.z - base_z;
                        int nly = npos.y - WORLD_MIN_Y;
                        
                        if (chunk_in_bounds(nlx, nly, nlz)) {
                            ntype = chunk_get_voxel(chunk, nlx, nly, nlz);
                        } else {
                            world_get_block_type(world, npos, &ntype);
                        }
                    }
                    
                    /* Water blocks are visible next to air or non-water */
                    /* Solid blocks are visible next to air or water */
                    if (is_water(type)) {
                        if (is_air(ntype) || !is_water(ntype)) {
                            visible = true;
                            break;
                        }
                    } else {
                        if (is_air(ntype) || is_water(ntype)) {
                            visible = true;
                            break;
                        }
                    }
                }
                
                if (!visible) continue;
                
                chunk_ensure_capacity(chunk, chunk->block_count + 1);
                chunk->blocks[chunk->block_count++] = (Block){.pos = pos, .type = type};
            }
        }
    }
    
    chunk->render_dirty = false;
}

static bool chunk_add_block(Chunk *chunk, IVec3 pos, uint8_t type) {
    int lx, ly, lz;
    if (!chunk_world_to_local(chunk, pos, &lx, &ly, &lz)) return false;
    if (!is_air(chunk_get_voxel(chunk, lx, ly, lz))) return false;
    
    chunk_set_voxel(chunk, lx, ly, lz, type);
    chunk->dirty = true;
    chunk->render_dirty = true;
    return true;
}

static bool chunk_remove_block(Chunk *chunk, IVec3 pos) {
    int lx, ly, lz;
    if (!chunk_world_to_local(chunk, pos, &lx, &ly, &lz)) return false;
    if (is_air(chunk_get_voxel(chunk, lx, ly, lz))) return false;
    
    chunk_set_voxel(chunk, lx, ly, lz, 255);
    chunk->dirty = true;
    chunk->render_dirty = true;
    return true;
}

/* -------------------------------------------------------------------------- */
/* World Operations                                                           */
/* -------------------------------------------------------------------------- */

static Chunk *world_find_chunk(World *world, int cx, int cz) {
    for (int i = 0; i < world->chunk_count; ++i) {
        if (world->chunks[i]->cx == cx && world->chunks[i]->cz == cz) {
            return world->chunks[i];
        }
    }
    return NULL;
}

static void world_add_chunk(World *world, Chunk *chunk) {
    if (world->chunk_count >= world->chunk_capacity) {
        int new_cap = world->chunk_capacity > 0 ? world->chunk_capacity * 2 : 64;
        Chunk **new_chunks = realloc(world->chunks, (size_t)new_cap * sizeof(Chunk *));
        if (!new_chunks) die("Failed to allocate chunk list");
        world->chunks = new_chunks;
        world->chunk_capacity = new_cap;
    }
    
    world->chunks[world->chunk_count++] = chunk;
    
    if ((uint32_t)world->chunk_count > MAX_LOADED_CHUNKS) {
        die("Exceeded maximum loaded chunks");
    }
}

static void world_try_set_spawn(World *world, Chunk *chunk) {
    if (world->spawn_set) return;
    
    int base_x = chunk_to_base(chunk->cx);
    int base_z = chunk_to_base(chunk->cz);
    
    /* Only check chunks containing (0, 0) */
    if (0 < base_x || 0 >= base_x + CHUNK_SIZE ||
        0 < base_z || 0 >= base_z + CHUNK_SIZE) {
        return;
    }
    
    int lx = -base_x;
    int lz = -base_z;
    
    /* Find highest solid block at spawn */
    for (int ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        uint8_t type = chunk_get_voxel(chunk, lx, ly, lz);
        if (!is_air(type) && !is_water(type)) {
            world->spawn_position = vec3(0.0f, (float)(WORLD_MIN_Y + ly) + 0.5f, 0.0f);
            world->spawn_set = true;
            return;
        }
    }
}

static Chunk *world_create_chunk(World *world, int cx, int cz) {
    Chunk *chunk = malloc(sizeof(Chunk));
    if (!chunk) die("Failed to allocate chunk");
    
    chunk_init(chunk, cx, cz);
    
    bool loaded = save_load_chunk(world->save, cx, cz, chunk->voxels);
    if (!loaded) {
        chunk_generate(world, chunk);
        chunk->dirty = true;
    }
    chunk->render_dirty = true;
    
    world_try_set_spawn(world, chunk);
    world_add_chunk(world, chunk);
    
    return chunk;
}

static void world_unload_chunk_at(World *world, int index) {
    Chunk *chunk = world->chunks[index];
    
    if (chunk->dirty) {
        save_store_chunk(world->save, chunk->cx, chunk->cz, chunk->voxels);
    }
    
    chunk_destroy(chunk);
    world->chunks[index] = world->chunks[--world->chunk_count];
}

static void world_mark_neighbors_dirty(World *world, IVec3 pos) {
    int cx = cell_to_chunk(pos.x);
    int cz = cell_to_chunk(pos.z);
    
    Chunk *center = world_find_chunk(world, cx, cz);
    if (center) center->render_dirty = true;
    
    /* Mark neighbors if on chunk boundary */
    int base_x = chunk_to_base(cx);
    int base_z = chunk_to_base(cz);
    int lx = pos.x - base_x;
    int lz = pos.z - base_z;
    
    if (lx == 0) {
        Chunk *c = world_find_chunk(world, cx - 1, cz);
        if (c) c->render_dirty = true;
    } else if (lx == CHUNK_SIZE - 1) {
        Chunk *c = world_find_chunk(world, cx + 1, cz);
        if (c) c->render_dirty = true;
    }
    
    if (lz == 0) {
        Chunk *c = world_find_chunk(world, cx, cz - 1);
        if (c) c->render_dirty = true;
    } else if (lz == CHUNK_SIZE - 1) {
        Chunk *c = world_find_chunk(world, cx, cz + 1);
        if (c) c->render_dirty = true;
    }
}

void world_init(World *world, WorldSave *save) {
    memset(world, 0, sizeof(*world));
    world->spawn_position = vec3(0.0f, 4.5f, 0.0f);
    world->save = save;
    world->entities = NULL;
    world->entity_count = 0;
    world->entity_capacity = 0;
}

void world_destroy(World *world) {
    for (int i = 0; i < world->chunk_count; ++i) {
        Chunk *chunk = world->chunks[i];
        if (chunk->dirty) {
            save_store_chunk(world->save, chunk->cx, chunk->cz, chunk->voxels);
        }
        chunk_destroy(chunk);
    }
    free(world->chunks);
    free(world->entities);
    memset(world, 0, sizeof(*world));
}

void world_update_chunks(World *world, Vec3 player_pos) {
    IVec3 center_cell = world_to_cell(player_pos);
    int center_cx = cell_to_chunk(center_cell.x);
    int center_cz = cell_to_chunk(center_cell.z);
    
    /* Load chunks in view radius */
    for (int dz = -ACTIVE_CHUNK_RADIUS; dz <= ACTIVE_CHUNK_RADIUS; ++dz) {
        for (int dx = -ACTIVE_CHUNK_RADIUS; dx <= ACTIVE_CHUNK_RADIUS; ++dx) {
            int cx = center_cx + dx;
            int cz = center_cz + dz;
            if (!world_find_chunk(world, cx, cz)) {
                world_create_chunk(world, cx, cz);
            }
        }
    }
    
    /* Unload distant chunks */
    for (int i = 0; i < world->chunk_count; ) {
        Chunk *chunk = world->chunks[i];
        int dx = abs(chunk->cx - center_cx);
        int dz = abs(chunk->cz - center_cz);
        
        if (dx > ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN ||
            dz > ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) {
            world_unload_chunk_at(world, i);
        } else {
            ++i;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Entity Management                                                          */
/* -------------------------------------------------------------------------- */

static void world_ensure_entity_capacity(World *world, int min_capacity) {
    if (world->entity_capacity >= min_capacity) return;

    int new_cap = world->entity_capacity > 0 ? world->entity_capacity * 2 : 8;
    while (new_cap < min_capacity) new_cap *= 2;

    Entity *new_entities = realloc(world->entities, (size_t)new_cap * sizeof(Entity));
    if (!new_entities) die("Failed to allocate entity array");

    world->entities = new_entities;
    world->entity_capacity = new_cap;
}

bool world_add_entity(World *world, Entity entity) {
    if (!world) return false;

    world_ensure_entity_capacity(world, world->entity_count + 1);
    world->entities[world->entity_count++] = entity;
    return true;
}

void world_update_entities(World *world, float delta_time) {
    if (!world) return;

    for (int i = 0; i < world->entity_count; ++i) {
        entity_update(&world->entities[i], delta_time);
        entity_apply_physics(&world->entities[i], world, delta_time);
    }
}

uint32_t world_get_entity_render_block_count(const World *world) {
    if (!world) return 0;

    uint32_t total = 0;
    for (int i = 0; i < world->entity_count; ++i) {
        total += entity_get_render_block_count(&world->entities[i]);
    }
    return total;
}

uint32_t world_write_entity_render_blocks(const World *world, void *out_data,
                                          uint32_t offset, uint32_t max) {
    if (!world || !out_data) return 0;

    uint32_t written = 0;
    for (int i = 0; i < world->entity_count && written < max; ++i) {
        written += entity_write_render_blocks(&world->entities[i], out_data,
                                               offset + written * ENTITY_INSTANCE_STRIDE_BYTES,
                                               max - written);
    }
    return written;
}

bool world_get_block_type(World *world, IVec3 pos, uint8_t *type_out) {
    if (!world_y_in_bounds(pos.y)) return false;
    
    Chunk *chunk = world_find_chunk(world, cell_to_chunk(pos.x), cell_to_chunk(pos.z));
    if (!chunk) return false;
    
    int lx, ly, lz;
    if (!chunk_world_to_local(chunk, pos, &lx, &ly, &lz)) return false;
    
    uint8_t type = chunk_get_voxel(chunk, lx, ly, lz);
    if (is_air(type)) return false;
    
    if (type_out) *type_out = type;
    return true;
}

bool world_block_exists(World *world, IVec3 pos) {
    return world_get_block_type(world, pos, NULL);
}

bool world_add_block(World *world, IVec3 pos, uint8_t type) {
    int cx = cell_to_chunk(pos.x);
    int cz = cell_to_chunk(pos.z);
    
    Chunk *chunk = world_find_chunk(world, cx, cz);
    if (!chunk) chunk = world_create_chunk(world, cx, cz);
    
    bool result = chunk_add_block(chunk, pos, type);
    if (result) world_mark_neighbors_dirty(world, pos);
    return result;
}

bool world_remove_block(World *world, IVec3 pos) {
    Chunk *chunk = world_find_chunk(world, cell_to_chunk(pos.x), cell_to_chunk(pos.z));
    if (!chunk) return false;
    
    bool result = chunk_remove_block(chunk, pos);
    if (result) world_mark_neighbors_dirty(world, pos);
    return result;
}

int world_total_render_blocks(World *world) {
    int total = 0;
    for (int i = 0; i < world->chunk_count; ++i) {
        Chunk *chunk = world->chunks[i];
        if (chunk->render_dirty) {
            chunk_rebuild_render_list(world, chunk);
        }
        total += chunk->block_count;
    }
    return total;
}

/* -------------------------------------------------------------------------- */
/* Terrain Generation                                                         */
/* -------------------------------------------------------------------------- */

static void chunk_generate(World *world, Chunk *chunk) {
    const int SEA_LEVEL = 3;
    const int BEDROCK_DEPTH = -4;
    
    int base_x = chunk_to_base(chunk->cx);
    int base_z = chunk_to_base(chunk->cz);
    
    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
        int wx = base_x + lx;
        float fx = (float)wx;
        
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            int wz = base_z + lz;
            float fz = (float)wz;
            
            bool forced_plains = (abs(wx) < 5 && abs(wz) < 5);
            
            /* Terrain height and biome */
            float base = fbm2d(fx * 0.045f, fz * 0.045f, 4, 2.0f, 0.5f, 1234u);
            float detail = fbm2d(fx * 0.12f, fz * 0.12f, 3, 2.15f, 0.5f, 5678u);
            float mountain = fbm2d(fx * 0.02f, fz * 0.02f, 5, 2.0f, 0.45f, 91011u);
            float moisture = fbm2d(fx * 0.03f + 300.0f, fz * 0.03f - 300.0f, 4, 2.0f, 0.5f, 121314u);
            float heat = fbm2d(fx * 0.03f - 600.0f, fz * 0.03f + 600.0f, 4, 2.0f, 0.5f, 151617u);
            float dryness = heat - moisture;
            
            BlockType surface = BLOCK_GRASS;
            float height = 6.0f + base * 4.0f + detail * 2.5f;
            
            if (!forced_plains && mountain > 0.45f) {
                float peaks = fbm2d(fx * 0.05f + 1000.0f, fz * 0.05f - 1000.0f, 4, 2.25f, 0.5f, 181920u);
                height = 12.0f + peaks * 12.0f;
                surface = BLOCK_STONE;
            } else if (!forced_plains && dryness > 0.45f) {
                float dunes = fbm2d(fx * 0.08f + 2000.0f, fz * 0.08f + 2000.0f, 3, 2.1f, 0.55f, 212223u);
                height = 3.0f + dunes * 3.5f;
                surface = BLOCK_SAND;
            } else {
                float meadow = fbm2d(fx * 0.07f - 1500.0f, fz * 0.07f + 1500.0f, 3, 2.0f, 0.5f, 242526u);
                height = 6.5f + base * 3.5f + meadow * 2.0f;
            }
            
            height = fmaxf(height, 0.5f);
            int ground_y = (int)floorf(height);
            
            /* Rivers */
            float river = fabsf(perlin2d(fx * 0.015f + 4000.0f, fz * 0.015f - 4000.0f, 272829u));
            bool is_river = !forced_plains && river < 0.11f;
            
            if (is_river) {
                ground_y = (int)fminf((float)ground_y, (float)SEA_LEVEL - 1.0f);
                surface = BLOCK_SAND;
            }
            
            /* Generate column */
            IVec3 col = {wx, 0, wz};
            
            /* Bedrock and stone */
            for (int y = BEDROCK_DEPTH; y < ground_y - 3; ++y) {
                col.y = y;
                chunk_add_block(chunk, col, BLOCK_STONE);
            }
            
            /* Filler layer */
            BlockType filler = (surface == BLOCK_SAND || surface == BLOCK_STONE) ? surface : BLOCK_DIRT;
            for (int y = ground_y - 3; y < ground_y; ++y) {
                if (y >= BEDROCK_DEPTH) {
                    col.y = y;
                    chunk_add_block(chunk, col, filler);
                }
            }
            
            /* Surface */
            col.y = ground_y;
            chunk_add_block(chunk, col, surface);
            
            /* Water */
            if (is_river || ground_y < SEA_LEVEL) {
                for (int y = ground_y + 1; y <= SEA_LEVEL; ++y) {
                    col.y = y;
                    chunk_add_block(chunk, col, BLOCK_WATER);
                }
            }
            
            /* Trees */
            bool can_tree = !forced_plains && !is_river && ground_y >= SEA_LEVEL && surface == BLOCK_GRASS;
            if (can_tree && (abs(wx) > 3 || abs(wz) > 3) &&
                lx >= 2 && lx <= CHUNK_SIZE - 3 && lz >= 2 && lz <= CHUNK_SIZE - 3) {
                
                uint32_t tree_hash = hash_2d(wx, wz, 424242u);
                if ((tree_hash % 100u) < 3u) {
                    int trunk_height = 4 + (int)((tree_hash >> 8) % 3u);
                    int top_y = ground_y + trunk_height;
                    
                    if (top_y + 2 <= WORLD_MAX_Y) {
                        /* Trunk */
                        for (int ty = 1; ty <= trunk_height; ++ty) {
                            col.y = ground_y + ty;
                            chunk_add_block(chunk, col, BLOCK_WOOD);
                        }
                        
                        /* Foliage */
                        for (int y = top_y - 2; y <= top_y + 1; ++y) {
                            int dy = y - top_y;
                            for (int dx = -2; dx <= 2; ++dx) {
                                for (int dz = -2; dz <= 2; ++dz) {
                                    int dist2 = dx * dx + dz * dz + dy * dy;
                                    if (dist2 > 6 || (dx == 0 && dz == 0 && y <= top_y)) continue;
                                    
                                    IVec3 leaf = {wx + dx, y, wz + dz};
                                    chunk_add_block(chunk, leaf, BLOCK_LEAVES);
                                }
                            }
                        }
                    }
                }
            }
            
            /* Set spawn point */
            if (!world->spawn_set && wx == 0 && wz == 0) {
                world->spawn_position = vec3(0.0f, (float)ground_y + 0.5f, 0.0f);
                world->spawn_set = true;
            }
        }
    }
}