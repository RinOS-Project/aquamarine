/*
 * Aquamarine - RinOS 2D/3D Graphics Library
 * aq_render3d.c - Software 3D rasterizer implementation
 */

#include "aq_render3d.h"
#include "aq_blend.h"
#include "aq_draw.h"

extern AqAllocFn g_aq_alloc;
extern AqFreeFn  g_aq_free;

/* ═══════════════════════════════════════════════════════════════
 * Context lifecycle
 * ═══════════════════════════════════════════════════════════════*/

AqRenderCtx* aq_render3d_create(AqSurface* target) {
    if (!target || !g_aq_alloc) return 0;

    AqRenderCtx* ctx = (AqRenderCtx*)g_aq_alloc(sizeof(AqRenderCtx));
    if (!ctx) return 0;

    aq_memset(ctx, 0, sizeof(AqRenderCtx));
    ctx->target = target;

    /* Allocate z-buffer */
    ctx->zb_width = target->width;
    ctx->zb_height = target->height;
    uint32_t zb_size = (uint32_t)(ctx->zb_width * ctx->zb_height) * sizeof(aq_fixed_t);
    ctx->zbuffer = (aq_fixed_t*)g_aq_alloc(zb_size);
    if (!ctx->zbuffer) {
        g_aq_free(ctx);
        return 0;
    }

    /* Initialize state */
    ctx->model = aq_mat4_identity();
    ctx->view = aq_mat4_identity();
    ctx->projection = aq_mat4_identity();
    ctx->mvp = aq_mat4_identity();

    ctx->shade_mode = AQ_SHADE_FLAT;
    ctx->cull_mode = AQ_CULL_BACK;
    ctx->depth_test = 1;
    ctx->depth_write = 1;
    ctx->texture = 0;

    ctx->light_dir = aq_vec3_normalize(AQ_VEC3I(0, 0, -1));
    ctx->light_color = AQ_RGB(255, 255, 255);
    ctx->ambient = AQ_RGB(40, 40, 40);

    aq_render3d_clear_depth(ctx);

    return ctx;
}

void aq_render3d_destroy(AqRenderCtx* ctx) {
    if (!ctx || !g_aq_free) return;
    if (ctx->zbuffer) g_aq_free(ctx->zbuffer);
    g_aq_free(ctx);
}

void aq_render3d_clear_depth(AqRenderCtx* ctx) {
    if (!ctx || !ctx->zbuffer) return;
    /* Set all depth values to far (max positive) */
    int32_t count = ctx->zb_width * ctx->zb_height;
    for (int32_t i = 0; i < count; i++) {
        ctx->zbuffer[i] = 0x7FFFFFFF;
    }
}

void aq_render3d_update_mvp(AqRenderCtx* ctx) {
    if (!ctx) return;
    AqMat4 mv = aq_mat4_mul(ctx->view, ctx->model);
    ctx->mvp = aq_mat4_mul(ctx->projection, mv);
}

/* ═══════════════════════════════════════════════════════════════
 * Internal: screen-space vertex
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    int32_t    sx, sy;     /* Screen x, y */
    aq_fixed_t sz;         /* Screen z (depth) */
    aq_fixed_t w_inv;      /* 1/w for perspective correction */
    aq_fixed_t u, v;       /* Texture coords (divided by w) */
    AqColor    color;      /* Vertex color */
} ScreenVert;

static inline ScreenVert project_vertex(AqRenderCtx* ctx, const AqVertex3D* vert) {
    ScreenVert sv;

    AqVec4 clip = aq_mat4_mul_vec4(ctx->mvp, AQ_VEC4(vert->pos.x, vert->pos.y, vert->pos.z, AQ_FIXED_ONE));

    /* Perspective divide */
    if (clip.w == 0) clip.w = 1;
    aq_fixed_t inv_w = aq_fixed_div(AQ_FIXED_ONE, clip.w);

    aq_fixed_t ndc_x = aq_fixed_mul(clip.x, inv_w);
    aq_fixed_t ndc_y = aq_fixed_mul(clip.y, inv_w);
    aq_fixed_t ndc_z = aq_fixed_mul(clip.z, inv_w);

    /* NDC to screen: x = (ndc_x + 1) * width / 2, y = (1 - ndc_y) * height / 2 */
    int32_t half_w = ctx->target->width / 2;
    int32_t half_h = ctx->target->height / 2;

    sv.sx = AQ_FIXED_TO_INT(aq_fixed_mul(ndc_x + AQ_FIXED_ONE, AQ_INT_TO_FIXED(half_w)));
    sv.sy = AQ_FIXED_TO_INT(aq_fixed_mul(AQ_FIXED_ONE - ndc_y, AQ_INT_TO_FIXED(half_h)));
    sv.sz = ndc_z;
    sv.w_inv = inv_w;
    sv.u = vert->u;
    sv.v = vert->v;
    sv.color = vert->color;

    return sv;
}

