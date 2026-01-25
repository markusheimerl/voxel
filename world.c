

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */
/* Blocks                                                                     */
/* -------------------------------------------------------------------------- */

AABB cell_aabb(IVec3 cell) {
    AABB box;
    box.min = vec3(cell.x - 0.5f, cell.y - 0.5f, cell.z - 0.5f);
    box.max = vec3(cell.x + 0.5f, cell.y + 0.5f, cell.z + 0.5f);
    return box;
}

static bool block_is_air(uint8_t type) { return type == 255u; }
static bool block_is_water(uint8_t type) { return type == BLOCK_WATER; }

/* -------------------------------------------------------------------------- */
/* World / Chunk Configuration                                                */
/* -------------------------------------------------------------------------- */

#define CHUNK_SIZE 16
#define WORLD_MIN_Y (-8)
#define WORLD_MAX_Y 32
#define CHUNK_HEIGHT (WORLD_MAX_Y - WORLD_MIN_Y + 1)

/* Increased view distance: more chunks loaded around the player */
#define ACTIVE_CHUNK_RADIUS 6
#define CHUNK_UNLOAD_MARGIN 2

#define MAX_LOADED_CHUNKS ((uint32_t)(((ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) * 2 + 1) * ((ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN) * 2 + 1)))

/* Single-file world save */
#define WORLD_SAVE_FILE "world.vox"
#define WORLD_SAVE_MAGIC 0x58574F56u /* 'VOWX' little-endian (VOXW as bytes) */
#define WORLD_SAVE_VERSION 1u

/* Instance buffer (CPU->GPU) budget */
#define INITIAL_INSTANCE_CAPACITY 200000u
#define MAX_INSTANCE_CAPACITY     1500000u

bool world_y_in_bounds(int y) {
    return y >= WORLD_MIN_Y && y <= WORLD_MAX_Y;
}

/* -------------------------------------------------------------------------- */
/* World Save (single file)                                                   */
/* -------------------------------------------------------------------------- */

IVec3 world_to_cell(Vec3 p) {
    IVec3 cell = {
        (int)floorf(p.x + 0.5f),
        (int)floorf(p.y + 0.5f),
        (int)floorf(p.z + 0.5f)
    };
    return cell;
}

static size_t chunk_voxel_count(void) {
    return (size_t)CHUNK_SIZE * (size_t)CHUNK_HEIGHT * (size_t)CHUNK_SIZE;
}

static int world_save_find_index(const WorldSave *save, int cx, int cz) {
    for (int i = 0; i < save->count; ++i) {
        if (save->records[i].cx == cx && save->records[i].cz == cz) return save->records[i].cx == cx && save->records[i].cz == cz ? i : -1;
    }
    return -1;
}

void world_save_init(WorldSave *save, const char *path) {
    *save = (WorldSave){0};
    snprintf(save->path, sizeof(save->path), "%s", path);
}

static void world_save_free_records(WorldSave *save) {
    for (int i = 0; i < save->count; ++i) {
        free(save->records[i].voxels);
    }
    free(save->records);
    save->records = NULL;
    save->count = 0;
    save->capacity = 0;
}

