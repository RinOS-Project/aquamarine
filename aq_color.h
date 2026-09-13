/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_color.h - Color utilities
 */

#ifndef AQ_COLOR_H
#define AQ_COLOR_H

#include "aq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * Color manipulation (inline)
 * ═══════════════════════════════════════════════════════════════*/

/* Blend fg over bg with weight alpha (0-255) */
static inline AqColor aq_color_blend(AqColor fg, AqColor bg, uint8_t alpha) {
    uint32_t a = alpha;
    uint32_t ia = 255 - a;
    AqColor r;
    r.r = (uint8_t)((fg.r * a + bg.r * ia) / 255);
    r.g = (uint8_t)((fg.g * a + bg.g * ia) / 255);
    r.b = (uint8_t)((fg.b * a + bg.b * ia) / 255);
    r.a = (uint8_t)((fg.a * a + bg.a * ia) / 255);
    return r;
}

/* Lighten by amount (0-255) */
static inline AqColor aq_color_lighten(AqColor c, int32_t amount) {
    AqColor r;
    int32_t v;
    v = c.r + amount; r.r = (uint8_t)(v > 255 ? 255 : v);
    v = c.g + amount; r.g = (uint8_t)(v > 255 ? 255 : v);
    v = c.b + amount; r.b = (uint8_t)(v > 255 ? 255 : v);
    r.a = c.a;
    return r;
}

/* Darken by amount (0-255) */
static inline AqColor aq_color_darken(AqColor c, int32_t amount) {
    AqColor r;
    int32_t v;
    v = c.r - amount; r.r = (uint8_t)(v < 0 ? 0 : v);
    v = c.g - amount; r.g = (uint8_t)(v < 0 ? 0 : v);
    v = c.b - amount; r.b = (uint8_t)(v < 0 ? 0 : v);
    r.a = c.a;
    return r;
}

/* Linear interpolation: t=0 gives a, t=255 gives b */
static inline AqColor aq_color_lerp(AqColor a, AqColor b, uint8_t t) {
    uint32_t it = 255 - (uint32_t)t;
    uint32_t tt = (uint32_t)t;
    AqColor r;
    r.r = (uint8_t)((a.r * it + b.r * tt) / 255);
    r.g = (uint8_t)((a.g * it + b.g * tt) / 255);
    r.b = (uint8_t)((a.b * it + b.b * tt) / 255);
    r.a = (uint8_t)((a.a * it + b.a * tt) / 255);
    return r;
}

/* Multiply two colors (for lighting) */
static inline AqColor aq_color_mul(AqColor a, AqColor b) {
    AqColor r;
    r.r = (uint8_t)((uint32_t)a.r * b.r / 255);
    r.g = (uint8_t)((uint32_t)a.g * b.g / 255);
    r.b = (uint8_t)((uint32_t)a.b * b.b / 255);
    r.a = (uint8_t)((uint32_t)a.a * b.a / 255);
    return r;
}

/* Premultiply alpha */
static inline AqColor aq_color_premultiply(AqColor c) {
    uint32_t a = c.a;
    AqColor r;
    r.r = (uint8_t)(c.r * a / 255);
    r.g = (uint8_t)(c.g * a / 255);
    r.b = (uint8_t)(c.b * a / 255);
    r.a = c.a;
    return r;
}

/* ═══════════════════════════════════════════════════════════════
 * HSV/HSL conversion (implemented in aq_color.c)
 * ═══════════════════════════════════════════════════════════════*/

/* h: 0-360, s: 0-255, v: 0-255 */
AqColor aq_color_from_hsv(int32_t h, int32_t s, int32_t v);
void    aq_color_to_hsv(AqColor c, int32_t* h, int32_t* s, int32_t* v);

/* h: 0-360, s: 0-255, l: 0-255 */
AqColor aq_color_from_hsl(int32_t h, int32_t s, int32_t l);

#ifdef __cplusplus
}
#endif

#endif /* AQ_COLOR_H */
