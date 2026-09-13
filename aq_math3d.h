/*
 * Aquamarine - RinOS 2D/3D Graphics Library
 * aq_math3d.h - 3D math (vectors, matrices, quaternions)
 *
 * All operations use fixed-point 16.16 arithmetic (no floating point).
 */

#ifndef AQ_MATH3D_H
#define AQ_MATH3D_H

#include "aq_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * 3D Vector (fixed-point 16.16)
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    aq_fixed_t x, y, z;
} AqVec3;

typedef struct {
    aq_fixed_t x, y, z, w;
} AqVec4;

#define AQ_VEC3(xv,yv,zv)     ((AqVec3){(xv),(yv),(zv)})
#define AQ_VEC3_ZERO           AQ_VEC3(0, 0, 0)
#define AQ_VEC3_ONE            AQ_VEC3(AQ_FIXED_ONE, AQ_FIXED_ONE, AQ_FIXED_ONE)
#define AQ_VEC3I(x,y,z)       AQ_VEC3(AQ_INT_TO_FIXED(x), AQ_INT_TO_FIXED(y), AQ_INT_TO_FIXED(z))

#define AQ_VEC4(xv,yv,zv,wv)  ((AqVec4){(xv),(yv),(zv),(wv)})

static inline AqVec3 aq_vec3_add(AqVec3 a, AqVec3 b) {
    return AQ_VEC3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline AqVec3 aq_vec3_sub(AqVec3 a, AqVec3 b) {
    return AQ_VEC3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline AqVec3 aq_vec3_scale(AqVec3 v, aq_fixed_t s) {
    return AQ_VEC3(aq_fixed_mul(v.x, s), aq_fixed_mul(v.y, s), aq_fixed_mul(v.z, s));
}

static inline AqVec3 aq_vec3_neg(AqVec3 v) {
    return AQ_VEC3(-v.x, -v.y, -v.z);
}

static inline aq_fixed_t aq_vec3_dot(AqVec3 a, AqVec3 b) {
    return aq_fixed_mul(a.x, b.x) + aq_fixed_mul(a.y, b.y) + aq_fixed_mul(a.z, b.z);
}

static inline AqVec3 aq_vec3_cross(AqVec3 a, AqVec3 b) {
    return AQ_VEC3(
        aq_fixed_mul(a.y, b.z) - aq_fixed_mul(a.z, b.y),
        aq_fixed_mul(a.z, b.x) - aq_fixed_mul(a.x, b.z),
        aq_fixed_mul(a.x, b.y) - aq_fixed_mul(a.y, b.x)
    );
}

static inline aq_fixed_t aq_vec3_length_sq(AqVec3 v) {
    return aq_vec3_dot(v, v);
}

static inline aq_fixed_t aq_vec3_length(AqVec3 v) {
    int64_t sq = (int64_t)v.x * v.x + (int64_t)v.y * v.y + (int64_t)v.z * v.z;
    sq >>= AQ_FIXED_SHIFT; /* scale back */
    return (aq_fixed_t)aq_isqrt((int32_t)sq) << (AQ_FIXED_SHIFT / 2);
}

static inline AqVec3 aq_vec3_normalize(AqVec3 v) {
    aq_fixed_t len = aq_vec3_length(v);
    if (len == 0) return AQ_VEC3_ZERO;
    return AQ_VEC3(
        aq_fixed_div(v.x, len),
        aq_fixed_div(v.y, len),
        aq_fixed_div(v.z, len)
    );
}

static inline AqVec3 aq_vec3_lerp(AqVec3 a, AqVec3 b, aq_fixed_t t) {
    aq_fixed_t it = AQ_FIXED_ONE - t;
    return AQ_VEC3(
        aq_fixed_mul(a.x, it) + aq_fixed_mul(b.x, t),
        aq_fixed_mul(a.y, it) + aq_fixed_mul(b.y, t),
        aq_fixed_mul(a.z, it) + aq_fixed_mul(b.z, t)
    );
}

/* ═══════════════════════════════════════════════════════════════
 * 4x4 Matrix (fixed-point 16.16, column-major)
 * m[col][row], i.e. m[0] = first column
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    aq_fixed_t m[4][4]; /* m[col][row] */
} AqMat4;

static inline AqMat4 aq_mat4_identity(void) {
    AqMat4 r = {{{0}}};
    r.m[0][0] = AQ_FIXED_ONE;
    r.m[1][1] = AQ_FIXED_ONE;
    r.m[2][2] = AQ_FIXED_ONE;
    r.m[3][3] = AQ_FIXED_ONE;
    return r;
}

static inline AqMat4 aq_mat4_mul(AqMat4 a, AqMat4 b) {
    AqMat4 r = {{{0}}};
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            aq_fixed_t sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += aq_fixed_mul(a.m[k][row], b.m[c][k]);
            }
            r.m[c][row] = sum;
        }
    }
    return r;
}