bool world_save_load(WorldSave *save) {
    FILE *f = fopen(save->path, "rb");
    if (!f) return false;

    uint32_t magic = 0, version = 0;
    uint32_t file_chunk_size = 0;
    int32_t file_min_y = 0, file_max_y = 0;
    uint32_t record_count = 0;

    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&file_chunk_size, sizeof(file_chunk_size), 1, f) != 1 ||
        fread(&file_min_y, sizeof(file_min_y), 1, f) != 1 ||
        fread(&file_max_y, sizeof(file_max_y), 1, f) != 1 ||
        fread(&record_count, sizeof(record_count), 1, f) != 1)
    {
        fclose(f);
        return false;
    }

    if (magic != WORLD_SAVE_MAGIC || version != WORLD_SAVE_VERSION ||
        file_chunk_size != (uint32_t)CHUNK_SIZE ||
        file_min_y != (int32_t)WORLD_MIN_Y ||
        file_max_y != (int32_t)WORLD_MAX_Y)
    {
        fclose(f);
        return false;
    }

    world_save_free_records(save);

    if (record_count > 0) {
        save->records = calloc(record_count, sizeof(ChunkRecord));
        if (!save->records) die("Failed to allocate world save index");
        save->capacity = (int)record_count;
    }

    size_t vcount = chunk_voxel_count();

    for (uint32_t i = 0; i < record_count; ++i) {
        int32_t cx = 0, cz = 0;
        if (fread(&cx, sizeof(cx), 1, f) != 1 ||
            fread(&cz, sizeof(cz), 1, f) != 1)
        {
            fclose(f);
            world_save_free_records(save);
            return false;
        }

        uint8_t *vox = malloc(vcount);
        if (!vox) die("Failed to allocate chunk voxel data for load");

        if (fread(vox, 1, vcount, f) != vcount) {
            free(vox);
            fclose(f);
            world_save_free_records(save);
            return false;
        }

        save->records[i].cx = cx;
        save->records[i].cz = cz;
        save->records[i].voxels = vox;
        save->count++;
    }

    fclose(f);
    save->dirty = false;
    return true;
}

static void world_save_ensure_capacity(WorldSave *save, int min_capacity) {
    if (save->capacity >= min_capacity) return;

    int new_capacity = save->capacity > 0 ? save->capacity : 64;
    while (new_capacity < min_capacity) new_capacity *= 2;

    ChunkRecord *new_records = realloc(save->records, (size_t)new_capacity * sizeof(ChunkRecord));
    if (!new_records) die("Failed to grow world save record list");

    save->records = new_records;
    save->capacity = new_capacity;
}

static void world_save_store(WorldSave *save, int cx, int cz, const uint8_t *voxels) {
    int idx = world_save_find_index(save, cx, cz);
    size_t vcount = chunk_voxel_count();

    if (idx < 0) {
        world_save_ensure_capacity(save, save->count + 1);
        idx = save->count++;
        save->records[idx].cx = (int32_t)cx;
        save->records[idx].cz = (int32_t)cz;
        save->records[idx].voxels = malloc(vcount);
        if (!save->records[idx].voxels) die("Failed to allocate world save voxel record");
    }

    memcpy(save->records[idx].voxels, voxels, vcount);
    save->dirty = true;
}

static bool world_save_fetch(const WorldSave *save, int cx, int cz, uint8_t *out_voxels) {
    int idx = world_save_find_index(save, cx, cz);
    if (idx < 0) return false;
    memcpy(out_voxels, save->records[idx].voxels, chunk_voxel_count());
    return true;
}

void world_save_flush(WorldSave *save) {
    if (!save->dirty) return;

    char tmp_path[300];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", save->path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) die("Failed to open temp world save file for writing");

    uint32_t magic = WORLD_SAVE_MAGIC;
    uint32_t version = WORLD_SAVE_VERSION;
    uint32_t file_chunk_size = (uint32_t)CHUNK_SIZE;
    int32_t file_min_y = (int32_t)WORLD_MIN_Y;
    int32_t file_max_y = (int32_t)WORLD_MAX_Y;
    uint32_t record_count = (uint32_t)save->count;

    if (fwrite(&magic, sizeof(magic), 1, f) != 1 ||
        fwrite(&version, sizeof(version), 1, f) != 1 ||
        fwrite(&file_chunk_size, sizeof(file_chunk_size), 1, f) != 1 ||
        fwrite(&file_min_y, sizeof(file_min_y), 1, f) != 1 ||
        fwrite(&file_max_y, sizeof(file_max_y), 1, f) != 1 ||
        fwrite(&record_count, sizeof(record_count), 1, f) != 1)
    {
        fclose(f);
        remove(tmp_path);
        die("Failed to write world save header");
    }

    size_t vcount = chunk_voxel_count();

    for (int i = 0; i < save->count; ++i) {
        int32_t cx = save->records[i].cx;
        int32_t cz = save->records[i].cz;

        if (fwrite(&cx, sizeof(cx), 1, f) != 1 ||
            fwrite(&cz, sizeof(cz), 1, f) != 1 ||
            fwrite(save->records[i].voxels, 1, vcount, f) != vcount)
        {
            fclose(f);
            remove(tmp_path);
            die("Failed to write world save record");
        }
    }

    fclose(f);

    if (rename(tmp_path, save->path) != 0) {
        remove(tmp_path);
        die("Failed to replace world save file");
    }

    save->dirty = false;
}

