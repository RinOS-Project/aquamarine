/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_gradient.h - Gradient rendering
 */

#ifndef AQ_GRADIENT_H
#define AQ_GRADIENT_H

#include "aq_types.h"
#include "aq_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Vertical gradient (top to bottom) */
void aq_gradient_v(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                   AqColor top, AqColor bottom);

/* Horizontal gradient (left to right) */
void aq_gradient_h(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                   AqColor left, AqColor right);

/* Linear gradient at arbitrary angle (degrees) */
void aq_gradient_linear(AqSurface* s, AqRect rect, AqColor start, AqColor end,
                        int32_t angle_deg);

/* Radial gradient */
void aq_gradient_radial(AqSurface* s, int32_t cx, int32_t cy, int32_t r,
                        AqColor center, AqColor edge);

/* Vertical gradient with rounded rect mask */
void aq_gradient_round_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, AqColor top, AqColor bottom);

#ifdef __cplusplus
}
#endif

#endif /* AQ_GRADIENT_H */
