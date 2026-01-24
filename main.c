#include <errno.h>
#include <math.h>
#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>

/* -------------------------------------------------------------------------- */
/* Macros                                                                     */
/* -------------------------------------------------------------------------- */

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

#define VK_CHECK(call)                                                            \
    do {                                                                          \
        VkResult vk_check_result__ = (call);                                      \
        if (vk_check_result__ != VK_SUCCESS) {                                    \
            fprintf(stderr, "%s failed: %s\n", #call, vk_result_to_string(vk_check_result__)); \
            exit(EXIT_FAILURE);                                                   \
        }                                                                         \
    } while (0)

static void die(const char *message);

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

/* -------------------------------------------------------------------------- */
/* Math Types                                                                 */
/* -------------------------------------------------------------------------- */

typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y; } Vec2;
typedef struct { float m[16]; } Mat4;
typedef struct { int x, y, z; } IVec3;

/* -------------------------------------------------------------------------- */
/* Math Helpers                                                               */
/* -------------------------------------------------------------------------- */

static Vec3 vec3(float x, float y, float z) {
    Vec3 v = {x, y, z};
    return v;
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = 1.0f;  m.m[5] = 1.0f;  m.m[10] = 1.0f; m.m[15] = 1.0f;
    return m;
}

static Mat4 mat4_perspective(float fov_radians, float aspect, float z_near, float z_far) {
    Mat4 m = {0};
    float tan_half_fov = tanf(fov_radians * 0.5f);

    m.m[0] = 1.0f / (aspect * tan_half_fov);
    m.m[5] = -1.0f / tan_half_fov;
    m.m[10] = z_far / (z_near - z_far);
    m.m[11] = -1.0f;
    m.m[14] = -(z_far * z_near) / (z_far - z_near);

    return m;
}

static Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static Vec3 vec3_scale(Vec3 v, float s) {
    return vec3(v.x * s, v.y * s, v.z * s);
}

static float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static float vec3_length(Vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

static Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len > 0.000001f) {
        return vec3_scale(v, 1.0f / len);
    }
    return v;
}

static Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

static Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 forward = vec3_normalize(vec3_sub(center, eye));
    Vec3 side = vec3_normalize(vec3_cross(forward, up));
    Vec3 up_actual = vec3_cross(side, forward);

    Mat4 m = mat4_identity();
    m.m[0]  = side.x;      m.m[4]  = side.y;      m.m[8]  = side.z;
    m.m[1]  = up_actual.x; m.m[5]  = up_actual.y; m.m[9]  = up_actual.z;
    m.m[2]  = -forward.x;  m.m[6]  = -forward.y;  m.m[10] = -forward.z;

    m.m[12] = -vec3_dot(side, eye);
    m.m[13] = -vec3_dot(up_actual, eye);
    m.m[14] =  vec3_dot(forward, eye);

    return m;
}