void world_save_destroy(WorldSave *save) {
    world_save_flush(save);
    world_save_free_records(save);
    *save = (WorldSave){0};
}

/* -------------------------------------------------------------------------- */
/* World / Chunk Structures                                                   */
/* -------------------------------------------------------------------------- */

typedef struct Chunk Chunk;
typedef struct World World;

/* -------------------------------------------------------------------------- */
/* Chunk Helpers                                                              */
/* -------------------------------------------------------------------------- */

static inline int div_floor_int(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        quotient -= 1;
    }
    return quotient;
}

static inline int cell_to_chunk_coord(int value) {
    return div_floor_int(value, CHUNK_SIZE);
}

static inline int chunk_base_coord(int chunk_coord) {
    return chunk_coord * CHUNK_SIZE;
}

static inline int chunk_voxel_index(int lx, int ly, int lz) {
    return (ly * CHUNK_SIZE + lz) * CHUNK_SIZE + lx;
}

static void chunk_ensure_capacity(Chunk *chunk, int min_capacity) {
    if (chunk->block_capacity >= min_capacity) return;

    int new_capacity = chunk->block_capacity > 0 ? chunk->block_capacity : 256;
    while (new_capacity < min_capacity) new_capacity *= 2;

    Block *new_blocks = realloc(chunk->blocks, (size_t)new_capacity * sizeof(Block));
    if (!new_blocks) die("Failed to allocate chunk blocks");

    chunk->blocks = new_blocks;
    chunk->block_capacity = new_capacity;
}

static void chunk_init(Chunk *chunk, int cx, int cz) {
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->block_count = 0;
    chunk->block_capacity = 0;
    chunk->blocks = NULL;
    chunk->dirty = false;
    chunk->render_dirty = true;

    size_t voxel_count = chunk_voxel_count();
    chunk->voxels = malloc(voxel_count);
    if (!chunk->voxels) die("Failed to allocate chunk voxels");
    memset(chunk->voxels, (int)255, voxel_count);
}

static void chunk_destroy(Chunk *chunk) {
    if (!chunk) return;
    free(chunk->voxels);
    free(chunk->blocks);
    free(chunk);
}

static uint8_t chunk_get_voxel(const Chunk *chunk, int lx, int ly, int lz) {
    if (lx < 0 || lx >= CHUNK_SIZE ||
        lz < 0 || lz >= CHUNK_SIZE ||
        ly < 0 || ly >= CHUNK_HEIGHT)
    {
        return 255u;
    }
    return chunk->voxels[chunk_voxel_index(lx, ly, lz)];
}

static void chunk_set_voxel(Chunk *chunk, int lx, int ly, int lz, uint8_t type) {
    if (lx < 0 || lx >= CHUNK_SIZE ||
        lz < 0 || lz >= CHUNK_SIZE ||
        ly < 0 || ly >= CHUNK_HEIGHT)
    {
        return;
    }
    chunk->voxels[chunk_voxel_index(lx, ly, lz)] = type;
}