/* ═══════════════════════════════════════════════════════════════
 * Internal: rasterize a screen-space triangle
 * ═══════════════════════════════════════════════════════════════*/

static void rasterize_triangle(AqRenderCtx* ctx, ScreenVert sv0, ScreenVert sv1, ScreenVert sv2) {
    AqSurface* s = ctx->target;
    if (!s) return;

    /* Bounding box */
    int32_t minx = AQ_MIN(sv0.sx, AQ_MIN(sv1.sx, sv2.sx));
    int32_t maxx = AQ_MAX(sv0.sx, AQ_MAX(sv1.sx, sv2.sx));
    int32_t miny = AQ_MIN(sv0.sy, AQ_MIN(sv1.sy, sv2.sy));
    int32_t maxy = AQ_MAX(sv0.sy, AQ_MAX(sv1.sy, sv2.sy));

    /* Clip to screen */
    if (minx < s->clip.x) minx = s->clip.x;
    if (miny < s->clip.y) miny = s->clip.y;
    if (maxx >= s->clip.x + s->clip.w) maxx = s->clip.x + s->clip.w - 1;
    if (maxy >= s->clip.y + s->clip.h) maxy = s->clip.y + s->clip.h - 1;
    if (minx > maxx || miny > maxy) return;

    /* Edge function: 2x area of triangle (fixed-point not needed, integers ok) */
    int32_t dx10 = sv1.sx - sv0.sx, dy10 = sv1.sy - sv0.sy;
    int32_t dx20 = sv2.sx - sv0.sx, dy20 = sv2.sy - sv0.sy;
    int32_t area2 = dx10 * dy20 - dx20 * dy10;
    if (area2 == 0) return; /* Degenerate triangle */

    /* For each pixel in bounding box */
    for (int32_t py = miny; py <= maxy; py++) {
        for (int32_t px = minx; px <= maxx; px++) {
            /* Barycentric coordinates via edge functions */
            int32_t dpx0 = px - sv0.sx, dpy0 = py - sv0.sy;
            int32_t dpx1 = px - sv1.sx, dpy1 = py - sv1.sy;
            int32_t dpx2 = px - sv2.sx, dpy2 = py - sv2.sy;

            int32_t w0 = (sv1.sx - sv0.sx) * dpy0 - (sv1.sy - sv0.sy) * dpx0;
            int32_t w1 = (sv2.sx - sv1.sx) * dpy1 - (sv2.sy - sv1.sy) * dpx1;
            int32_t w2 = (sv0.sx - sv2.sx) * dpy2 - (sv0.sy - sv2.sy) * dpx2;

            /* Check if inside (or on edge) */
            if (area2 > 0) {
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            } else {
                if (w0 > 0 || w1 > 0 || w2 > 0) continue;
                w0 = -w0; w1 = -w1; w2 = -w2;
                area2 = -area2;
            }

            /* Barycentric weights (scaled by area2) */
            /* w1 corresponds to v2, w2 to v0, w0 to v1 ...
               Actually: use standard form */
            int32_t b0 = w1;  /* weight for sv0 */
            int32_t b1 = w2;  /* weight for sv1 */
            int32_t b2 = w0;  /* weight for sv2 */

            /* Interpolate depth */
            int64_t z = ((int64_t)b0 * sv0.sz + (int64_t)b1 * sv1.sz + (int64_t)b2 * sv2.sz) / area2;

            /* Depth test */
            int32_t zb_idx = py * ctx->zb_width + px;
            if (ctx->depth_test && z >= ctx->zbuffer[zb_idx]) continue;
            if (ctx->depth_write) ctx->zbuffer[zb_idx] = (aq_fixed_t)z;

            /* Determine pixel color */
            AqColor pixel_color;

            if (ctx->shade_mode == AQ_SHADE_GOURAUD) {
                /* Interpolate vertex colors */
                pixel_color.r = (uint8_t)(((int64_t)b0 * sv0.color.r + (int64_t)b1 * sv1.color.r + (int64_t)b2 * sv2.color.r) / area2);
                pixel_color.g = (uint8_t)(((int64_t)b0 * sv0.color.g + (int64_t)b1 * sv1.color.g + (int64_t)b2 * sv2.color.g) / area2);
                pixel_color.b = (uint8_t)(((int64_t)b0 * sv0.color.b + (int64_t)b1 * sv1.color.b + (int64_t)b2 * sv2.color.b) / area2);
                pixel_color.a = 255;
            } else if (ctx->shade_mode == AQ_SHADE_TEXTURE && ctx->texture) {
                /* Interpolate UVs */
                aq_fixed_t u = (aq_fixed_t)(((int64_t)b0 * sv0.u + (int64_t)b1 * sv1.u + (int64_t)b2 * sv2.u) / area2);
                aq_fixed_t v = (aq_fixed_t)(((int64_t)b0 * sv0.v + (int64_t)b1 * sv1.v + (int64_t)b2 * sv2.v) / area2);

                /* Sample texture (nearest-neighbor) */
                int32_t tx = AQ_FIXED_TO_INT(aq_fixed_mul(u, AQ_INT_TO_FIXED(ctx->texture->width - 1)));
                int32_t ty = AQ_FIXED_TO_INT(aq_fixed_mul(v, AQ_INT_TO_FIXED(ctx->texture->height - 1)));
                tx = AQ_CLAMP(tx, 0, ctx->texture->width - 1);
                ty = AQ_CLAMP(ty, 0, ctx->texture->height - 1);
                pixel_color = aq_get_pixel(ctx->texture, tx, ty);
            } else {
                /* Flat: use first vertex color */
                pixel_color = sv0.color;
            }

            aq_put_pixel(s, px, py, pixel_color);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════*/

void aq_render3d_triangle(AqRenderCtx* ctx,
                          const AqVertex3D* v0,
                          const AqVertex3D* v1,
                          const AqVertex3D* v2) {
    if (!ctx || !ctx->target || !v0 || !v1 || !v2) return;

    /* Back-face culling (in world space) */
    if (ctx->cull_mode != AQ_CULL_NONE) {
        AqVec3 e1 = aq_vec3_sub(v1->pos, v0->pos);
        AqVec3 e2 = aq_vec3_sub(v2->pos, v0->pos);
        AqVec3 face_normal = aq_vec3_cross(e1, e2);

        /* Camera direction (simplified: assume looking into -Z) */
        AqVec3 view_dir = AQ_VEC3(0, 0, -AQ_FIXED_ONE);
        aq_fixed_t d = aq_vec3_dot(face_normal, view_dir);

        if (ctx->cull_mode == AQ_CULL_BACK && d <= 0) return;
        if (ctx->cull_mode == AQ_CULL_FRONT && d >= 0) return;
    }

    /* Flat shading: compute lighting from face normal */
    if (ctx->shade_mode == AQ_SHADE_FLAT) {
        AqVec3 e1 = aq_vec3_sub(v1->pos, v0->pos);
        AqVec3 e2 = aq_vec3_sub(v2->pos, v0->pos);
        AqVec3 normal = aq_vec3_normalize(aq_vec3_cross(e1, e2));

        aq_fixed_t ndotl = aq_vec3_dot(normal, ctx->light_dir);
        if (ndotl < 0) ndotl = -ndotl;

        /* Compute lit color */
        AqColor base = v0->color;
        uint32_t intensity = AQ_FIXED_TO_INT(ndotl * 255);
        if (intensity > 255) intensity = 255;

        AqColor lit;
        lit.r = (uint8_t)AQ_CLAMP((int32_t)ctx->ambient.r + (int32_t)base.r * intensity / 255, 0, 255);
        lit.g = (uint8_t)AQ_CLAMP((int32_t)ctx->ambient.g + (int32_t)base.g * intensity / 255, 0, 255);
        lit.b = (uint8_t)AQ_CLAMP((int32_t)ctx->ambient.b + (int32_t)base.b * intensity / 255, 0, 255);
        lit.a = 255;

        /* Project and use lit color for all vertices */
        ScreenVert sv0 = project_vertex(ctx, v0); sv0.color = lit;
        ScreenVert sv1 = project_vertex(ctx, v1); sv1.color = lit;
        ScreenVert sv2 = project_vertex(ctx, v2); sv2.color = lit;
        rasterize_triangle(ctx, sv0, sv1, sv2);
    } else {
        /* Gouraud / Texture: project as-is */
        ScreenVert sv0 = project_vertex(ctx, v0);
        ScreenVert sv1 = project_vertex(ctx, v1);
        ScreenVert sv2 = project_vertex(ctx, v2);
        rasterize_triangle(ctx, sv0, sv1, sv2);
    }
}

void aq_render3d_mesh(AqRenderCtx* ctx,
                      const AqVertex3D* vertices, int32_t vertex_count) {
    if (!ctx || !vertices || vertex_count < 3) return;

    for (int32_t i = 0; i + 2 < vertex_count; i += 3) {
        aq_render3d_triangle(ctx, &vertices[i], &vertices[i + 1], &vertices[i + 2]);
    }
}

void aq_render3d_mesh_indexed(AqRenderCtx* ctx,
                              const AqVertex3D* vertices,
                              const uint16_t* indices, int32_t index_count) {
    if (!ctx || !vertices || !indices || index_count < 3) return;

    for (int32_t i = 0; i + 2 < index_count; i += 3) {
        aq_render3d_triangle(ctx,
            &vertices[indices[i]],
            &vertices[indices[i + 1]],
            &vertices[indices[i + 2]]);
    }
}

void aq_render3d_wire_triangle(AqRenderCtx* ctx,
                               const AqVertex3D* v0,
                               const AqVertex3D* v1,
                               const AqVertex3D* v2,
                               AqColor color) {
    if (!ctx || !ctx->target) return;

    ScreenVert sv0 = project_vertex(ctx, v0);
    ScreenVert sv1 = project_vertex(ctx, v1);
    ScreenVert sv2 = project_vertex(ctx, v2);

    aq_line(ctx->target, sv0.sx, sv0.sy, sv1.sx, sv1.sy, color);
    aq_line(ctx->target, sv1.sx, sv1.sy, sv2.sx, sv2.sy, color);
    aq_line(ctx->target, sv2.sx, sv2.sy, sv0.sx, sv0.sy, color);
}

void aq_render3d_wire_mesh(AqRenderCtx* ctx,
                           const AqVertex3D* vertices, int32_t vertex_count,
                           AqColor color) {
    if (!ctx || !vertices || vertex_count < 3) return;

    for (int32_t i = 0; i + 2 < vertex_count; i += 3) {
        aq_render3d_wire_triangle(ctx,
            &vertices[i], &vertices[i + 1], &vertices[i + 2], color);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════════*/

AqPoint aq_render3d_project(AqRenderCtx* ctx, AqVec3 world_pos) {
    AqVertex3D v = { 0 };
    v.pos = world_pos;
    ScreenVert sv = project_vertex(ctx, &v);
    return AQ_PT(sv.sx, sv.sy);
}

void aq_render3d_gen_cube(AqVertex3D* out, aq_fixed_t size, AqColor color) {
    if (!out) return;

    aq_fixed_t s = size / 2;

    /* 6 faces * 2 triangles * 3 vertices = 36 vertices */
    AqVec3 positions[8] = {
        AQ_VEC3(-s, -s, -s), AQ_VEC3( s, -s, -s),
        AQ_VEC3( s,  s, -s), AQ_VEC3(-s,  s, -s),
        AQ_VEC3(-s, -s,  s), AQ_VEC3( s, -s,  s),
        AQ_VEC3( s,  s,  s), AQ_VEC3(-s,  s,  s),
    };

    /* Face indices (2 triangles per face) */
    static const uint8_t faces[36] = {
        /* Front  (+Z) */ 4,5,6, 4,6,7,
        /* Back   (-Z) */ 1,0,3, 1,3,2,
        /* Right  (+X) */ 5,1,2, 5,2,6,
        /* Left   (-X) */ 0,4,7, 0,7,3,
        /* Top    (+Y) */ 7,6,2, 7,2,3,
        /* Bottom (-Y) */ 0,1,5, 0,5,4,
    };

    AqVec3 normals[6] = {
        AQ_VEC3(0, 0, AQ_FIXED_ONE),   /* Front */
        AQ_VEC3(0, 0, -AQ_FIXED_ONE),  /* Back */
        AQ_VEC3(AQ_FIXED_ONE, 0, 0),   /* Right */
        AQ_VEC3(-AQ_FIXED_ONE, 0, 0),  /* Left */
        AQ_VEC3(0, AQ_FIXED_ONE, 0),   /* Top */
        AQ_VEC3(0, -AQ_FIXED_ONE, 0),  /* Bottom */
    };

    for (int32_t i = 0; i < 36; i++) {
        out[i].pos = positions[faces[i]];
        out[i].normal = normals[i / 6];
        out[i].color = color;
        out[i].u = 0;
        out[i].v = 0;
    }

    /* Set UV coordinates for each face */
    for (int32_t face = 0; face < 6; face++) {
        int32_t base = face * 6;
        /* Triangle 1 */
        out[base + 0].u = 0;                out[base + 0].v = 0;
        out[base + 1].u = AQ_FIXED_ONE;     out[base + 1].v = 0;
        out[base + 2].u = AQ_FIXED_ONE;     out[base + 2].v = AQ_FIXED_ONE;
        /* Triangle 2 */
        out[base + 3].u = 0;                out[base + 3].v = 0;
        out[base + 4].u = AQ_FIXED_ONE;     out[base + 4].v = AQ_FIXED_ONE;
        out[base + 5].u = 0;                out[base + 5].v = AQ_FIXED_ONE;
    }
}