static bool ivec3_equal(IVec3 a, IVec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static IVec3 ivec3_add(IVec3 a, IVec3 b) {
    IVec3 result = {a.x + b.x, a.y + b.y, a.z + b.z};
    return result;
}

static int sign_int(int value) {
    return (value > 0) - (value < 0);
}

static IVec3 world_to_cell(Vec3 p) {
    IVec3 cell = {
        (int)floorf(p.x + 0.5f),
        (int)floorf(p.y + 0.5f),
        (int)floorf(p.z + 0.5f)
    };
    return cell;
}

static bool world_y_in_bounds(int y) {
    return y >= WORLD_MIN_Y && y <= WORLD_MAX_Y;
}

/* -------------------------------------------------------------------------- */
/* Noise Functions                                                            */
/* -------------------------------------------------------------------------- */

static uint32_t hash_2d(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h += seed * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float gradient_dot(int ix, int iy, float x, float y, uint32_t seed) {
    static const Vec2 gradients[8] = {
        { 1.0f,  0.0f}, {-1.0f,  0.0f},
        { 0.0f,  1.0f}, { 0.0f, -1.0f},
        { 0.70710678f,  0.70710678f},
        {-0.70710678f,  0.70710678f},
        { 0.70710678f, -0.70710678f},
        {-0.70710678f, -0.70710678f}
    };

    uint32_t h = hash_2d(ix, iy, seed);
    Vec2 g = gradients[h & 7u];

    float dx = x - (float)ix;
    float dy = y - (float)iy;
    return dx * g.x + dy * g.y;
}

static float perlin2d(float x, float y, uint32_t seed) {
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float sx = x - (float)x0;
    float sy = y - (float)y0;

    float n0 = gradient_dot(x0, y0, x, y, seed);
    float n1 = gradient_dot(x1, y0, x, y, seed);
    float ix0 = lerp(n0, n1, fade(sx));

    n0 = gradient_dot(x0, y1, x, y, seed);
    n1 = gradient_dot(x1, y1, x, y, seed);
    float ix1 = lerp(n0, n1, fade(sx));

    return lerp(ix0, ix1, fade(sy));
}

static float fbm2d(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed) {
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        float noise = perlin2d(x * frequency, y * frequency, seed + (uint32_t)i * 1013u);
        sum += noise * amplitude;
        norm += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    if (norm > 0.0f) {
        sum /= norm;
    }

    return sum;
}

/* -------------------------------------------------------------------------- */
/* Camera                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;
    Vec3 front;
    Vec3 up;
    Vec3 right;
    Vec3 world_up;
    float yaw;
    float pitch;
    float movement_speed;
    float mouse_sensitivity;
} Camera;

static void camera_update_axes(Camera *cam) {
    float yaw_rad = cam->yaw   * (float)M_PI / 180.0f;
    float pitch_rad = cam->pitch * (float)M_PI / 180.0f;

    Vec3 front = vec3(cosf(yaw_rad) * cosf(pitch_rad),
                      sinf(pitch_rad),
                      sinf(yaw_rad) * cosf(pitch_rad));
    cam->front = vec3_normalize(front);
    cam->right = vec3_normalize(vec3_cross(cam->front, cam->world_up));
    cam->up    = vec3_normalize(vec3_cross(cam->right, cam->front));
}

static void camera_init(Camera *cam) {
    cam->position         = vec3(0.0f, 0.0f, 3.0f);
    cam->world_up         = vec3(0.0f, 1.0f, 0.0f);
    cam->yaw              = -90.0f;
    cam->pitch            = 0.0f;
    cam->movement_speed   = 6.0f;
    cam->mouse_sensitivity = 0.1f;
    camera_update_axes(cam);
}

static void camera_process_mouse(Camera *cam, float x_offset, float y_offset) {
    x_offset *= cam->mouse_sensitivity;
    y_offset *= cam->mouse_sensitivity;

    cam->yaw   += x_offset;
    cam->pitch += y_offset;

    if (cam->pitch > 89.0f)  cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;

    camera_update_axes(cam);
}

static Mat4 camera_view_matrix(Camera *cam) {
    Vec3 target = vec3_add(cam->position, cam->front);
    return mat4_look_at(cam->position, target, cam->up);
}

/* -------------------------------------------------------------------------- */
/* Player                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    Vec3 position;    /* feet center */
    float velocity_y;
    bool on_ground;
} Player;

typedef struct {
    Vec3 min;
    Vec3 max;
} AABB;

static void player_compute_aabb(Vec3 pos, AABB *aabb) {
    const float half_width = 0.4f;
    const float height = 1.8f;

    aabb->min = vec3(pos.x - half_width, pos.y, pos.z - half_width);
    aabb->max = vec3(pos.x + half_width, pos.y + height, pos.z + half_width);
}

/* -------------------------------------------------------------------------- */
/* Blocks                                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    BLOCK_DIRT  = 0,
    BLOCK_STONE = 1,
    BLOCK_GRASS = 2,
    BLOCK_SAND  = 3,
    BLOCK_WATER = 4,
    BLOCK_TYPE_COUNT
} BlockType;

enum { HIGHLIGHT_TEXTURE_INDEX = BLOCK_STONE };

typedef struct {
    IVec3 pos;
    uint8_t type;
} Block;

static AABB cell_aabb(IVec3 cell) {
    AABB box;
    box.min = vec3(cell.x - 0.5f, cell.y - 0.5f, cell.z - 0.5f);
    box.max = vec3(cell.x + 0.5f, cell.y + 0.5f, cell.z + 0.5f);
    return box;
}

static bool block_is_air(uint8_t type) { return type == 255u; }
static bool block_is_water(uint8_t type) { return type == BLOCK_WATER; }

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

static size_t chunk_voxel_count(void) {
    return (size_t)CHUNK_SIZE * (size_t)CHUNK_HEIGHT * (size_t)CHUNK_SIZE;
}

static int world_save_find_index(const WorldSave *save, int cx, int cz) {
    for (int i = 0; i < save->count; ++i) {
        if (save->records[i].cx == cx && save->records[i].cz == cz) return i;
    }
    return -1;
}

static void world_save_init(WorldSave *save, const char *path) {
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

static bool world_save_load(WorldSave *save) {
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

static void world_save_flush(WorldSave *save) {
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

static void world_save_destroy(WorldSave *save) {
    world_save_flush(save);
    world_save_free_records(save);
    *save = (WorldSave){0};
}

/* -------------------------------------------------------------------------- */
/* World / Chunk Structures                                                   */
/* -------------------------------------------------------------------------- */

typedef struct Chunk Chunk;
typedef struct World World;

struct Chunk {
    int cx;
    int cz;
    uint8_t *voxels;

    /* Render list: only blocks that likely have at least one visible face */
    Block *blocks;
    int block_count;
    int block_capacity;

    bool dirty;        /* needs saving */
    bool render_dirty; /* needs rebuild render list */
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

/* Forward declarations for cross-chunk neighbor lookups */
static bool world_get_block_type(World *world, IVec3 pos, uint8_t *type_out);

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

static void world_init(World *world, WorldSave *save) {
    world->chunks = NULL;
    world->chunk_count = 0;
    world->chunk_capacity = 0;
    world->spawn_position = vec3(0.0f, 4.5f, 0.0f);
    world->spawn_set = false;
    world->save = save;
}

static void world_destroy(World *world) {
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

static void world_update_chunks(World *world, Vec3 player_pos) {
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

static bool world_get_block_type(World *world, IVec3 pos, uint8_t *type_out) {
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

static bool world_block_exists(World *world, IVec3 pos) {
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

static bool world_add_block(World *world, IVec3 pos, uint8_t type) {
    int cx = cell_to_chunk_coord(pos.x);
    int cz = cell_to_chunk_coord(pos.z);
    Chunk *chunk = world_get_or_create_chunk(world, cx, cz);

    bool result = chunk_add_block(chunk, pos, type);
    if (result) world_mark_chunk_and_neighbors_render_dirty(world, pos);
    return result;
}

static bool world_remove_block(World *world, IVec3 pos) {
    int cx = cell_to_chunk_coord(pos.x);
    int cz = cell_to_chunk_coord(pos.z);
    Chunk *chunk = world_find_chunk(world, cx, cz);
    if (!chunk) return false;

    bool result = chunk_remove_block(chunk, pos);
    if (result) world_mark_chunk_and_neighbors_render_dirty(world, pos);
    return result;
}

static int world_total_render_blocks(World *world) {
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

            if (!world->spawn_set && world_x == 0 && world_z == 0) {
                world->spawn_position = vec3(0.0f, (float)ground_y + 0.5f, 0.0f);
                world->spawn_set = true;
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Utility Functions                                                          */
/* -------------------------------------------------------------------------- */

static bool is_key_pressed(const bool *keys, KeySym sym) {
    if (sym < 256) return keys[sym];
    return false;
}

/* -------------------------------------------------------------------------- */
/* Raycast                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct {
    bool hit;
    IVec3 cell;
    IVec3 normal;
    uint8_t type;
} RayHit;

static RayHit raycast_blocks(World *world, Vec3 origin, Vec3 direction, float max_distance) {
    const float step = 0.05f;
    RayHit result = {0};
    Vec3 dir = vec3_normalize(direction);

    IVec3 previous_cell = world_to_cell(origin);

    for (float t = 0.0f; t <= max_distance; t += step) {
        Vec3 point = vec3_add(origin, vec3_scale(dir, t));
        IVec3 cell = world_to_cell(point);

        if (!ivec3_equal(cell, previous_cell)) {
            uint8_t type = 0;
            if (world_get_block_type(world, cell, &type)) {
                result.hit = true;
                result.cell = cell;
                result.normal = (IVec3){
                    sign_int(previous_cell.x - cell.x),
                    sign_int(previous_cell.y - cell.y),
                    sign_int(previous_cell.z - cell.z)
                };
                result.type = type;
                break;
            }
            previous_cell = cell;
        }
    }

    return result;
}

/* -------------------------------------------------------------------------- */
/* Player Collision Helpers                                                   */
/* -------------------------------------------------------------------------- */

static void resolve_collision_axis(World *world, Vec3 *position, float delta, int axis) {
    if (delta == 0.0f) return;

    AABB player_box;
    player_compute_aabb(*position, &player_box);

    int min_x = (int)floorf(player_box.min.x - 0.5f);
    int max_x = (int)floorf(player_box.max.x + 0.5f);
    int min_y = (int)floorf(player_box.min.y - 0.5f);
    int max_y = (int)floorf(player_box.max.y + 0.5f);
    int min_z = (int)floorf(player_box.min.z - 0.5f);
    int max_z = (int)floorf(player_box.max.z + 0.5f);

    for (int x = min_x; x <= max_x; ++x) {
        for (int y = min_y; y <= max_y; ++y) {
            if (!world_y_in_bounds(y)) continue;
            for (int z = min_z; z <= max_z; ++z) {
                uint8_t type = 0;
                if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                if (type == BLOCK_WATER) continue;

                player_compute_aabb(*position, &player_box);
                AABB block_box = cell_aabb((IVec3){x, y, z});

                if (!((player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
                      (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
                      (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z)))
                {
                    continue;
                }

                switch (axis) {
                case 0: /* X axis */
                    if (delta > 0.0f) position->x = block_box.min.x - 0.4f - 0.001f;
                    else position->x = block_box.max.x + 0.4f + 0.001f;
                    break;
                case 2: /* Z axis */
                    if (delta > 0.0f) position->z = block_box.min.z - 0.4f - 0.001f;
                    else position->z = block_box.max.z + 0.4f + 0.001f;
                    break;
                default:
                    break;
                }

                player_compute_aabb(*position, &player_box);
            }
        }
    }
}

static void resolve_collision_y(World *world, Vec3 *position, float *velocity_y, bool *on_ground) {
    AABB player_box;
    player_compute_aabb(*position, &player_box);
    *on_ground = false;

    int min_x = (int)floorf(player_box.min.x - 0.5f);
    int max_x = (int)floorf(player_box.max.x + 0.5f);
    int min_y = (int)floorf(player_box.min.y - 0.5f);
    int max_y = (int)floorf(player_box.max.y + 0.5f);
    int min_z = (int)floorf(player_box.min.z - 0.5f);
    int max_z = (int)floorf(player_box.max.z + 0.5f);

    for (int x = min_x; x <= max_x; ++x) {
        for (int y = min_y; y <= max_y; ++y) {
            if (!world_y_in_bounds(y)) continue;
            for (int z = min_z; z <= max_z; ++z) {
                uint8_t type = 0;
                if (!world_get_block_type(world, (IVec3){x, y, z}, &type)) continue;
                if (type == BLOCK_WATER) continue;

                player_compute_aabb(*position, &player_box);
                AABB block_box = cell_aabb((IVec3){x, y, z});

                if (!((player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
                      (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
                      (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z)))
                {
                    continue;
                }

                if (*velocity_y < 0.0f) {
                    position->y = block_box.max.y;
                    *velocity_y = 0.0f;
                    *on_ground = true;
                } else if (*velocity_y > 0.0f) {
                    position->y = block_box.min.y - 1.8f - 0.001f;
                    *velocity_y = 0.0f;
                }

                player_compute_aabb(*position, &player_box);
            }
        }
    }
}

static bool block_overlaps_player(const Player *player, IVec3 cell) {
    AABB player_box;
    player_compute_aabb(player->position, &player_box);

    AABB block_box = cell_aabb(cell);
    bool intersect =
        (player_box.min.x < block_box.max.x && player_box.max.x > block_box.min.x) &&
        (player_box.min.y < block_box.max.y && player_box.max.y > block_box.min.y) &&
        (player_box.min.z < block_box.max.z && player_box.max.z > block_box.min.z);

    return intersect;
}

/* -------------------------------------------------------------------------- */
/* Vulkan Error Helpers                                                       */
/* -------------------------------------------------------------------------- */

static const char *vk_result_to_string(VkResult result) {
    switch (result) {
    case VK_SUCCESS:                                 return "VK_SUCCESS";
    case VK_NOT_READY:                               return "VK_NOT_READY";
    case VK_TIMEOUT:                                 return "VK_TIMEOUT";
    case VK_EVENT_SET:                               return "VK_EVENT_SET";
    case VK_EVENT_RESET:                             return "VK_EVENT_RESET";
    case VK_INCOMPLETE:                              return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:                return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:              return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:             return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:                       return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:                 return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:                 return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:             return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:               return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:               return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:                  return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:              return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:                  return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:          return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:                          return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:                   return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:          return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:             return "VK_ERROR_VALIDATION_FAILED_EXT";
    default:                                         return "UNKNOWN_VULKAN_ERROR";
    }
}

static void die(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */
/* Vulkan Buffer / Image Helpers                                              */
/* -------------------------------------------------------------------------- */

static uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                 uint32_t type_filter,
                                 VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        bool supported = (type_filter & (1 << i)) != 0;
        bool matches = (memory_properties.memoryTypes[i].propertyFlags & properties) == properties;
        if (supported && matches) return i;
    }

    die("Failed to find suitable memory type");
    return 0;
}

static void create_buffer(VkDevice device,
                          VkPhysicalDevice physical_device,
                          VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer *buffer,
                          VkDeviceMemory *memory) {
    VkBufferCreateInfo buffer_info = {0};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(device, &buffer_info, NULL, buffer));

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                  requirements.memoryTypeBits,
                                                  properties);

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindBufferMemory(device, *buffer, *memory, 0));
}

static void create_image(VkDevice device,
                         VkPhysicalDevice physical_device,
                         uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         VkImageTiling tiling,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties,
                         VkImage *image,
                         VkDeviceMemory *memory) {
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(device, &image_info, NULL, image));

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, *image, &requirements);

    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(physical_device,
                                                  requirements.memoryTypeBits,
                                                  properties);

    VK_CHECK(vkAllocateMemory(device, &alloc_info, NULL, memory));
    VK_CHECK(vkBindImageMemory(device, *image, *memory, 0));
}

static VkCommandBuffer begin_single_use_commands(VkDevice device, VkCommandPool command_pool) {
    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &alloc_info, &command_buffer));

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));
    return command_buffer;
}

static void end_single_use_commands(VkDevice device, VkCommandPool command_pool,
                                    VkQueue graphics_queue, VkCommandBuffer command_buffer) {
    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(graphics_queue));

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

static void transition_image_layout(VkDevice device,
                                    VkCommandPool command_pool,
                                    VkQueue graphics_queue,
                                    VkImage image,
                                    VkFormat format,
                                    VkImageLayout old_layout,
                                    VkImageLayout new_layout) {
    (void)format;

    VkCommandBuffer command_buffer = begin_single_use_commands(device, command_pool);

    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = 0;
    VkPipelineStageFlags dst_stage = 0;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        die("Unsupported image layout transition");
    }

    vkCmdPipelineBarrier(command_buffer,
                         src_stage, dst_stage,
                         0,
                         0, NULL,
                         0, NULL,
                         1, &barrier);

    end_single_use_commands(device, command_pool, graphics_queue, command_buffer);
}

static void copy_buffer_to_image(VkDevice device,
                                 VkCommandPool command_pool,
                                 VkQueue graphics_queue,
                                 VkBuffer buffer,
                                 VkImage image,
                                 uint32_t width,
                                 uint32_t height) {
    VkCommandBuffer command_buffer = begin_single_use_commands(device, command_pool);

    VkBufferImageCopy region = {0};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
    region.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(command_buffer,
                           buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    end_single_use_commands(device, command_pool, graphics_queue, command_buffer);
}

/* -------------------------------------------------------------------------- */
/* Texture Handling                                                           */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    uint32_t width;
    uint32_t height;
} Texture;

typedef struct {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
} ImageData;

static bool load_png_rgba(const char *filename, ImageData *out_image) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return false; }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    png_byte color_type = png_get_color_type(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    size_t row_bytes = png_get_rowbytes(png, info);
    uint8_t *pixels = malloc(row_bytes * height);
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_bytep *row_pointers = malloc(sizeof(png_bytep) * height);
    if (!row_pointers) {
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    for (png_uint_32 y = 0; y < height; ++y) {
        row_pointers[y] = pixels + y * row_bytes;
    }

    png_read_image(png, row_pointers);
    png_read_end(png, NULL);

    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    out_image->pixels = pixels;
    out_image->width = (uint32_t)width;
    out_image->height = (uint32_t)height;
    return true;
}

static void free_image(ImageData *image) {
    free(image->pixels);
    *image = (ImageData){0};
}

static VkSampler create_nearest_sampler(VkDevice device) {
    VkSamplerCreateInfo sampler_info = {0};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSampler sampler;
    VK_CHECK(vkCreateSampler(device, &sampler_info, NULL, &sampler));
    return sampler;
}

static void texture_create_from_pixels(VkDevice device,
                                       VkPhysicalDevice physical_device,
                                       VkCommandPool command_pool,
                                       VkQueue graphics_queue,
                                       const uint8_t *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       Texture *texture) {
    VkDeviceSize image_size = (VkDeviceSize)width * (VkDeviceSize)height * 4;

    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    create_buffer(device,
                  physical_device,
                  image_size,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &staging_buffer,
                  &staging_memory);

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, staging_memory, 0, image_size, 0, &mapped));
    memcpy(mapped, pixels, (size_t)image_size);
    vkUnmapMemory(device, staging_memory);

    create_image(device,
                 physical_device,
                 width,
                 height,
                 VK_FORMAT_R8G8B8A8_SRGB,
                 VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 &texture->image,
                 &texture->memory);

    transition_image_layout(device, command_pool, graphics_queue,
                            texture->image, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_buffer_to_image(device, command_pool, graphics_queue,
                         staging_buffer, texture->image, width, height);

    transition_image_layout(device, command_pool, graphics_queue,
                            texture->image, VK_FORMAT_R8G8B8A8_SRGB,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device, staging_buffer, NULL);
    vkFreeMemory(device, staging_memory, NULL);

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device, &view_info, NULL, &texture->view));

    texture->sampler = create_nearest_sampler(device);
    texture->width = width;
    texture->height = height;
}

