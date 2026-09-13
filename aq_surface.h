/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_surface.h - Surface management
 */

#ifndef AQ_SURFACE_H
#define AQ_SURFACE_H

#include "aq_types.h"
#include "aq_clip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RinRenderTarget RinRenderTarget;

/* ═══════════════════════════════════════════════════════════════
 * Allocator
 * ═══════════════════════════════════════════════════════════════*/

/* Must be called before aq_surface_create */
void aq_set_allocator(AqAllocFn alloc_fn, AqFreeFn free_fn);

/* ═══════════════════════════════════════════════════════════════
 * Surface lifecycle
 * ═══════════════════════════════════════════════════════════════*/

/* Create a new surface with allocated pixel buffer */
AqSurface* aq_surface_create(int32_t width, int32_t height, AqPixelFormat format);

/* Wrap an existing pixel buffer (does NOT own memory) */
AqSurface* aq_surface_create_from(uint8_t* pixels, int32_t width, int32_t height,
                                  int32_t pitch, AqPixelFormat format);

/* Bind a runtime-owned target without taking ownership of its pixels. */
int aq_surface_init_from_rin_render_target(AqSurface* surface,
                                           const RinRenderTarget* target);

/* Create a sub-surface view into a parent (shares pixel data) */
AqSurface* aq_surface_create_sub(AqSurface* parent, AqRect region);

/* Destroy surface (frees pixel data if owned) */
void aq_surface_destroy(AqSurface* surface);

/* ═══════════════════════════════════════════════════════════════
 * Clipping
 * ═══════════════════════════════════════════════════════════════*/

void   aq_surface_set_clip(AqSurface* s, AqRect clip);
void   aq_surface_reset_clip(AqSurface* s);
AqRect aq_surface_get_clip(const AqSurface* s);

/* ═══════════════════════════════════════════════════════════════
 * Pixel access (inline for performance)
 * ═══════════════════════════════════════════════════════════════*/

/* Get pointer to start of row y */
static inline uint8_t* aq_surface_row(AqSurface* s, int32_t y) {
    return s->pixels + y * s->pitch;
}

static inline const uint8_t* aq_surface_row_const(const AqSurface* s, int32_t y) {
    return s->pixels + y * s->pitch;
}

/* Write pixel (with clip check) */
static inline void aq_put_pixel(AqSurface* s, int32_t x, int32_t y, AqColor color) {
    if (x < s->clip.x || x >= s->clip.x + s->clip.w ||
        y < s->clip.y || y >= s->clip.y + s->clip.h) return;

    uint8_t* p = s->pixels + y * s->pitch;

    switch (s->format) {
    case AQ_FORMAT_BGRA32:
        p += x * 4;
        p[0] = color.b;
        p[1] = color.g;
        p[2] = color.r;
        p[3] = color.a;
        break;
    case AQ_FORMAT_RGBA32:
        p += x * 4;
        p[0] = color.r;
        p[1] = color.g;
        p[2] = color.b;
        p[3] = color.a;
        break;
    case AQ_FORMAT_RGB24:
        p += x * 3;
        p[0] = color.r;
        p[1] = color.g;
        p[2] = color.b;
        break;
    case AQ_FORMAT_BGR24:
        p += x * 3;
        p[0] = color.b;
        p[1] = color.g;
        p[2] = color.r;
        break;
    case AQ_FORMAT_A8:
        p[x] = color.a;
        break;
    }
}

/* Read pixel (with clip check) */
static inline AqColor aq_get_pixel(const AqSurface* s, int32_t x, int32_t y) {
    AqColor c = {0, 0, 0, 0};
    if (x < 0 || x >= s->width || y < 0 || y >= s->height) return c;

    const uint8_t* p = s->pixels + y * s->pitch;

    switch (s->format) {
    case AQ_FORMAT_BGRA32:
        p += x * 4;
        c.b = p[0]; c.g = p[1]; c.r = p[2]; c.a = p[3];
        break;
    case AQ_FORMAT_RGBA32:
        p += x * 4;
        c.r = p[0]; c.g = p[1]; c.b = p[2]; c.a = p[3];
        break;
    case AQ_FORMAT_RGB24:
        p += x * 3;
        c.r = p[0]; c.g = p[1]; c.b = p[2]; c.a = 255;
        break;
    case AQ_FORMAT_BGR24:
        p += x * 3;
        c.b = p[0]; c.g = p[1]; c.r = p[2]; c.a = 255;
        break;
    case AQ_FORMAT_A8:
        c.r = c.g = c.b = p[x]; c.a = p[x];
        break;
    }
    return c;
}

/* ═══════════════════════════════════════════════════════════════
 * Surface operations
 * ═══════════════════════════════════════════════════════════════*/

/* Fill entire surface with a solid color */
void aq_surface_clear(AqSurface* s, AqColor color);

#ifdef __cplusplus
}
#endif

#endif /* AQ_SURFACE_H */
