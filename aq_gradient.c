/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_gradient.c - Gradient rendering implementation
 */

#include "aq_gradient.h"
#include "aq_blend.h"
#include "aq_color.h"
#include "aq_clip.h"
#include "aq_math.h"

void aq_gradient_v(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                   AqColor top, AqColor bottom) {
    if (!s || w <= 0 || h <= 0) return;

    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!aq_clip_rect(s, &cx, &cy, &cw, &ch)) return;

    for (int32_t py = cy; py < cy + ch; py++) {
        int32_t t = ((py - y) * 255) / (h > 1 ? h - 1 : 1);
        if (t > 255) t = 255;
        AqColor c = aq_color_lerp(top, bottom, (uint8_t)t);

        if (c.a == 255 && s->format == AQ_FORMAT_BGRA32) {
            uint32_t packed = aq_color_to_bgra32(c);
            uint32_t* row = (uint32_t*)(s->pixels + py * s->pitch) + cx;
            aq_memset32(row, packed, (uint32_t)cw);
        } else {
            for (int32_t px = cx; px < cx + cw; px++) {
                aq_put_pixel_blend(s, px, py, c);
            }
        }
    }
}

void aq_gradient_h(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                   AqColor left, AqColor right) {
    if (!s || w <= 0 || h <= 0) return;

    int32_t cx = x, cy = y, cw = w, ch = h;
    if (!aq_clip_rect(s, &cx, &cy, &cw, &ch)) return;

    for (int32_t px = cx; px < cx + cw; px++) {
        int32_t t = ((px - x) * 255) / (w > 1 ? w - 1 : 1);
        if (t > 255) t = 255;
        AqColor c = aq_color_lerp(left, right, (uint8_t)t);

        for (int32_t py = cy; py < cy + ch; py++) {
            aq_put_pixel_blend(s, px, py, c);
        }
    }
}

void aq_gradient_linear(AqSurface* s, AqRect rect, AqColor start, AqColor end,
                        int32_t angle_deg) {
    if (!s || rect.w <= 0 || rect.h <= 0) return;

    /* Special cases */
    if (angle_deg % 360 == 90 || angle_deg % 360 == -270) {
        aq_gradient_v(s, rect.x, rect.y, rect.w, rect.h, start, end);
        return;
    }
    if (angle_deg % 360 == 0) {
        aq_gradient_h(s, rect.x, rect.y, rect.w, rect.h, start, end);
        return;
    }

    int32_t cx = rect.x, cy = rect.y, cw = rect.w, ch = rect.h;
    if (!aq_clip_rect(s, &cx, &cy, &cw, &ch)) return;

    /* Direction vector (fixed-point) */
    aq_fixed_t dx = aq_cos_fixed(angle_deg);
    aq_fixed_t dy = aq_sin_fixed(angle_deg);

    /* Project rect corners to find the gradient range */
    int32_t half_w = rect.w / 2;
    int32_t half_h = rect.h / 2;
    aq_fixed_t proj_max = AQ_ABS(aq_fixed_mul(AQ_INT_TO_FIXED(half_w), dx)) +
                          AQ_ABS(aq_fixed_mul(AQ_INT_TO_FIXED(half_h), dy));
    if (proj_max == 0) proj_max = AQ_FIXED_ONE;
    aq_fixed_t range = 2 * proj_max;

    int32_t mid_x = rect.x + half_w;
    int32_t mid_y = rect.y + half_h;

    for (int32_t py = cy; py < cy + ch; py++) {
        for (int32_t px = cx; px < cx + cw; px++) {
            aq_fixed_t rel_x = AQ_INT_TO_FIXED(px - mid_x);
            aq_fixed_t rel_y = AQ_INT_TO_FIXED(py - mid_y);
            aq_fixed_t proj = aq_fixed_mul(rel_x, dx) + aq_fixed_mul(rel_y, dy);

            /* Normalize to 0-255 */
            int32_t t = (int32_t)(((int64_t)(proj + proj_max) * 255) / range);
            t = AQ_CLAMP(t, 0, 255);

            AqColor c = aq_color_lerp(start, end, (uint8_t)t);
            aq_put_pixel_blend(s, px, py, c);
        }
    }
}

void aq_gradient_radial(AqSurface* s, int32_t cx, int32_t cy, int32_t r,
                        AqColor center, AqColor edge) {
    if (!s || r <= 0) return;

    int32_t x0 = cx - r, y0 = cy - r;
    int32_t w = 2 * r + 1, h = 2 * r + 1;
    int32_t bx = x0, by = y0, bw = w, bh = h;
    if (!aq_clip_rect(s, &bx, &by, &bw, &bh)) return;

    int32_t r2 = r * r;

    for (int32_t py = by; py < by + bh; py++) {
        int32_t dy = py - cy;
        for (int32_t px = bx; px < bx + bw; px++) {
            int32_t dx = px - cx;
            int32_t dist2 = dx * dx + dy * dy;
            if (dist2 > r2) continue;

            int32_t dist = aq_isqrt(dist2);
            int32_t t = (dist * 255) / r;
            if (t > 255) t = 255;

            AqColor c = aq_color_lerp(center, edge, (uint8_t)t);
            aq_put_pixel_blend(s, px, py, c);
        }
    }
}

void aq_gradient_round_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                            int32_t radius, AqColor top, AqColor bottom) {
    if (!s || w <= 0 || h <= 0) return;
    if (radius <= 0) { aq_gradient_v(s, x, y, w, h, top, bottom); return; }

    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    int32_t r2 = radius * radius;

    int32_t bx = x, by = y, bw = w, bh = h;
    if (!aq_clip_rect(s, &bx, &by, &bw, &bh)) return;

    for (int32_t py = by; py < by + bh; py++) {
        int32_t t = ((py - y) * 255) / (h > 1 ? h - 1 : 1);
        if (t > 255) t = 255;
        AqColor c = aq_color_lerp(top, bottom, (uint8_t)t);

        for (int32_t px = bx; px < bx + bw; px++) {
            /* Check if inside rounded rect */
            int32_t dx = 0, dy_c = 0;
            int inside = 1;

            /* Top-left corner */
            if (px < x + radius && py < y + radius) {
                dx = x + radius - px;
                dy_c = y + radius - py;
                if (dx * dx + dy_c * dy_c > r2) inside = 0;
            }
            /* Top-right corner */
            else if (px >= x + w - radius && py < y + radius) {
                dx = px - (x + w - radius - 1);
                dy_c = y + radius - py;
                if (dx * dx + dy_c * dy_c > r2) inside = 0;
            }
            /* Bottom-left corner */
            else if (px < x + radius && py >= y + h - radius) {
                dx = x + radius - px;
                dy_c = py - (y + h - radius - 1);
                if (dx * dx + dy_c * dy_c > r2) inside = 0;
            }
            /* Bottom-right corner */
            else if (px >= x + w - radius && py >= y + h - radius) {
                dx = px - (x + w - radius - 1);
                dy_c = py - (y + h - radius - 1);
                if (dx * dx + dy_c * dy_c > r2) inside = 0;
            }

            if (inside) {
                aq_put_pixel_blend(s, px, py, c);
            }
        }
    }
}