/* Build render list: include only blocks that have at least one potentially visible face. */
static void chunk_rebuild_render_list(World *world, Chunk *chunk) {
    chunk->block_count = 0;

    int base_x = chunk_base_coord(chunk->cx);
    int base_z = chunk_base_coord(chunk->cz);

    for (int ly = 0; ly < CHUNK_HEIGHT; ++ly) {
        int wy = WORLD_MIN_Y + ly;
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                uint8_t type = chunk_get_voxel(chunk, lx, ly, lz);
                if (block_is_air(type)) continue;

                /* Neighbor positions in world coords */
                IVec3 p = (IVec3){ base_x + lx, wy, base_z + lz };

                /* Determine if this block has any face exposed to air / water boundary */
                static const IVec3 dirs[6] = {
                    { 1, 0, 0}, {-1, 0, 0},
                    { 0, 1, 0}, { 0,-1, 0},
                    { 0, 0, 1}, { 0, 0,-1}
                };

                bool visible = false;
                for (int d = 0; d < 6; ++d) {
                    IVec3 npos = ivec3_add(p, dirs[d]);
                    uint8_t ntype = 255u;

                    if (!world_y_in_bounds(npos.y)) {
                        ntype = 255u;
                    } else if (npos.x >= base_x && npos.x < base_x + CHUNK_SIZE &&
                               npos.z >= base_z && npos.z < base_z + CHUNK_SIZE)
                    {
                        int nlx = npos.x - base_x;
                        int nlz = npos.z - base_z;
                        int nly = npos.y - WORLD_MIN_Y;
                        ntype = chunk_get_voxel(chunk, nlx, nly, nlz);
                    } else {
                        /* Cross-chunk neighbor: treat missing chunks as air */
                        if (!world_get_block_type(world, npos, &ntype)) {
                            ntype = 255u;
                        }
                    }

                    if (block_is_water(type)) {
                        if (block_is_air(ntype) || !block_is_water(ntype)) {
                            visible = true;
                            break;
                        }
                    } else {
                        if (block_is_air(ntype) || block_is_water(ntype)) {
                            visible = true;
                            break;
                        }
                    }
                }

                if (!visible) continue;

                chunk_ensure_capacity(chunk, chunk->block_count + 1);
                chunk->blocks[chunk->block_count].pos = p;
                chunk->blocks[chunk->block_count].type = type;
                chunk->block_count++;
            }
        }
    }

    chunk->render_dirty = false;
}

static bool chunk_load_from_worldsave(WorldSave *save, Chunk *chunk) {
    if (!save) return false;
    bool ok = world_save_fetch(save, chunk->cx, chunk->cz, chunk->voxels);
    if (ok) {
        chunk->dirty = false;
        chunk->render_dirty = true;
    }
    return ok;
}

static void chunk_store_to_worldsave(WorldSave *save, Chunk *chunk) {
    if (!save) return;
    world_save_store(save, chunk->cx, chunk->cz, chunk->voxels);
    chunk->dirty = false;
}

/* Editing ops */
static bool chunk_add_block(Chunk *chunk, IVec3 pos, uint8_t type) {
    if (!world_y_in_bounds(pos.y)) return false;

    int base_x = chunk_base_coord(chunk->cx);
    int base_z = chunk_base_coord(chunk->cz);

    int lx = pos.x - base_x;
    int lz = pos.z - base_z;
    int ly = pos.y - WORLD_MIN_Y;

    if (lx < 0 || lx >= CHUNK_SIZE ||
        lz < 0 || lz >= CHUNK_SIZE ||
        ly < 0 || ly >= CHUNK_HEIGHT)
    {
        return false;
    }

    if (!block_is_air(chunk_get_voxel(chunk, lx, ly, lz))) return false;

    chunk_set_voxel(chunk, lx, ly, lz, type);
    chunk->dirty = true;
    chunk->render_dirty = true;
    return true;
}

static bool chunk_remove_block(Chunk *chunk, IVec3 pos) {
    if (!world_y_in_bounds(pos.y)) return false;

    int base_x = chunk_base_coord(chunk->cx);
    int base_z = chunk_base_coord(chunk->cz);

    int lx = pos.x - base_x;
    int lz = pos.z - base_z;
    int ly = pos.y - WORLD_MIN_Y;

    if (lx < 0 || lx >= CHUNK_SIZE ||
        lz < 0 || lz >= CHUNK_SIZE ||
        ly < 0 || ly >= CHUNK_HEIGHT)
    {
        return false;
    }

    if (block_is_air(chunk_get_voxel(chunk, lx, ly, lz))) return false;

    chunk_set_voxel(chunk, lx, ly, lz, 255u);
    chunk->dirty = true;
    chunk->render_dirty = true;
    return true;
}