static void texture_create_from_file(VkDevice device,
                                     VkPhysicalDevice physical_device,
                                     VkCommandPool command_pool,
                                     VkQueue graphics_queue,
                                     const char *filename,
                                     Texture *texture) {
    ImageData image = {0};
    if (!load_png_rgba(filename, &image)) die("Failed to load PNG texture");

    texture_create_from_pixels(device, physical_device, command_pool, graphics_queue,
                               image.pixels, image.width, image.height, texture);

    free_image(&image);
}

static void texture_create_solid(VkDevice device,
                                 VkPhysicalDevice physical_device,
                                 VkCommandPool command_pool,
                                 VkQueue graphics_queue,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                 Texture *texture) {
    uint8_t pixel[4] = {r, g, b, a};
    texture_create_from_pixels(device, physical_device, command_pool, graphics_queue,
                               pixel, 1, 1, texture);
}

static void texture_destroy(VkDevice device, Texture *texture) {
    vkDestroySampler(device, texture->sampler, NULL);
    vkDestroyImageView(device, texture->view, NULL);
    vkDestroyImage(device, texture->image, NULL);
    vkFreeMemory(device, texture->memory, NULL);
    *texture = (Texture){0};
}

/* -------------------------------------------------------------------------- */
/* Shader Helpers                                                             */
/* -------------------------------------------------------------------------- */

