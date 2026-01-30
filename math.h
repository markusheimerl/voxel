#ifndef MATH_H
#define MATH_H

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Types                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct { float x, y; } Vec2;
typedef struct { float x, y, z; } Vec3;
typedef struct { int x, y, z; } IVec3;
typedef struct { float m[16]; } Mat4;

/* -------------------------------------------------------------------------- */
/* Vector Operations                                                          */
/* -------------------------------------------------------------------------- */

Vec3 vec3(float x, float y, float z);
Vec3 vec3_add(Vec3 a, Vec3 b);
Vec3 vec3_sub(Vec3 a, Vec3 b);
Vec3 vec3_scale(Vec3 v, float s);
Vec3 vec3_cross(Vec3 a, Vec3 b);
Vec3 vec3_normalize(Vec3 v);
float vec3_dot(Vec3 a, Vec3 b);
float vec3_length(Vec3 v);

IVec3 ivec3_add(IVec3 a, IVec3 b);
bool ivec3_equal(IVec3 a, IVec3 b);

/* -------------------------------------------------------------------------- */
/* Matrix Operations                                                          */
/* -------------------------------------------------------------------------- */

Mat4 mat4_identity(void);
Mat4 mat4_perspective(float fov_radians, float aspect, float near, float far);
Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up);

/* -------------------------------------------------------------------------- */
/* Utility Functions                                                          */
/* -------------------------------------------------------------------------- */

float lerp(float a, float b, float t);
int sign_int(int value);

/* -------------------------------------------------------------------------- */
/* Noise Generation                                                           */
/* -------------------------------------------------------------------------- */

uint32_t hash_2d(int x, int y, uint32_t seed);
float perlin2d(float x, float y, uint32_t seed);
float fbm2d(float x, float y, int octaves, float lacunarity, float gain, uint32_t seed);

#endif /* MATH_H */