/* -------------------------------------------------------------------------- */
/* World Helpers                                                              */
/* -------------------------------------------------------------------------- */

static Chunk *world_find_chunk(World *world, int cx, int cz) {
    for (int i = 0; i < world->chunk_count; ++i) {
        if (world->chunks[i]->cx == cx && world->chunks[i]->cz == cz) return world->chunks[i];
    }
    return NULL;
}

static void world_try_set_spawn(World *world, Chunk *chunk) {
    if (world->spawn_set) return;

    int base_x = chunk_base_coord(chunk->cx);
    int base_z = chunk_base_coord(chunk->cz);

    if (0 < base_x || 0 >= base_x + CHUNK_SIZE ||
        0 < base_z || 0 >= base_z + CHUNK_SIZE)
    {
        return;
    }

    int lx = -base_x;
    int lz = -base_z;

    for (int ly = CHUNK_HEIGHT - 1; ly >= 0; --ly) {
        uint8_t type = chunk_get_voxel(chunk, lx, ly, lz);
        if (!block_is_air(type) && !block_is_water(type)) {
            float y = (float)(WORLD_MIN_Y + ly) + 0.5f;
            world->spawn_position = vec3(0.0f, y, 0.0f);
            world->spawn_set = true;
            break;
        }
    }
}

static void chunk_generate(World *world, Chunk *chunk);

static Chunk *world_get_or_create_chunk(World *world, int cx, int cz) {
    Chunk *existing = world_find_chunk(world, cx, cz);
    if (existing) return existing;

    Chunk *chunk = malloc(sizeof(Chunk));
    if (!chunk) die("Failed to allocate chunk");
    chunk_init(chunk, cx, cz);

    bool loaded = chunk_load_from_worldsave(world->save, chunk);
    if (!loaded) {
        chunk_generate(world, chunk);
        chunk->dirty = true;
        chunk->render_dirty = true;
    }

    world_try_set_spawn(world, chunk);

    if (world->chunk_count >= world->chunk_capacity) {
        int new_capacity = world->chunk_capacity > 0 ? world->chunk_capacity * 2 : 64;
        Chunk **new_chunks = realloc(world->chunks, (size_t)new_capacity * sizeof(Chunk *));
        if (!new_chunks) die("Failed to allocate chunk list");
        world->chunks = new_chunks;
        world->chunk_capacity = new_capacity;
    }

    world->chunks[world->chunk_count++] = chunk;

    if ((uint32_t)world->chunk_count > MAX_LOADED_CHUNKS) {
        die("Exceeded maximum loaded chunk count");
    }

    return chunk;
}

static void world_unload_chunk_index(World *world, int index) {
    Chunk *chunk = world->chunks[index];

    if (chunk->dirty) {
        chunk_store_to_worldsave(world->save, chunk);
    }

    chunk_destroy(chunk);

    world->chunks[index] = world->chunks[world->chunk_count - 1];
    world->chunk_count--;
}

void world_init(World *world, WorldSave *save) {
    world->chunks = NULL;
    world->chunk_count = 0;
    world->chunk_capacity = 0;
    world->spawn_position = vec3(0.0f, 4.5f, 0.0f);
    world->spawn_set = false;
    world->save = save;
}

void world_destroy(World *world) {
    for (int i = 0; i < world->chunk_count; ++i) {
        Chunk *chunk = world->chunks[i];
        if (chunk->dirty) {
            chunk_store_to_worldsave(world->save, chunk);
        }
        chunk_destroy(chunk);
    }
    free(world->chunks);
    world->chunks = NULL;
    world->chunk_count = 0;
    world->chunk_capacity = 0;
}

