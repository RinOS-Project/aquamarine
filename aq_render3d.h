/*
 * Aquamarine - RinOS 2D/3D Graphics Library
 * aq_render3d.h - Software 3D rasterizer
 *
 * Features:
 *   - Z-buffer depth testing
 *   - Perspective-correct vertex attribute interpolation
 *   - Flat shading, Gouraud shading
 *   - Texture mapping (nearest-neighbor)
 *   - Wireframe rendering
 *   - Back-face culling
 */

#ifndef AQ_RENDER3D_H
#define AQ_RENDER3D_H

#include "aq_types.h"
#include "aq_surface.h"
#include "aq_math3d.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * 3D Vertex
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    AqVec3    pos;       /* Object-space position (fixed-point) */
    AqVec3    normal;    /* Vertex normal (fixed-point, normalized) */
    aq_fixed_t u, v;     /* Texture coordinates (fixed-point, 0..1) */
    AqColor   color;     /* Vertex color (for Gouraud shading) */
} AqVertex3D;

/* ═══════════════════════════════════════════════════════════════
 * Render context
 * ═══════════════════════════════════════════════════════════════*/

typedef enum {
    AQ_SHADE_FLAT    = 0,  /* Single color per triangle */
    AQ_SHADE_GOURAUD = 1,  /* Interpolate vertex colors */
    AQ_SHADE_TEXTURE = 2,  /* Texture mapping */
} AqShadeMode;

typedef enum {
    AQ_CULL_NONE  = 0,
    AQ_CULL_BACK  = 1,
    AQ_CULL_FRONT = 2,
} AqCullMode;

typedef struct {
    AqSurface*   target;       /* Render target surface */
    aq_fixed_t*  zbuffer;      /* Depth buffer (fixed-point, one per pixel) */
    int32_t      zb_width;     /* Z-buffer width */
    int32_t      zb_height;    /* Z-buffer height */

    AqMat4       model;        /* Model transform */
    AqMat4       view;         /* View/camera transform */
    AqMat4       projection;   /* Projection matrix */
    AqMat4       mvp;          /* Cached model*view*projection */

    AqShadeMode  shade_mode;
    AqCullMode   cull_mode;
    int          depth_test;   /* 1 = enable depth testing */
    int          depth_write;  /* 1 = write to depth buffer */

    const AqSurface* texture;  /* Current texture (for AQ_SHADE_TEXTURE) */

    /* Directional light for flat shading */
    AqVec3       light_dir;    /* Light direction (normalized, world-space) */
    AqColor      light_color;  /* Light color */
    AqColor      ambient;      /* Ambient light color */
} AqRenderCtx;

/* ═══════════════════════════════════════════════════════════════
 * Render context lifecycle
 * ═══════════════════════════════════════════════════════════════*/

/* Create render context for the given target surface */
AqRenderCtx* aq_render3d_create(AqSurface* target);

/* Destroy render context (frees z-buffer) */
void aq_render3d_destroy(AqRenderCtx* ctx);

/* Clear the depth buffer */
void aq_render3d_clear_depth(AqRenderCtx* ctx);

/* Update the cached MVP matrix (call after changing model/view/projection) */
void aq_render3d_update_mvp(AqRenderCtx* ctx);

/* ═══════════════════════════════════════════════════════════════
 * 3D Drawing
 * ═══════════════════════════════════════════════════════════════*/

/* Draw a single 3D triangle */
void aq_render3d_triangle(AqRenderCtx* ctx,
                          const AqVertex3D* v0,
                          const AqVertex3D* v1,
                          const AqVertex3D* v2);

/* Draw a triangle mesh (array of vertices, 3 per triangle) */
void aq_render3d_mesh(AqRenderCtx* ctx,
                      const AqVertex3D* vertices, int32_t vertex_count);

/* Draw indexed triangle mesh */
void aq_render3d_mesh_indexed(AqRenderCtx* ctx,
                              const AqVertex3D* vertices,
                              const uint16_t* indices, int32_t index_count);

/* Draw wireframe triangle */
void aq_render3d_wire_triangle(AqRenderCtx* ctx,
                               const AqVertex3D* v0,
                               const AqVertex3D* v1,
                               const AqVertex3D* v2,
                               AqColor color);

/* Draw wireframe mesh */
void aq_render3d_wire_mesh(AqRenderCtx* ctx,
                           const AqVertex3D* vertices, int32_t vertex_count,
                           AqColor color);

/* ═══════════════════════════════════════════════════════════════
 * Utility
 * ═══════════════════════════════════════════════════════════════*/

/* Project a 3D point to 2D screen coordinates */
AqPoint aq_render3d_project(AqRenderCtx* ctx, AqVec3 world_pos);

/* Create a cube mesh (fills 36 vertices for 12 triangles) */
void aq_render3d_gen_cube(AqVertex3D* out_verts, aq_fixed_t size, AqColor color);

#ifdef __cplusplus
}
#endif

#endif /* AQ_RENDER3D_H */
