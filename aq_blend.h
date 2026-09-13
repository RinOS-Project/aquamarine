/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_blend.h - Alpha blending and blit operations
 */

#ifndef AQ_BLEND_H
#define AQ_BLEND_H

#include "aq_types.h"
#include "aq_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * Alpha blending (inline)
 * ═══════════════════════════════════════════════════════════════*/

/* Porter-Duff "over" operator: src over dst */
static inline AqColor aq_blend_over(AqColor src, AqColor dst) {
    if (src.a == 255) return src;
    if (src.a == 0) return dst;

    uint32_t sa = src.a;
    uint32_t da = 255 - sa;
    AqColor out;
    out.r = (uint8_t)((src.r * sa + dst.r * da) / 255);
    out.g = (uint8_t)((src.g * sa + dst.g * da) / 255);
    out.b = (uint8_t)((src.b * sa + dst.b * da) / 255);
    out.a = (uint8_t)(sa + (dst.a * da) / 255);
    return out;
}

/* Blend with extra alpha multiplier */
static inline AqColor aq_blend_over_alpha(AqColor src, AqColor dst, uint8_t alpha) {
    uint32_t sa = ((uint32_t)src.a * alpha) / 255;
    if (sa == 0) return dst;
    if (sa == 255) { src.a = 255; return src; }

    uint32_t da = 255 - sa;
    AqColor out;
    out.r = (uint8_t)((src.r * sa + dst.r * da) / 255);
    out.g = (uint8_t)((src.g * sa + dst.g * da) / 255);
    out.b = (uint8_t)((src.b * sa + dst.b * da) / 255);
    out.a = (uint8_t)(sa + (dst.a * da) / 255);
    return out;
}

/* Write pixel with alpha blending onto surface */
static inline void aq_put_pixel_blend(AqSurface* s, int32_t x, int32_t y, AqColor color) {
    if (color.a == 0) return;
    if (color.a == 255) {
        aq_put_pixel(s, x, y, color);
        return;
    }
    if (x < s->clip.x || x >= s->clip.x + s->clip.w ||
        y < s->clip.y || y >= s->clip.y + s->clip.h) return;

    AqColor dst = aq_get_pixel(s, x, y);
    AqColor blended = aq_blend_over(color, dst);
    aq_put_pixel(s, x, y, blended);
}

/* ═══════════════════════════════════════════════════════════════
 * Blit operations
 * ═══════════════════════════════════════════════════════════════*/

/* Blit src onto dst at (dx, dy) with alpha blending */
void aq_blit(AqSurface* dst, int32_t dx, int32_t dy, const AqSurface* src);
void aq_blit_pixels(AqSurface* dst, int32_t dx, int32_t dy, int32_t width,
                    int32_t height, const uint32_t* pixels);

/* Blit a sub-region of src */
void aq_blit_region(AqSurface* dst, int32_t dx, int32_t dy,
                    const AqSurface* src, AqRect src_rect);

/* Blit with per-surface alpha multiplier */
void aq_blit_alpha(AqSurface* dst, int32_t dx, int32_t dy,
                   const AqSurface* src, uint8_t alpha);

/* Copy without blending (overwrite) */
void aq_blit_copy(AqSurface* dst, int32_t dx, int32_t dy,
                  const AqSurface* src, AqRect src_rect);

#ifdef __cplusplus
}
#endif

#endif /* AQ_BLEND_H */