void world_update_chunks(World *world, Vec3 player_pos) {
    IVec3 center_cell = world_to_cell(player_pos);
    int center_cx = cell_to_chunk_coord(center_cell.x);
    int center_cz = cell_to_chunk_coord(center_cell.z);

    for (int dz = -ACTIVE_CHUNK_RADIUS; dz <= ACTIVE_CHUNK_RADIUS; ++dz) {
        for (int dx = -ACTIVE_CHUNK_RADIUS; dx <= ACTIVE_CHUNK_RADIUS; ++dx) {
            world_get_or_create_chunk(world, center_cx + dx, center_cz + dz);
        }
    }

    for (int i = 0; i < world->chunk_count; ) {
        Chunk *chunk = world->chunks[i];
        int dx = chunk->cx - center_cx;
        int dz = chunk->cz - center_cz;

        if (abs(dx) > ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN ||
            abs(dz) > ACTIVE_CHUNK_RADIUS + CHUNK_UNLOAD_MARGIN)
        {
            world_unload_chunk_index(world, i);
            continue;
        }

        ++i;
    }
}

bool world_get_block_type(World *world, IVec3 pos, uint8_t *type_out) {
    if (!world_y_in_bounds(pos.y)) return false;

    int cx = cell_to_chunk_coord(pos.x);
    int cz = cell_to_chunk_coord(pos.z);

    Chunk *chunk = world_find_chunk(world, cx, cz);
    if (!chunk) return false;

    int lx = pos.x - chunk_base_coord(cx);
    int lz = pos.z - chunk_base_coord(cz);
    int ly = pos.y - WORLD_MIN_Y;

    uint8_t type = chunk_get_voxel(chunk, lx, ly, lz);
    if (block_is_air(type)) return false;

    if (type_out) *type_out = type;
    return true;
}

bool world_block_exists(World *world, IVec3 pos) {
    return world_get_block_type(world, pos, NULL);
}

static void world_mark_chunk_and_neighbors_render_dirty(World *world, IVec3 pos) {
    int cx = cell_to_chunk_coord(pos.x);
    int cz = cell_to_chunk_coord(pos.z);
    Chunk *c = world_find_chunk(world, cx, cz);
    if (c) c->render_dirty = true;

    /* If on a chunk boundary, neighbor visibility may change too */
    int base_x = chunk_base_coord(cx);
    int base_z = chunk_base_coord(cz);
    int lx = pos.x - base_x;
    int lz = pos.z - base_z;

    if (lx == 0) {
        Chunk *n = world_find_chunk(world, cx - 1, cz);
        if (n) n->render_dirty = true;
    } else if (lx == CHUNK_SIZE - 1) {
        Chunk *n = world_find_chunk(world, cx + 1, cz);
        if (n) n->render_dirty = true;
    }

    if (lz == 0) {
        Chunk *n = world_find_chunk(world, cx, cz - 1);
        if (n) n->render_dirty = true;
    } else if (lz == CHUNK_SIZE - 1) {
        Chunk *n = world_find_chunk(world, cx, cz + 1);
        if (n) n->render_dirty = true;
    }
}

bool world_add_block(World *world, IVec3 pos, uint8_t type) {
    int cx = cell_to_chunk_coord(pos.x);
    int cz = cell_to_chunk_coord(pos.z);
    Chunk *chunk = world_get_or_create_chunk(world, cx, cz);

    bool result = chunk_add_block(chunk, pos, type);
    if (result) world_mark_chunk_and_neighbors_render_dirty(world, pos);
    return result;
}

