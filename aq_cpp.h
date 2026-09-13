/* SPDX-License-Identifier: MIT */
/* C++ convenience overloads for packed RinOS ARGB colors. */
#ifndef AQUAMARINE_CPP_H
#define AQUAMARINE_CPP_H

extern "C" {
#include "aquamarine.h"
}

static inline AqColor aq_app_color(uint32_t color) {
    return aq_color_from_argb32(color);
}

static inline void aq_surface_clear(AqSurface* surface, uint32_t color) {
    aq_surface_clear(surface, aq_app_color(color));
}

static inline void aq_fill_rect(AqSurface* surface, int32_t x, int32_t y,
                                int32_t w, int32_t h, uint32_t color) {
    aq_fill_rect(surface, x, y, w, h, aq_app_color(color));
}

static inline void aq_draw_rect(AqSurface* surface, int32_t x, int32_t y,
                                int32_t w, int32_t h, uint32_t color) {
    aq_draw_rect(surface, x, y, w, h, aq_app_color(color));
}

static inline void aq_draw_rect_stroke(AqSurface* surface, int32_t x,
                                       int32_t y, int32_t w, int32_t h,
                                       int32_t thickness, uint32_t color) {
    aq_draw_rect_stroke(surface, x, y, w, h, thickness, aq_app_color(color));
}

static inline void aq_fill_round_rect(AqSurface* surface, int32_t x,
                                      int32_t y, int32_t w, int32_t h,
                                      int32_t radius, uint32_t color) {
    aq_fill_round_rect(surface, x, y, w, h, radius, aq_app_color(color));
}

static inline void aq_draw_round_rect(AqSurface* surface, int32_t x,
                                      int32_t y, int32_t w, int32_t h,
                                      int32_t radius, uint32_t color) {
    aq_draw_round_rect(surface, x, y, w, h, radius, aq_app_color(color));
}

static inline void aq_draw_round_rect_stroke(AqSurface* surface, int32_t x,
                                             int32_t y, int32_t w, int32_t h,
                                             int32_t radius, int32_t thickness,
                                             uint32_t color) {
    aq_draw_round_rect_stroke(surface, x, y, w, h, radius, thickness,
                              aq_app_color(color));
}

static inline void aq_hline(AqSurface* surface, int32_t x, int32_t y,
                            int32_t width, uint32_t color) {
    aq_hline(surface, x, y, width, aq_app_color(color));
}

static inline void aq_vline(AqSurface* surface, int32_t x, int32_t y,
                            int32_t height, uint32_t color) {
    aq_vline(surface, x, y, height, aq_app_color(color));
}

static inline void aq_line(AqSurface* surface, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, uint32_t color) {
    aq_line(surface, x0, y0, x1, y1, aq_app_color(color));
}

static inline void aq_fill_circle(AqSurface* surface, int32_t cx, int32_t cy,
                                  int32_t radius, uint32_t color) {
    aq_fill_circle(surface, cx, cy, radius, aq_app_color(color));
}

static inline void aq_draw_circle(AqSurface* surface, int32_t cx, int32_t cy,
                                  int32_t radius, uint32_t color) {
    aq_draw_circle(surface, cx, cy, radius, aq_app_color(color));
}

static inline void aq_box_shadow(AqSurface* surface, int32_t x, int32_t y,
                                 int32_t w, int32_t h, int32_t blur,
                                 int32_t spread, uint32_t color) {
    aq_box_shadow(surface, x, y, w, h, blur, spread, aq_app_color(color));
}

static inline void aq_box_shadow(AqSurface* surface, int32_t x, int32_t y,
                                 int32_t w, int32_t h, int32_t blur,
                                 uint32_t color) {
    aq_box_shadow(surface, x, y, w, h, blur, 0, aq_app_color(color));
}

/* Compatibility-free call shape used by migrated code whose color preceded
 * the optional spread value.  It is still a typed Aquamarine overload and
 * never routes through the removed runtime drawing API. */
static inline void aq_box_shadow(AqSurface* surface, int32_t x, int32_t y,
                                 int32_t w, int32_t h, int32_t blur,
                                 uint32_t color, int32_t spread) {
    aq_box_shadow(surface, x, y, w, h, blur, spread, aq_app_color(color));
}

static inline void aq_gradient_v(AqSurface* surface, int32_t x, int32_t y,
                                 int32_t w, int32_t h, uint32_t top,
                                 uint32_t bottom) {
    aq_gradient_v(surface, x, y, w, h, aq_app_color(top), aq_app_color(bottom));
}

static inline void aq_draw_string(AqSurface* surface, int32_t x, int32_t y,
                                  const char* text, uint32_t color) {
    aq_draw_string(surface, x, y, text, aq_app_color(color),
                   aq_font_builtin_8x16());
}

static inline void aq_draw_string_scaled(AqSurface* surface, int32_t x,
                                         int32_t y, const char* text,
                                         uint32_t color, uint32_t numerator,
                                         uint32_t denominator) {
    aq_draw_string_scaled(surface, x, y, text, aq_app_color(color),
                          aq_font_builtin_8x16(), numerator, denominator);
}

static inline int32_t aq_text_width(const char* text) {
    return aq_text_width(text, aq_font_builtin_8x16());
}

static inline int32_t aq_text_height(void) {
    return aq_font_builtin_8x16()->glyph_h;
}

static inline uint32_t aq_color_lighten(uint32_t color, float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    return aq_color_to_argb32(aq_color_lighten(
        aq_color_from_argb32(color), (int32_t)(amount * 255.0f)));
}

static inline uint32_t aq_color_darken(uint32_t color, float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    return aq_color_to_argb32(aq_color_darken(
        aq_color_from_argb32(color), (int32_t)(amount * 255.0f)));
}

static inline uint32_t aq_color_lerp(uint32_t first, uint32_t second,
                                     float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    return aq_color_to_argb32(aq_color_lerp(
        aq_color_from_argb32(first), aq_color_from_argb32(second),
        (uint8_t)(amount * 255.0f)));
}

static inline void aq_put_pixel(AqSurface* surface, int32_t x, int32_t y,
                                uint32_t color) {
    aq_put_pixel(surface, x, y, aq_app_color(color));
}

#endif
