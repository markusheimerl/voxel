#include "math.h"
#include <math.h>

/* -------------------------------------------------------------------------- */
/* Vector Operations                                                          */
/* -------------------------------------------------------------------------- */

Vec3 vec3(float x, float y, float z) {
    return (Vec3){x, y, z};
}

Vec3 vec3_add(Vec3 a, Vec3 b) {
    return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 vec3_scale(Vec3 v, float s) {
    return (Vec3){v.x * s, v.y * s, v.z * s};
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float vec3_length(Vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    return len > 1e-6f ? vec3_scale(v, 1.0f / len) : v;
}

IVec3 ivec3_add(IVec3 a, IVec3 b) {
    return (IVec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

bool ivec3_equal(IVec3 a, IVec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

/* -------------------------------------------------------------------------- */
/* Matrix Operations                                                          */
/* -------------------------------------------------------------------------- */

Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

Mat4 mat4_perspective(float fov_radians, float aspect, float near, float far) {
    float tan_half_fov = tanf(fov_radians * 0.5f);
    Mat4 m = {0};
    
    m.m[0]  = 1.0f / (aspect * tan_half_fov);
    m.m[5]  = -1.0f / tan_half_fov;
    m.m[10] = far / (near - far);
    m.m[11] = -1.0f;
    m.m[14] = -(far * near) / (far - near);
    
    return m;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 fwd = vec3_normalize(vec3_sub(center, eye));
    Vec3 side = vec3_normalize(vec3_cross(fwd, up));
    Vec3 up_actual = vec3_cross(side, fwd);
    
    Mat4 m = mat4_identity();
    m.m[0]  = side.x;      m.m[4]  = side.y;      m.m[8]  = side.z;
    m.m[1]  = up_actual.x; m.m[5]  = up_actual.y; m.m[9]  = up_actual.z;
    m.m[2]  = -fwd.x;      m.m[6]  = -fwd.y;      m.m[10] = -fwd.z;
    m.m[12] = -vec3_dot(side, eye);
    m.m[13] = -vec3_dot(up_actual, eye);
    m.m[14] =  vec3_dot(fwd, eye);
    
    return m;
}

/* -------------------------------------------------------------------------- */
/* Utility Functions                                                          */
/* -------------------------------------------------------------------------- */

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

int sign_int(int value) {
    return (value > 0) - (value < 0);
}

/* -------------------------------------------------------------------------- */
/* Noise Generation                                                           */
/* -------------------------------------------------------------------------- */

uint32_t hash_2d(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float gradient_dot(int ix, int iy, float x, float y, uint32_t seed) {
    static const Vec2 gradients[8] = {
        { 1.0f,  0.0f}, {-1.0f,  0.0f}, { 0.0f,  1.0f}, { 0.0f, -1.0f},
        { 0.70710678f,  0.70710678f}, {-0.70710678f,  0.70710678f},
        { 0.70710678f, -0.70710678f}, {-0.70710678f, -0.70710678f}
    };
    
    Vec2 g = gradients[hash_2d(ix, iy, seed) & 7u];
    float dx = x - (float)ix;
    float dy = y - (float)iy;
    return dx * g.x + dy * g.y;
}

float perlin2d(float x, float y, uint32_t seed) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float sx = fade(x - (float)x0);
    float sy = fade(y - (float)y0);
    
    float n00 = gradient_dot(x0,     y0,     x, y, seed);
    float n10 = gradient_dot(x0 + 1, y0,     x, y, seed);
    float n01 = gradient_dot(x0,     y0 + 1, x, y, seed);
    float n11 = gradient_dot(x0 + 1, y0 + 1, x, y, seed);
    
    return lerp(lerp(n00, n10, sx), lerp(n01, n11, sx), sy);
}

float fbm2d(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed) {
    float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, total = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        sum += perlin2d(x * frequency, y * frequency, seed + (uint32_t)i * 1013u) * amplitude;
        total += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    
    return total > 0.0f ? sum / total : sum;
}