bool world_remove_block(World *world, IVec3 pos) {
    int cx = cell_to_chunk_coord(pos.x);
    int cz = cell_to_chunk_coord(pos.z);
    Chunk *chunk = world_find_chunk(world, cx, cz);
    if (!chunk) return false;

    bool result = chunk_remove_block(chunk, pos);
    if (result) world_mark_chunk_and_neighbors_render_dirty(world, pos);
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
/* Chunk Generation                                                           */
/* -------------------------------------------------------------------------- */

static void chunk_generate(World *world, Chunk *chunk) {
    static const int SEA_LEVEL = 3;
    static const int BEDROCK_DEPTH = -4;

    int base_x = chunk_base_coord(chunk->cx);
    int base_z = chunk_base_coord(chunk->cz);

    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
        int world_x = base_x + lx;

        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            int world_z = base_z + lz;

            float fx = (float)world_x;
            float fz = (float)world_z;

            bool forced_plains = (abs(world_x) < 5 && abs(world_z) < 5);

            float base = fbm2d(fx * 0.045f, fz * 0.045f, 4, 2.0f, 0.5f, 1234u);
            float detail = fbm2d(fx * 0.12f, fz * 0.12f, 3, 2.15f, 0.5f, 5678u);
            float mountain_factor = fbm2d(fx * 0.02f, fz * 0.02f, 5, 2.0f, 0.45f, 91011u);
            float moisture = fbm2d(fx * 0.03f + 300.0f, fz * 0.03f - 300.0f, 4, 2.0f, 0.5f, 121314u);
            float heat = fbm2d(fx * 0.03f - 600.0f, fz * 0.03f + 600.0f, 4, 2.0f, 0.5f, 151617u);
            float dryness = heat - moisture;

            BlockType surface = BLOCK_GRASS;
            float height = 6.0f + base * 4.0f + detail * 2.5f;

            if (!forced_plains && mountain_factor > 0.45f) {
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
                surface = BLOCK_GRASS;
            }

            height = fmaxf(height, 0.5f);
            int ground_y = (int)floorf(height);

            float river_signal = fabsf(perlin2d(fx * 0.015f + 4000.0f, fz * 0.015f - 4000.0f, 272829u));
            bool is_river = !forced_plains && river_signal < 0.11f;

            if (is_river) {
                ground_y = (int)fminf((float)ground_y, (float)SEA_LEVEL - 1.0f);
                surface = BLOCK_SAND;
            }

            for (int y = BEDROCK_DEPTH; y < ground_y - 3; ++y) {
                chunk_add_block(chunk, (IVec3){world_x, y, world_z}, BLOCK_STONE);
            }

            for (int y = ground_y - 3; y < ground_y; ++y) {
                if (y < BEDROCK_DEPTH) continue;

                BlockType filler = BLOCK_DIRT;
                if (surface == BLOCK_SAND) filler = BLOCK_SAND;
                else if (surface == BLOCK_STONE) filler = BLOCK_STONE;

                chunk_add_block(chunk, (IVec3){world_x, y, world_z}, filler);
            }

            chunk_add_block(chunk, (IVec3){world_x, ground_y, world_z}, surface);

            bool fill_with_water = is_river || ground_y < SEA_LEVEL;
            if (fill_with_water) {
                for (int y = ground_y + 1; y <= SEA_LEVEL; ++y) {
                    chunk_add_block(chunk, (IVec3){world_x, y, world_z}, BLOCK_WATER);
                }
            }

            bool can_tree = (!forced_plains && !is_river && !fill_with_water && surface == BLOCK_GRASS);
            if (can_tree) {
                if (abs(world_x) > 3 || abs(world_z) > 3) {
                    if (lx >= 2 && lx <= CHUNK_SIZE - 3 && lz >= 2 && lz <= CHUNK_SIZE - 3) {
                        uint32_t tree_hash = hash_2d(world_x, world_z, 424242u);
                        if ((tree_hash % 100u) < 3u) {
                            int trunk_height = 4 + (int)((tree_hash >> 8) % 3u);
                            int top_y = ground_y + trunk_height;
                            if (top_y + 2 <= WORLD_MAX_Y) {
                                for (int ty = 1; ty <= trunk_height; ++ty) {
                                    chunk_add_block(chunk, (IVec3){world_x, ground_y + ty, world_z}, BLOCK_WOOD);
                                }

                                for (int y = top_y - 2; y <= top_y + 1; ++y) {
                                    int dy = y - top_y;
                                    for (int dx = -2; dx <= 2; ++dx) {
                                        for (int dz = -2; dz <= 2; ++dz) {
                                            int dist2 = dx * dx + dz * dz + dy * dy;
                                            if (dist2 > 6) continue;
                                            if (dx == 0 && dz == 0 && y <= top_y) continue;
                                            chunk_add_block(chunk, (IVec3){world_x + dx, y, world_z + dz}, BLOCK_LEAVES);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!world->spawn_set && world_x == 0 && world_z == 0) {
                world->spawn_position = vec3(0.0f, (float)ground_y + 0.5f, 0.0f);
                world->spawn_set = true;
            }
        }
    }
}
