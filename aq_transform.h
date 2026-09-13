/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_transform.h - Surface transforms (scale, rotate, flip)
 */

#ifndef AQ_TRANSFORM_H
#define AQ_TRANSFORM_H

#include "aq_types.h"
#include "aq_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flip surface in place */
void aq_surface_flip_h(AqSurface* s);
void aq_surface_flip_v(AqSurface* s);

/* Create a scaled copy (nearest-neighbor) */
AqSurface* aq_surface_scale_nn(const AqSurface* src, int32_t new_w, int32_t new_h);

/* Create a scaled copy (bilinear interpolation) */
AqSurface* aq_surface_scale_bilinear(const AqSurface* src, int32_t new_w, int32_t new_h);

/* Create a rotated copy (degrees, nearest-neighbor) */
AqSurface* aq_surface_rotate(const AqSurface* src, int32_t angle_deg);

/* Scale blit: blit src_rect from src onto dst_rect in dst (nearest-neighbor) */
void aq_blit_scaled(AqSurface* dst, AqRect dst_rect,
                    const AqSurface* src, AqRect src_rect);

/* Scale blit with bilinear interpolation */
void aq_blit_scaled_bilinear(AqSurface* dst, AqRect dst_rect,
                             const AqSurface* src, AqRect src_rect);

/* Scale blit with a constant source alpha. When `copy` is non-zero, the
 * sampled pixel replaces the destination (Porter-Duff copy); otherwise it is
 * composited source-over. `bilinear` selects linear interpolation instead of
 * nearest-neighbor sampling. */
void aq_blit_scaled_alpha(AqSurface* dst, AqRect dst_rect,
                          const AqSurface* src, AqRect src_rect,
                          uint8_t alpha, int bilinear, int copy);

#ifdef __cplusplus
}
#endif

#endif /* AQ_TRANSFORM_H */