static inline AqVec4 aq_mat4_mul_vec4(AqMat4 m, AqVec4 v) {
    AqVec4 r;
    r.x = aq_fixed_mul(m.m[0][0], v.x) + aq_fixed_mul(m.m[1][0], v.y) +
          aq_fixed_mul(m.m[2][0], v.z) + aq_fixed_mul(m.m[3][0], v.w);
    r.y = aq_fixed_mul(m.m[0][1], v.x) + aq_fixed_mul(m.m[1][1], v.y) +
          aq_fixed_mul(m.m[2][1], v.z) + aq_fixed_mul(m.m[3][1], v.w);
    r.z = aq_fixed_mul(m.m[0][2], v.x) + aq_fixed_mul(m.m[1][2], v.y) +
          aq_fixed_mul(m.m[2][2], v.z) + aq_fixed_mul(m.m[3][2], v.w);
    r.w = aq_fixed_mul(m.m[0][3], v.x) + aq_fixed_mul(m.m[1][3], v.y) +
          aq_fixed_mul(m.m[2][3], v.z) + aq_fixed_mul(m.m[3][3], v.w);
    return r;
}

/* Transform a vec3 (w=1), returns projected vec3 */
static inline AqVec3 aq_mat4_transform_point(AqMat4 m, AqVec3 p) {
    AqVec4 v = AQ_VEC4(p.x, p.y, p.z, AQ_FIXED_ONE);
    AqVec4 r = aq_mat4_mul_vec4(m, v);
    return AQ_VEC3(r.x, r.y, r.z);
}

/* Transform a vec3 (w=0), for directions/normals */
static inline AqVec3 aq_mat4_transform_dir(AqMat4 m, AqVec3 d) {
    AqVec4 v = AQ_VEC4(d.x, d.y, d.z, 0);
    AqVec4 r = aq_mat4_mul_vec4(m, v);
    return AQ_VEC3(r.x, r.y, r.z);
}

/* ═══════════════════════════════════════════════════════════════
 * Matrix constructors
 * ═══════════════════════════════════════════════════════════════*/

static inline AqMat4 aq_mat4_translate(aq_fixed_t tx, aq_fixed_t ty, aq_fixed_t tz) {
    AqMat4 m = aq_mat4_identity();
    m.m[3][0] = tx;
    m.m[3][1] = ty;
    m.m[3][2] = tz;
    return m;
}

static inline AqMat4 aq_mat4_scale(aq_fixed_t sx, aq_fixed_t sy, aq_fixed_t sz) {
    AqMat4 m = {{{0}}};
    m.m[0][0] = sx;
    m.m[1][1] = sy;
    m.m[2][2] = sz;
    m.m[3][3] = AQ_FIXED_ONE;
    return m;
}

/* Rotation around X axis (angle in degrees) */
static inline AqMat4 aq_mat4_rotate_x(int32_t deg) {
    aq_fixed_t c = aq_cos_fixed(deg);
    aq_fixed_t s = aq_sin_fixed(deg);
    AqMat4 m = aq_mat4_identity();
    m.m[1][1] = c;  m.m[2][1] = -s;
    m.m[1][2] = s;  m.m[2][2] = c;
    return m;
}

/* Rotation around Y axis */
static inline AqMat4 aq_mat4_rotate_y(int32_t deg) {
    aq_fixed_t c = aq_cos_fixed(deg);
    aq_fixed_t s = aq_sin_fixed(deg);
    AqMat4 m = aq_mat4_identity();
    m.m[0][0] = c;  m.m[2][0] = s;
    m.m[0][2] = -s; m.m[2][2] = c;
    return m;
}