static char *read_binary_file(const char *filepath, size_t *out_size) {
    FILE *file = fopen(filepath, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) { fclose(file); return NULL; }

    char *buffer = malloc((size_t)file_size);
    if (!buffer) { fclose(file); return NULL; }

    size_t read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if (read != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    *out_size = (size_t)file_size;
    return buffer;
}

static VkShaderModule create_shader_module(VkDevice device, const char *filepath) {
    size_t size = 0;
    char *code = read_binary_file(filepath, &size);
    if (!code) die("Failed to read shader file");

    VkShaderModuleCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = (const uint32_t *)code;

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &create_info, NULL, &module));

    free(code);
    return module;
}

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

/* -------------------------------------------------------------------------- */
/* Vulkan Swapchain Resources                                                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkSwapchainKHR swapchain;
    VkImage *images;
    VkImageView *image_views;
    VkFramebuffer *framebuffers;
    VkRenderPass render_pass;
    VkPipeline pipeline_solid;
    VkPipeline pipeline_wireframe;
    VkPipeline pipeline_crosshair;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet *descriptor_sets_normal;
    VkDescriptorSet *descriptor_sets_highlight;

    VkCommandBuffer *command_buffers;

    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    uint32_t image_count;
    VkExtent2D extent;
    VkFormat format;
} SwapchainResources;

static void swapchain_resources_reset(SwapchainResources *res) {
    *res = (SwapchainResources){0};
}

static void swapchain_destroy(VkDevice device,
                              VkCommandPool command_pool,
                              SwapchainResources *res) {
    if (res->command_buffers) {
        vkFreeCommandBuffers(device, command_pool, res->image_count, res->command_buffers);
        free(res->command_buffers);
        res->command_buffers = NULL;
    }

    if (res->descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, res->descriptor_pool, NULL);
        res->descriptor_pool = VK_NULL_HANDLE;
    }

    if (res->framebuffers) {
        for (uint32_t i = 0; i < res->image_count; ++i) {
            vkDestroyFramebuffer(device, res->framebuffers[i], NULL);
        }
        free(res->framebuffers);
        res->framebuffers = NULL;
    }

    if (res->pipeline_crosshair != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, res->pipeline_crosshair, NULL);
        res->pipeline_crosshair = VK_NULL_HANDLE;
    }
    if (res->pipeline_wireframe != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, res->pipeline_wireframe, NULL);
        res->pipeline_wireframe = VK_NULL_HANDLE;
    }
    if (res->pipeline_solid != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, res->pipeline_solid, NULL);
        res->pipeline_solid = VK_NULL_HANDLE;
    }

    if (res->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, res->render_pass, NULL);
        res->render_pass = VK_NULL_HANDLE;
    }

    if (res->depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, res->depth_view, NULL);
        res->depth_view = VK_NULL_HANDLE;
    }
    if (res->depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, res->depth_image, NULL);
        res->depth_image = VK_NULL_HANDLE;
    }
    if (res->depth_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, res->depth_memory, NULL);
        res->depth_memory = VK_NULL_HANDLE;
    }

    if (res->image_views) {
        for (uint32_t i = 0; i < res->image_count; ++i) {
            vkDestroyImageView(device, res->image_views[i], NULL);
        }
        free(res->image_views);
        res->image_views = NULL;
    }

    free(res->images);
    res->images = NULL;

    if (res->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, res->swapchain, NULL);
        res->swapchain = VK_NULL_HANDLE;
    }

    free(res->descriptor_sets_normal);
    free(res->descriptor_sets_highlight);
    res->descriptor_sets_normal = NULL;
    res->descriptor_sets_highlight = NULL;

    res->image_count = 0;
    res->extent = (VkExtent2D){0, 0};
    res->format = VK_FORMAT_UNDEFINED;
}

static void create_depth_resources(VkDevice device,
                                   VkPhysicalDevice physical_device,
                                   VkExtent2D extent,
                                   VkImage *image,
                                   VkDeviceMemory *memory,
                                   VkImageView *view) {
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    create_image(device,
                 physical_device,
                 extent.width,
                 extent.height,
                 depth_format,
                 VK_IMAGE_TILING_OPTIMAL,
                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 image,
                 memory);

    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = *image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(device, &view_info, NULL, view));
}

/* -------------------------------------------------------------------------- */
/* Swapchain creation                                                         */
/* -------------------------------------------------------------------------- */

typedef struct {
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkQueue graphics_queue;
    VkCommandPool command_pool;
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    const Texture *textures;
    uint32_t texture_count;
    const Texture *black_texture;
} SwapchainContext;

