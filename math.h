#ifndef MATH_H
#define MATH_H

#include <stdbool.h>
#include <stdint.h>

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

Vec3 vec3(float x, float y, float z);
float lerp(float a, float b, float t);
Mat4 mat4_identity(void);
Mat4 mat4_perspective(float fov_radians, float aspect, float z_near, float z_far);

Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);
float vec3_dot(Vec3 a, Vec3 b);
float vec3_length(Vec3 v);
Vec3 vec3_normalize(Vec3 v);
Vec3 vec3_cross(Vec3 a, Vec3 b);
Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up);

bool ivec3_equal(IVec3 a, IVec3 b);
IVec3 ivec3_add(IVec3 a, IVec3 b);
int sign_int(int value);

/* -------------------------------------------------------------------------- */
/* Noise Functions                                                            */
/* -------------------------------------------------------------------------- */

uint32_t hash_2d(int x, int y, uint32_t seed);
float fade(float t);
float gradient_dot(int ix, int iy, float x, float y, uint32_t seed);
float perlin2d(float x, float y, uint32_t seed);
float fbm2d(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed);

#endif /* MATH_H */