/* Rotation around Z axis */
static inline AqMat4 aq_mat4_rotate_z(int32_t deg) {
    aq_fixed_t c = aq_cos_fixed(deg);
    aq_fixed_t s = aq_sin_fixed(deg);
    AqMat4 m = aq_mat4_identity();
    m.m[0][0] = c;  m.m[1][0] = -s;
    m.m[0][1] = s;  m.m[1][1] = c;
    return m;
}

/* ═══════════════════════════════════════════════════════════════
 * Projection matrices
 * ═══════════════════════════════════════════════════════════════*/

/* Perspective projection.
 * fov_deg: vertical field of view in degrees
 * aspect: width/height as fixed-point (e.g., 16:9 = AQ_INT_TO_FIXED(16)/AQ_INT_TO_FIXED(9))
 * near, far: clipping planes (fixed-point)
 */
static inline AqMat4 aq_mat4_perspective(int32_t fov_deg, aq_fixed_t aspect,
                                         aq_fixed_t near_p, aq_fixed_t far_p) {
    AqMat4 m = {{{0}}};

    /* tan(fov/2) approximation using sin/cos */
    int32_t half_fov = fov_deg / 2;
    aq_fixed_t sin_val = aq_sin_fixed(half_fov);
    aq_fixed_t cos_val = aq_cos_fixed(half_fov);
    if (sin_val == 0) sin_val = 1;

    aq_fixed_t f = aq_fixed_div(cos_val, sin_val); /* cot(fov/2) */

    aq_fixed_t range = far_p - near_p;
    if (range == 0) range = 1;

    m.m[0][0] = aq_fixed_div(f, aspect);
    m.m[1][1] = f;
    m.m[2][2] = -aq_fixed_div(far_p + near_p, range);
    m.m[2][3] = -AQ_FIXED_ONE;
    m.m[3][2] = -aq_fixed_div(aq_fixed_mul(AQ_INT_TO_FIXED(2), aq_fixed_mul(far_p, near_p)), range);

    return m;
}

/* Orthographic projection */
static inline AqMat4 aq_mat4_ortho(aq_fixed_t left, aq_fixed_t right,
                                   aq_fixed_t bottom, aq_fixed_t top_p,
                                   aq_fixed_t near_p, aq_fixed_t far_p) {
    AqMat4 m = {{{0}}};

    aq_fixed_t rl = right - left;
    aq_fixed_t tb = top_p - bottom;
    aq_fixed_t fn = far_p - near_p;
    if (rl == 0) rl = 1;
    if (tb == 0) tb = 1;
    if (fn == 0) fn = 1;

    m.m[0][0] = aq_fixed_div(AQ_INT_TO_FIXED(2), rl);
    m.m[1][1] = aq_fixed_div(AQ_INT_TO_FIXED(2), tb);
    m.m[2][2] = -aq_fixed_div(AQ_INT_TO_FIXED(2), fn);
    m.m[3][0] = -aq_fixed_div(right + left, rl);
    m.m[3][1] = -aq_fixed_div(top_p + bottom, tb);
    m.m[3][2] = -aq_fixed_div(far_p + near_p, fn);
    m.m[3][3] = AQ_FIXED_ONE;

    return m;
}

/* Look-at view matrix */
static inline AqMat4 aq_mat4_look_at(AqVec3 eye, AqVec3 target, AqVec3 up) {
    AqVec3 f = aq_vec3_normalize(aq_vec3_sub(target, eye));
    AqVec3 s = aq_vec3_normalize(aq_vec3_cross(f, up));
    AqVec3 u = aq_vec3_cross(s, f);

    AqMat4 m = aq_mat4_identity();
    m.m[0][0] = s.x;  m.m[1][0] = s.y;  m.m[2][0] = s.z;
    m.m[0][1] = u.x;  m.m[1][1] = u.y;  m.m[2][1] = u.z;
    m.m[0][2] = -f.x; m.m[1][2] = -f.y; m.m[2][2] = -f.z;
    m.m[3][0] = -aq_vec3_dot(s, eye);
    m.m[3][1] = -aq_vec3_dot(u, eye);
    m.m[3][2] = aq_vec3_dot(f, eye);

    return m;
}

#ifdef __cplusplus
}
#endif

#endif /* AQ_MATH3D_H */
