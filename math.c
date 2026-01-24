#include "math.h"
#include <math.h>

/* -------------------------------------------------------------------------- */
/* Math Helpers                                                               */
/* -------------------------------------------------------------------------- */

Vec3 vec3(float x, float y, float z) {
    Vec3 v = {x, y, z};
    return v;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Mat4 mat4_identity(void) {
    Mat4 m = {0};
    m.m[0] = 1.0f;  m.m[5] = 1.0f;  m.m[10] = 1.0f; m.m[15] = 1.0f;
    return m;
}

Mat4 mat4_perspective(float fov_radians, float aspect, float z_near, float z_far) {
    Mat4 m = {0};
    float tan_half_fov = tanf(fov_radians * 0.5f);

    m.m[0] = 1.0f / (aspect * tan_half_fov);
    m.m[5] = -1.0f / tan_half_fov;
    m.m[10] = z_far / (z_near - z_far);
    m.m[11] = -1.0f;
    m.m[14] = -(z_far * z_near) / (z_far - z_near);

    return m;
}

Vec3 vec3_add(Vec3 a, Vec3 b) {
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vec3 vec3_scale(Vec3 v, float s) {
    return vec3(v.x * s, v.y * s, v.z * s);
}

float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float vec3_length(Vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

Vec3 vec3_normalize(Vec3 v) {
    float len = vec3_length(v);
    if (len > 0.000001f) {
        return vec3_scale(v, 1.0f / len);
    }
    return v;
}

Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
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

bool ivec3_equal(IVec3 a, IVec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

IVec3 ivec3_add(IVec3 a, IVec3 b) {
    IVec3 result = {a.x + b.x, a.y + b.y, a.z + b.z};
    return result;
}

int sign_int(int value) {
    return (value > 0) - (value < 0);
}

/* -------------------------------------------------------------------------- */
/* Noise Functions                                                            */
/* -------------------------------------------------------------------------- */

uint32_t hash_2d(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h += seed * 374761393u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float gradient_dot(int ix, int iy, float x, float y, uint32_t seed) {
    const Vec2 gradients[8] = {
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

float perlin2d(float x, float y, uint32_t seed) {
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

float fbm2d(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed) {
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
