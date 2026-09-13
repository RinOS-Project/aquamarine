/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_draw.h - Drawing primitives
 */

#ifndef AQ_DRAW_H
#define AQ_DRAW_H

#include "aq_types.h"
#include "aq_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * Rectangles
 * ═══════════════════════════════════════════════════════════════*/

void aq_fill_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h, AqColor color);
void aq_draw_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h, AqColor color);
void aq_fill_round_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t radius, AqColor color);
void aq_draw_round_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t radius, AqColor color);
void aq_draw_rect_stroke(AqSurface* s, int32_t x, int32_t y, int32_t w,
                         int32_t h, int32_t thickness, AqColor color);
void aq_draw_round_rect_stroke(AqSurface* s, int32_t x, int32_t y,
                               int32_t w, int32_t h, int32_t radius,
                               int32_t thickness, AqColor color);
void aq_invert_rect(AqSurface* s, int32_t x, int32_t y, int32_t w,
                    int32_t h);

/* ═══════════════════════════════════════════════════════════════
 * Lines
 * ═══════════════════════════════════════════════════════════════*/

void aq_hline(AqSurface* s, int32_t x, int32_t y, int32_t w, AqColor color);
void aq_vline(AqSurface* s, int32_t x, int32_t y, int32_t h, AqColor color);
void aq_line(AqSurface* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, AqColor color);
void aq_line_aa(AqSurface* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, AqColor color);

/* ═══════════════════════════════════════════════════════════════
 * Circles / Ellipses
 * ═══════════════════════════════════════════════════════════════*/

void aq_fill_circle(AqSurface* s, int32_t cx, int32_t cy, int32_t r, AqColor color);
void aq_draw_circle(AqSurface* s, int32_t cx, int32_t cy, int32_t r, AqColor color);
void aq_draw_circle_aa(AqSurface* s, int32_t cx, int32_t cy, int32_t r, AqColor color);
void aq_fill_ellipse(AqSurface* s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, AqColor color);
void aq_draw_ellipse(AqSurface* s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, AqColor color);

/* ═══════════════════════════════════════════════════════════════
 * Triangles / Polygons
 * ═══════════════════════════════════════════════════════════════*/

void aq_fill_triangle(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqColor color);
void aq_draw_triangle(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqColor color);
void aq_fill_convex_polygon(AqSurface* s, const AqPoint* pts, int32_t count, AqColor color);

/* ═══════════════════════════════════════════════════════════════
 * Bezier Curves
 * ═══════════════════════════════════════════════════════════════*/

void aq_bezier_quad(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqColor color);
void aq_bezier_cubic(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqPoint p3, AqColor color);

/* ═══════════════════════════════════════════════════════════════
 * Arcs
 * ═══════════════════════════════════════════════════════════════*/

void aq_draw_arc(AqSurface* s, int32_t cx, int32_t cy, int32_t r,
                 int32_t start_deg, int32_t end_deg, AqColor color);

/* ═══════════════════════════════════════════════════════════════
 * Effects
 * ═══════════════════════════════════════════════════════════════*/

void aq_box_shadow(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t blur, int32_t spread, AqColor color);

#ifdef __cplusplus
}
#endif

#endif /* AQ_DRAW_H */