static void swapchain_create(SwapchainContext *ctx,
                             SwapchainResources *res,
                             uint32_t framebuffer_width,
                             uint32_t framebuffer_height) {
    VkPhysicalDevice physical_device = ctx->physical_device;
    VkDevice device = ctx->device;
    VkSurfaceKHR surface = ctx->surface;

    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities));

    uint32_t format_count = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL));
    VkSurfaceFormatKHR *formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats));

    VkSurfaceFormatKHR surface_format = formats[0];
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surface_format = formats[i];
            break;
        }
    }
    free(formats);

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = framebuffer_width;
        extent.height = framebuffer_height;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {0};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &res->swapchain));

    VK_CHECK(vkGetSwapchainImagesKHR(device, res->swapchain, &image_count, NULL));
    res->images = malloc(sizeof(VkImage) * image_count);
    VK_CHECK(vkGetSwapchainImagesKHR(device, res->swapchain, &image_count, res->images));
    res->image_count = image_count;
    res->extent = extent;
    res->format = surface_format.format;

    res->image_views = malloc(sizeof(VkImageView) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo view_info = {0};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = res->images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = surface_format.format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &view_info, NULL, &res->image_views[i]));
    }

    create_depth_resources(device, physical_device, extent,
                           &res->depth_image, &res->depth_memory, &res->depth_view);

    VkAttachmentDescription attachments[2] = {0};

    attachments[0].format = surface_format.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref = {0};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref = {0};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {0};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = ARRAY_LENGTH(attachments);
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(device, &render_pass_info, NULL, &res->render_pass));

    VkPipelineShaderStageCreateInfo shader_stages[2] = {0};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = ctx->vert_shader;
    shader_stages[0].pName = "main";

    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = ctx->frag_shader;
    shader_stages[1].pName = "main";

    /* Vertex binding 0: per-vertex; binding 1: per-instance */
    VkVertexInputBindingDescription bindings[2] = {0};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(Vertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindings[1].binding = 1;
    bindings[1].stride = sizeof(InstanceData);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attributes[4] = {0};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, pos);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, uv);

    attributes[2].binding = 1;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = offsetof(InstanceData, x);

    attributes[3].binding = 1;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32_UINT;
    attributes[3].offset = offsetof(InstanceData, type);

    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = ARRAY_LENGTH(bindings);
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = ARRAY_LENGTH(attributes);
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_tri = {0};
    input_assembly_tri.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_tri.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_lines = input_assembly_tri;
    input_assembly_lines.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkViewport viewport = {0};
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster_solid = {0};
    raster_solid.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_solid.polygonMode = VK_POLYGON_MODE_FILL;
    raster_solid.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_solid.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_solid.lineWidth = 3.0f;

    VkPipelineRasterizationStateCreateInfo raster_wire = raster_solid;
    raster_wire.cullMode = VK_CULL_MODE_NONE;

    VkPipelineRasterizationStateCreateInfo raster_cross = raster_wire;

    VkPipelineMultisampleStateCreateInfo multisample = {0};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_solid = {0};
    depth_solid.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_solid.depthTestEnable = VK_TRUE;
    depth_solid.depthWriteEnable = VK_TRUE;
    depth_solid.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineDepthStencilStateCreateInfo depth_wire = depth_solid;
    depth_wire.depthWriteEnable = VK_FALSE;
    depth_wire.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineDepthStencilStateCreateInfo depth_cross = {0};
    depth_cross.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_cross.depthTestEnable = VK_FALSE;
    depth_cross.depthWriteEnable = VK_FALSE;
    depth_cross.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState color_blend = {0};
    color_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_state = {0};
    color_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_state.attachmentCount = 1;
    color_state.pAttachments = &color_blend;

    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = ARRAY_LENGTH(shader_stages);
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_state;
    pipeline_info.layout = ctx->pipeline_layout;
    pipeline_info.renderPass = res->render_pass;

    pipeline_info.pInputAssemblyState = &input_assembly_tri;
    pipeline_info.pRasterizationState = &raster_solid;
    pipeline_info.pDepthStencilState = &depth_solid;

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &res->pipeline_solid));

    pipeline_info.pInputAssemblyState = &input_assembly_lines;
    pipeline_info.pRasterizationState = &raster_wire;
    pipeline_info.pDepthStencilState = &depth_wire;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &res->pipeline_wireframe));

    pipeline_info.pRasterizationState = &raster_cross;
    pipeline_info.pDepthStencilState = &depth_cross;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &res->pipeline_crosshair));

    res->framebuffers = malloc(sizeof(VkFramebuffer) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageView attachments_views[] = {res->image_views[i], res->depth_view};

        VkFramebufferCreateInfo framebuffer_info = {0};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = res->render_pass;
        framebuffer_info.attachmentCount = ARRAY_LENGTH(attachments_views);
        framebuffer_info.pAttachments = attachments_views;
        framebuffer_info.width = extent.width;
        framebuffer_info.height = extent.height;
        framebuffer_info.layers = 1;

        VK_CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &res->framebuffers[i]));
    }

    /* Descriptors: only sampler array */
    VkDescriptorPoolSize pool_size = {0};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = image_count * ctx->texture_count * 2;

    VkDescriptorPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = image_count * 2;

    VK_CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &res->descriptor_pool));

    VkDescriptorSetLayout *layouts = malloc(sizeof(VkDescriptorSetLayout) * image_count);
    for (uint32_t i = 0; i < image_count; ++i) layouts[i] = ctx->descriptor_set_layout;

    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = res->descriptor_pool;
    alloc_info.descriptorSetCount = image_count;
    alloc_info.pSetLayouts = layouts;

    res->descriptor_sets_normal = malloc(sizeof(VkDescriptorSet) * image_count);
    res->descriptor_sets_highlight = malloc(sizeof(VkDescriptorSet) * image_count);

    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, res->descriptor_sets_normal));
    VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, res->descriptor_sets_highlight));

    free(layouts);

    for (uint32_t i = 0; i < image_count; ++i) {
        VkDescriptorImageInfo normal_images[BLOCK_TYPE_COUNT];
        VkDescriptorImageInfo highlight_images[BLOCK_TYPE_COUNT];

        for (uint32_t tex = 0; tex < ctx->texture_count; ++tex) {
            normal_images[tex].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normal_images[tex].imageView = ctx->textures[tex].view;
            normal_images[tex].sampler = ctx->textures[tex].sampler;

            highlight_images[tex] = normal_images[tex];
        }

        if (ctx->texture_count > HIGHLIGHT_TEXTURE_INDEX) {
            highlight_images[HIGHLIGHT_TEXTURE_INDEX].imageView = ctx->black_texture->view;
            highlight_images[HIGHLIGHT_TEXTURE_INDEX].sampler = ctx->black_texture->sampler;
        }

        VkWriteDescriptorSet write = {0};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = ctx->texture_count;

        write.dstSet = res->descriptor_sets_normal[i];
        write.pImageInfo = normal_images;
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

        write.dstSet = res->descriptor_sets_highlight[i];
        write.pImageInfo = highlight_images;
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    }

    res->command_buffers = malloc(sizeof(VkCommandBuffer) * image_count);

    VkCommandBufferAllocateInfo command_alloc = {0};
    command_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_alloc.commandPool = ctx->command_pool;
    command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_alloc.commandBufferCount = image_count;

    VK_CHECK(vkAllocateCommandBuffers(device, &command_alloc, res->command_buffers));
}

/* -------------------------------------------------------------------------- */
/* Main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void) {
    uint32_t window_width = 800;
    uint32_t window_height = 600;

    /* ---------------------------------------------------------------------- */
    /* X11 Setup                                                              */
    /* ---------------------------------------------------------------------- */

    Display *display = XOpenDisplay(NULL);
    if (!display) die("Failed to open X11 display");

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window window = XCreateSimpleWindow(display, root,
                                        0, 0,
                                        window_width,
                                        window_height,
                                        1,
                                        BlackPixel(display, screen),
                                        WhitePixel(display, screen));

    XStoreName(display, window, "Voxel Engine");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 PointerMotionMask | StructureNotifyMask | ButtonPressMask);

    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);
    XMapWindow(display, window);

    char empty_cursor_data[8] = {0};
    Pixmap blank = XCreateBitmapFromData(display, window, empty_cursor_data, 8, 8);
    Colormap colormap = DefaultColormap(display, screen);
    XColor black, dummy;
    XAllocNamedColor(display, colormap, "black", &black, &dummy);
    Cursor invisible_cursor = XCreatePixmapCursor(display, blank, blank, &black, &black, 0, 0);
    XFreePixmap(display, blank);

    /* ---------------------------------------------------------------------- */
    /* Vulkan Instance and Surface                                            */
    /* ---------------------------------------------------------------------- */

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Voxel Engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_1;

    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME
    };

    VkInstanceCreateInfo instance_info = {0};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app_info;
    instance_info.enabledExtensionCount = ARRAY_LENGTH(instance_extensions);
    instance_info.ppEnabledExtensionNames = instance_extensions;

    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_info, NULL, &instance));

    VkXlibSurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surface_info.dpy = display;
    surface_info.window = window;

    VkSurfaceKHR surface;
    VK_CHECK(vkCreateXlibSurfaceKHR(instance, &surface_info, NULL, &surface));

    /* ---------------------------------------------------------------------- */
    /* Physical Device and Logical Device                                     */
    /* ---------------------------------------------------------------------- */

    uint32_t device_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, NULL));
    if (device_count == 0) die("No Vulkan-capable GPU found");

    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &device_count, physical_devices));

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    uint32_t graphics_family = UINT32_MAX;

    for (uint32_t i = 0; i < device_count; ++i) {
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);

        VkQueueFamilyProperties *families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, families);

        for (uint32_t j = 0; j < queue_family_count; ++j) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present);

            bool has_graphics = (families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (has_graphics && present) {
                physical_device = physical_devices[i];
                graphics_family = j;
                break;
            }
        }

        free(families);
        if (physical_device != VK_NULL_HANDLE) break;
    }

    free(physical_devices);

    if (physical_device == VK_NULL_HANDLE) die("No suitable GPU found");

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = graphics_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(physical_device, &supported_features);

    VkPhysicalDeviceFeatures enabled_features = {0};
    enabled_features.wideLines = supported_features.wideLines ? VK_TRUE : VK_FALSE;

    VkDeviceCreateInfo device_info = {0};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = ARRAY_LENGTH(device_extensions);
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &enabled_features;

    VkDevice device;
    VK_CHECK(vkCreateDevice(physical_device, &device_info, NULL, &device));

    VkQueue graphics_queue;
    vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);

    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.queueFamilyIndex = graphics_family;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_info, NULL, &command_pool));

    /* ---------------------------------------------------------------------- */
    /* Textures                                                                */
    /* ---------------------------------------------------------------------- */

    Texture textures[BLOCK_TYPE_COUNT];
    const char *texture_files[BLOCK_TYPE_COUNT] = {
        "dirt.png",
        "stone.png",
        "grass.png",
        "sand.png",
        "water.png"
    };

    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; ++i) {
        texture_create_from_file(device, physical_device, command_pool, graphics_queue,
                                 texture_files[i], &textures[i]);
    }

    Texture black_texture;
    texture_create_solid(device, physical_device, command_pool, graphics_queue, 0, 0, 0, 255, &black_texture);

    /* ---------------------------------------------------------------------- */
    /* Geometry Buffers (static)                                               */
    /* ---------------------------------------------------------------------- */

    VkBuffer block_vertex_buffer;
    VkDeviceMemory block_vertex_memory;
    create_buffer(device, physical_device, sizeof(BLOCK_VERTICES),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &block_vertex_buffer, &block_vertex_memory);

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(device, block_vertex_memory, 0, sizeof(BLOCK_VERTICES), 0, &mapped));
    memcpy(mapped, BLOCK_VERTICES, sizeof(BLOCK_VERTICES));
    vkUnmapMemory(device, block_vertex_memory);

    VkBuffer block_index_buffer;
    VkDeviceMemory block_index_memory;
    create_buffer(device, physical_device, sizeof(BLOCK_INDICES),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &block_index_buffer, &block_index_memory);

    VK_CHECK(vkMapMemory(device, block_index_memory, 0, sizeof(BLOCK_INDICES), 0, &mapped));
    memcpy(mapped, BLOCK_INDICES, sizeof(BLOCK_INDICES));
    vkUnmapMemory(device, block_index_memory);

    VkBuffer edge_vertex_buffer;
    VkDeviceMemory edge_vertex_memory;
    create_buffer(device, physical_device, sizeof(EDGE_VERTICES),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &edge_vertex_buffer, &edge_vertex_memory);

    VK_CHECK(vkMapMemory(device, edge_vertex_memory, 0, sizeof(EDGE_VERTICES), 0, &mapped));
    memcpy(mapped, EDGE_VERTICES, sizeof(EDGE_VERTICES));
    vkUnmapMemory(device, edge_vertex_memory);

    VkBuffer edge_index_buffer;
    VkDeviceMemory edge_index_memory;
    create_buffer(device, physical_device, sizeof(EDGE_INDICES),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &edge_index_buffer, &edge_index_memory);

    VK_CHECK(vkMapMemory(device, edge_index_memory, 0, sizeof(EDGE_INDICES), 0, &mapped));
    memcpy(mapped, EDGE_INDICES, sizeof(EDGE_INDICES));
    vkUnmapMemory(device, edge_index_memory);

    /* Crosshair vertices (in clip space, updated on resize to keep aspect) */
    VkBuffer crosshair_vertex_buffer;
    VkDeviceMemory crosshair_vertex_memory;
    create_buffer(device, physical_device, sizeof(Vertex) * 4,
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &crosshair_vertex_buffer, &crosshair_vertex_memory);

    /* Instance buffer (blocks + highlight + crosshair) */
    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkDeviceMemory instance_memory = VK_NULL_HANDLE;
    uint32_t instance_capacity = INITIAL_INSTANCE_CAPACITY;

    create_buffer(device, physical_device,
                  (VkDeviceSize)instance_capacity * sizeof(InstanceData),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  &instance_buffer, &instance_memory);

    /* ---------------------------------------------------------------------- */
    /* Shaders, Descriptor Layout, Pipeline Layout                            */
    /* ---------------------------------------------------------------------- */

    VkShaderModule vert_shader = create_shader_module(device, "shaders/vert.spv");
    VkShaderModule frag_shader = create_shader_module(device, "shaders/frag.spv");

    /* NOTE: shaders are expected to use:
       - set=0 binding=0 sampler2D texSamplers[BLOCK_TYPE_COUNT]
       - vertex inputs: location 0/1 for Vertex, 2/3 for InstanceData
       - push constants: mat4 view, mat4 proj
    */

    VkDescriptorSetLayoutBinding sampler_binding = {0};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = BLOCK_TYPE_COUNT;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &sampler_binding;

    VkDescriptorSetLayout descriptor_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layout_info, NULL, &descriptor_layout));

    VkPushConstantRange push_range = {0};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_range;

    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout));

    /* ---------------------------------------------------------------------- */
    /* Swapchain Resources (lazy init later)                                  */
    /* ---------------------------------------------------------------------- */

    SwapchainResources swapchain;
    swapchain_resources_reset(&swapchain);

    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;

    VkSemaphoreCreateInfo semaphore_info = {0};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &image_available));
    VK_CHECK(vkCreateSemaphore(device, &semaphore_info, NULL, &render_finished));
    VK_CHECK(vkCreateFence(device, &fence_info, NULL, &in_flight));

    bool swapchain_needs_recreate = true;

    SwapchainContext swapchain_ctx = {
        .device = device,
        .physical_device = physical_device,
        .surface = surface,
        .graphics_queue = graphics_queue,
        .command_pool = command_pool,
        .descriptor_set_layout = descriptor_layout,
        .pipeline_layout = pipeline_layout,
        .vert_shader = vert_shader,
        .frag_shader = frag_shader,
        .textures = textures,
        .texture_count = BLOCK_TYPE_COUNT,
        .black_texture = &black_texture
    };

    /* ---------------------------------------------------------------------- */
    /* World Save + Voxel World                                                */
    /* ---------------------------------------------------------------------- */

    WorldSave save;
    world_save_init(&save, WORLD_SAVE_FILE);
    (void)world_save_load(&save);

    World world;
    world_init(&world, &save);
    world_update_chunks(&world, world.spawn_position);
    if (!world.spawn_set) world.spawn_position = vec3(0.0f, 4.5f, 0.0f);

    /* ---------------------------------------------------------------------- */
    /* Player / Camera                                                        */
    /* ---------------------------------------------------------------------- */

    const float GRAVITY = 17.0f;
    const float JUMP_HEIGHT = 1.2f;
    const float JUMP_VELOCITY = sqrtf(2.0f * GRAVITY * JUMP_HEIGHT);
    const float EYE_HEIGHT = 1.6f;

    Player player = {0};
    player.position = world.spawn_position;

    Camera camera;
    camera_init(&camera);
    camera.position = vec3_add(player.position, vec3(0.0f, EYE_HEIGHT, 0.0f));

    world_update_chunks(&world, player.position);

    /* Input State */
    bool keys[256] = {0};
    bool mouse_captured = false;
    bool first_mouse = true;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;
    uint8_t current_block_type = BLOCK_DIRT;

    struct timespec last_time;
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    struct timespec last_autosave;
    clock_gettime(CLOCK_MONOTONIC, &last_autosave);

    /* ---------------------------------------------------------------------- */
    /* Main Loop                                                               */
    /* ---------------------------------------------------------------------- */

    bool running = true;

    while (running) {
        if (swapchain_needs_recreate) {
            vkDeviceWaitIdle(device);

            XWindowAttributes attrs;
            XGetWindowAttributes(display, window, &attrs);
            if (attrs.width == 0 || attrs.height == 0) {
                swapchain_needs_recreate = false;
                continue;
            }

            window_width = (uint32_t)attrs.width;
            window_height = (uint32_t)attrs.height;

            swapchain_destroy(device, command_pool, &swapchain);
            swapchain_create(&swapchain_ctx, &swapchain, window_width, window_height);

            /* Update crosshair vertices to keep constant size in screen space */
            const float crosshair_size = 0.03f;
            float aspect_correction = (float)swapchain.extent.height / (float)swapchain.extent.width;

            Vertex crosshair_vertices[] = {
                {{-crosshair_size * aspect_correction, 0.0f, 0.0f}, {0.0f, 0.0f}},
                {{ crosshair_size * aspect_correction, 0.0f, 0.0f}, {1.0f, 0.0f}},
                {{ 0.0f, -crosshair_size, 0.0f}, {0.0f, 0.0f}},
                {{ 0.0f,  crosshair_size, 0.0f}, {1.0f, 0.0f}},
            };

            void *ch = NULL;
            VK_CHECK(vkMapMemory(device, crosshair_vertex_memory, 0, sizeof(crosshair_vertices), 0, &ch));
            memcpy(ch, crosshair_vertices, sizeof(crosshair_vertices));
            vkUnmapMemory(device, crosshair_vertex_memory);

            swapchain_needs_recreate = false;
        }

        world_update_chunks(&world, player.position);

        bool mouse_moved = false;
        float mouse_x = 0.0f;
        float mouse_y = 0.0f;
        bool left_click = false;
        bool right_click = false;

        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);

            switch (event.type) {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == wm_delete_window) running = false;
                break;

            case ConfigureNotify:
                if (event.xconfigure.width != (int)window_width ||
                    event.xconfigure.height != (int)window_height)
                {
                    swapchain_needs_recreate = true;
                }
                break;

            case KeyPress: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym == XK_Escape) {
                    if (mouse_captured) {
                        XUngrabPointer(display, CurrentTime);
                        XUndefineCursor(display, window);
                        mouse_captured = false;
                        first_mouse = true;
                    }
                } else if (sym == XK_1) current_block_type = BLOCK_DIRT;
                else if (sym == XK_2) current_block_type = BLOCK_STONE;
                else if (sym == XK_3) current_block_type = BLOCK_GRASS;
                else if (sym == XK_4) current_block_type = BLOCK_SAND;
                else if (sym == XK_5) current_block_type = BLOCK_WATER;
                else if (sym < 256) keys[sym] = true;
            } break;

            case KeyRelease: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym < 256) keys[sym] = false;
            } break;

            case MotionNotify:
                if (mouse_captured) {
                    mouse_x = (float)event.xmotion.x;
                    mouse_y = (float)event.xmotion.y;
                    mouse_moved = true;
                }
                break;

            case ButtonPress:
                if (event.xbutton.button == Button1) {
                    if (!mouse_captured) {
                        XGrabPointer(display, window, True, PointerMotionMask,
                                     GrabModeAsync, GrabModeAsync, window, None, CurrentTime);
                        XDefineCursor(display, window, invisible_cursor);
                        mouse_captured = true;
                        first_mouse = true;
                    } else {
                        left_click = true;
                    }
                } else if (event.xbutton.button == Button3) {
                    if (mouse_captured) right_click = true;
                }
                break;

            default:
                break;
            }
        }

        if (mouse_captured && mouse_moved) {
            int center_x = (int)window_width / 2;
            int center_y = (int)window_height / 2;

            if (first_mouse) {
                last_mouse_x = (float)center_x;
                last_mouse_y = (float)center_y;
                first_mouse = false;
            }

            float x_offset = mouse_x - last_mouse_x;
            float y_offset = last_mouse_y - mouse_y;

            camera_process_mouse(&camera, x_offset, y_offset);

            XWarpPointer(display, None, window, 0, 0, 0, 0, center_x, center_y);
            XFlush(display);

            last_mouse_x = (float)center_x;
            last_mouse_y = (float)center_y;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        float delta_time =
            (now.tv_sec - last_time.tv_sec) +
            (now.tv_nsec - last_time.tv_nsec) / 1000000000.0f;
        last_time = now;

        Vec3 move_delta = vec3(0.0f, 0.0f, 0.0f);
        bool wants_jump = false;

        if (mouse_captured) {
            Vec3 forward = camera.front;
            forward.y = 0.0f;
            forward = vec3_normalize(forward);

            Vec3 right = camera.right;
            right.y = 0.0f;
            right = vec3_normalize(right);

            Vec3 movement_dir = vec3(0.0f, 0.0f, 0.0f);
            if (is_key_pressed(keys, 'w') || is_key_pressed(keys, 'W')) movement_dir = vec3_add(movement_dir, forward);
            if (is_key_pressed(keys, 's') || is_key_pressed(keys, 'S')) movement_dir = vec3_sub(movement_dir, forward);
            if (is_key_pressed(keys, 'a') || is_key_pressed(keys, 'A')) movement_dir = vec3_sub(movement_dir, right);
            if (is_key_pressed(keys, 'd') || is_key_pressed(keys, 'D')) movement_dir = vec3_add(movement_dir, right);

            if (vec3_length(movement_dir) > 0.0f) movement_dir = vec3_normalize(movement_dir);

            move_delta = vec3_scale(movement_dir, camera.movement_speed * delta_time);
            wants_jump = is_key_pressed(keys, ' ') || is_key_pressed(keys, XK_space);
        }

        if (wants_jump && player.on_ground) {
            player.velocity_y = JUMP_VELOCITY;
            player.on_ground = false;
        }

        player.velocity_y -= GRAVITY * delta_time;

        player.position.x += move_delta.x;
        resolve_collision_axis(&world, &player.position, move_delta.x, 0);

        player.position.z += move_delta.z;
        resolve_collision_axis(&world, &player.position, move_delta.z, 2);

        player.position.y += player.velocity_y * delta_time;
        resolve_collision_y(&world, &player.position, &player.velocity_y, &player.on_ground);

        camera.position = vec3_add(player.position, vec3(0.0f, EYE_HEIGHT, 0.0f));

        RayHit ray_hit = raycast_blocks(&world, camera.position, camera.front, 6.0f);

        if (mouse_captured && (left_click || right_click)) {
            if (ray_hit.hit) {
                if (left_click) world_remove_block(&world, ray_hit.cell);

                if (right_click) {
                    if (!(ray_hit.normal.x == 0 && ray_hit.normal.y == 0 && ray_hit.normal.z == 0)) {
                        IVec3 place = ivec3_add(ray_hit.cell, ray_hit.normal);
                        if (!world_block_exists(&world, place) && !block_overlaps_player(&player, place)) {
                            world_add_block(&world, place, current_block_type);
                        }
                    }
                }
            }
        }

        /* Periodic autosave (single-file) */
        double since_autosave =
            (now.tv_sec - last_autosave.tv_sec) +
            (now.tv_nsec - last_autosave.tv_nsec) / 1000000000.0;

        if (since_autosave > 5.0) {
            world_save_flush(&save);
            last_autosave = now;
        }

        if (swapchain_needs_recreate) continue;

        VK_CHECK(vkWaitForFences(device, 1, &in_flight, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(device, 1, &in_flight));

        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device,
                                                 swapchain.swapchain,
                                                 UINT64_MAX,
                                                 image_available,
                                                 VK_NULL_HANDLE,
                                                 &image_index);

        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_needs_recreate = true;
            continue;
        } else if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
            die("Failed to acquire swapchain image");
        }

        /* Build instance list */
        int render_blocks = world_total_render_blocks(&world);
        uint32_t highlight_instance_index = (uint32_t)render_blocks;
        uint32_t crosshair_instance_index = (uint32_t)render_blocks + 1;
        uint32_t total_instances = (uint32_t)render_blocks + 2;

        if (total_instances > instance_capacity) {
            uint32_t new_cap = instance_capacity;
            while (new_cap < total_instances) new_cap *= 2;
            if (new_cap > MAX_INSTANCE_CAPACITY) die("Exceeded maximum instance buffer capacity");

            vkDeviceWaitIdle(device);
            vkDestroyBuffer(device, instance_buffer, NULL);
            vkFreeMemory(device, instance_memory, NULL);

            instance_capacity = new_cap;
            create_buffer(device, physical_device,
                          (VkDeviceSize)instance_capacity * sizeof(InstanceData),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          &instance_buffer, &instance_memory);
        }

        InstanceData *instances = NULL;
        VK_CHECK(vkMapMemory(device, instance_memory, 0,
                             (VkDeviceSize)total_instances * sizeof(InstanceData),
                             0, (void **)&instances));

        uint32_t w = 0;
        for (int ci = 0; ci < world.chunk_count; ++ci) {
            Chunk *chunk = world.chunks[ci];
            for (int bi = 0; bi < chunk->block_count; ++bi) {
                Block b = chunk->blocks[bi];
                instances[w++] = (InstanceData){ (float)b.pos.x, (float)b.pos.y, (float)b.pos.z, (uint32_t)b.type };
            }
        }

        /* Highlight + crosshair instances */
        if (ray_hit.hit) {
            instances[highlight_instance_index] = (InstanceData){
                (float)ray_hit.cell.x, (float)ray_hit.cell.y, (float)ray_hit.cell.z, (uint32_t)HIGHLIGHT_TEXTURE_INDEX
            };
        } else {
            instances[highlight_instance_index] = (InstanceData){0,0,0,(uint32_t)HIGHLIGHT_TEXTURE_INDEX};
        }
        instances[crosshair_instance_index] = (InstanceData){0,0,0,(uint32_t)HIGHLIGHT_TEXTURE_INDEX};

        vkUnmapMemory(device, instance_memory);

        /* Matrices */
        PushConstants pc = {0};
        pc.view = camera_view_matrix(&camera);
        pc.proj = mat4_perspective(55.0f * (float)M_PI / 180.0f,
                                   (float)swapchain.extent.width / (float)swapchain.extent.height,
                                   0.1f,
                                   200.0f);

        PushConstants pc_identity = {0};
        pc_identity.view = mat4_identity();
        pc_identity.proj = mat4_identity();

        VK_CHECK(vkResetCommandBuffer(swapchain.command_buffers[image_index], 0));

        VkCommandBufferBeginInfo begin_info = {0};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(swapchain.command_buffers[image_index], &begin_info));

        VkClearValue clears[2];
        clears[0].color.float32[0] = 0.1f;
        clears[0].color.float32[1] = 0.12f;
        clears[0].color.float32[2] = 0.18f;
        clears[0].color.float32[3] = 1.0f;
        clears[1].depthStencil.depth = 1.0f;

        VkRenderPassBeginInfo rp_begin = {0};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.renderPass = swapchain.render_pass;
        rp_begin.framebuffer = swapchain.framebuffers[image_index];
        rp_begin.renderArea.extent = swapchain.extent;
        rp_begin.clearValueCount = ARRAY_LENGTH(clears);
        rp_begin.pClearValues = clears;

        vkCmdBeginRenderPass(swapchain.command_buffers[image_index], &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

        VkDeviceSize offsets[2] = {0, 0};
        VkBuffer vbs[2];

        /* Draw all blocks in one instanced draw */
        vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_solid);
        vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        vbs[0] = block_vertex_buffer;
        vbs[1] = instance_buffer;
        vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);
        vkCmdBindIndexBuffer(swapchain.command_buffers[image_index], block_index_buffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout,
                                0,
                                1,
                                &swapchain.descriptor_sets_normal[image_index],
                                0,
                                NULL);

        if (render_blocks > 0) {
            vkCmdDrawIndexed(swapchain.command_buffers[image_index],
                             (uint32_t)ARRAY_LENGTH(BLOCK_INDICES),
                             (uint32_t)render_blocks,
                             0, 0, 0);
        }

        /* Highlight wireframe (1 instance, different mesh/indices) */
        if (ray_hit.hit) {
            vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_wireframe);
            vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

            vbs[0] = edge_vertex_buffer;
            vbs[1] = instance_buffer;
            vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);
            vkCmdBindIndexBuffer(swapchain.command_buffers[image_index], edge_index_buffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0,
                                    1,
                                    &swapchain.descriptor_sets_highlight[image_index],
                                    0,
                                    NULL);

            vkCmdDrawIndexed(swapchain.command_buffers[image_index],
                             (uint32_t)ARRAY_LENGTH(EDGE_INDICES),
                             1,
                             0, 0, highlight_instance_index);
        }

        /* Crosshair (lines, clip-space geometry, identity matrices) */
        vkCmdBindPipeline(swapchain.command_buffers[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, swapchain.pipeline_crosshair);
        vkCmdPushConstants(swapchain.command_buffers[image_index], pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_identity), &pc_identity);

        vbs[0] = crosshair_vertex_buffer;
        vbs[1] = instance_buffer;
        vkCmdBindVertexBuffers(swapchain.command_buffers[image_index], 0, 2, vbs, offsets);

        vkCmdBindDescriptorSets(swapchain.command_buffers[image_index],
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_layout,
                                0,
                                1,
                                &swapchain.descriptor_sets_highlight[image_index],
                                0,
                                NULL);

        vkCmdDraw(swapchain.command_buffers[image_index], 4, 1, 0, crosshair_instance_index);

        vkCmdEndRenderPass(swapchain.command_buffers[image_index]);
        VK_CHECK(vkEndCommandBuffer(swapchain.command_buffers[image_index]));

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit = {0};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &swapchain.command_buffers[image_index];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &render_finished;

        VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submit, in_flight));

        VkPresentInfoKHR present = {0};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain.swapchain;
        present.pImageIndices = &image_index;

        VkResult present_result = vkQueuePresentKHR(graphics_queue, &present);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
            swapchain_needs_recreate = true;
        } else if (present_result != VK_SUCCESS) {
            die("Failed to present swapchain image");
        }
    }

    /* ---------------------------------------------------------------------- */
    /* Cleanup                                                                 */
    /* ---------------------------------------------------------------------- */

    vkDeviceWaitIdle(device);

    /* Flush all chunks into the single save file */
    world_destroy(&world);
    world_save_destroy(&save);

    swapchain_destroy(device, command_pool, &swapchain);

    vkDestroyFence(device, in_flight, NULL);
    vkDestroySemaphore(device, render_finished, NULL);
    vkDestroySemaphore(device, image_available, NULL);

    vkDestroyBuffer(device, instance_buffer, NULL);
    vkFreeMemory(device, instance_memory, NULL);

    vkDestroyBuffer(device, crosshair_vertex_buffer, NULL);
    vkFreeMemory(device, crosshair_vertex_memory, NULL);

    vkDestroyBuffer(device, edge_vertex_buffer, NULL);
    vkFreeMemory(device, edge_vertex_memory, NULL);
    vkDestroyBuffer(device, edge_index_buffer, NULL);
    vkFreeMemory(device, edge_index_memory, NULL);

    vkDestroyBuffer(device, block_vertex_buffer, NULL);
    vkFreeMemory(device, block_vertex_memory, NULL);
    vkDestroyBuffer(device, block_index_buffer, NULL);
    vkFreeMemory(device, block_index_memory, NULL);

    for (uint32_t i = 0; i < BLOCK_TYPE_COUNT; ++i) {
        texture_destroy(device, &textures[i]);
    }
    texture_destroy(device, &black_texture);

    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);

    vkDestroyShaderModule(device, vert_shader, NULL);
    vkDestroyShaderModule(device, frag_shader, NULL);

    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    XFreeCursor(display, invisible_cursor);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return 0;
}