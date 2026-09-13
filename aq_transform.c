/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_transform.c - Surface transforms implementation
 */

#include "aq_transform.h"
#include "aq_blend.h"
#include "aq_color.h"
#include "aq_math.h"

/* ═══════════════════════════════════════════════════════════════
 * Flip
 * ═══════════════════════════════════════════════════════════════*/

void aq_surface_flip_h(AqSurface* s) {
    if (!s || !s->pixels) return;
    int32_t bpp = s->bpp / 8;

    for (int32_t y = 0; y < s->height; y++) {
        uint8_t* row = s->pixels + y * s->pitch;
        for (int32_t x = 0; x < s->width / 2; x++) {
            int32_t x2 = s->width - 1 - x;
            uint8_t* a = row + x * bpp;
            uint8_t* b = row + x2 * bpp;
            for (int32_t i = 0; i < bpp; i++) {
                uint8_t tmp = a[i];
                a[i] = b[i];
                b[i] = tmp;
            }
        }
    }
}

void aq_surface_flip_v(AqSurface* s) {
    if (!s || !s->pixels) return;
    int32_t row_bytes = s->width * (s->bpp / 8);

    for (int32_t y = 0; y < s->height / 2; y++) {
        uint8_t* a = s->pixels + y * s->pitch;
        uint8_t* b = s->pixels + (s->height - 1 - y) * s->pitch;
        /* Swap rows byte-by-byte */
        for (int32_t i = 0; i < row_bytes; i++) {
            uint8_t tmp = a[i];
            a[i] = b[i];
            b[i] = tmp;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Scale
 * ═══════════════════════════════════════════════════════════════*/

AqSurface* aq_surface_scale_nn(const AqSurface* src, int32_t new_w, int32_t new_h) {
    if (!src || new_w <= 0 || new_h <= 0) return 0;

    AqSurface* dst = aq_surface_create(new_w, new_h, src->format);
    if (!dst) return 0;

    /* Fixed-point step values */
    aq_fixed_t step_x = aq_fixed_div(AQ_INT_TO_FIXED(src->width), AQ_INT_TO_FIXED(new_w));
    aq_fixed_t step_y = aq_fixed_div(AQ_INT_TO_FIXED(src->height), AQ_INT_TO_FIXED(new_h));

    aq_fixed_t src_y = 0;
    for (int32_t dy = 0; dy < new_h; dy++) {
        int32_t sy = AQ_FIXED_TO_INT(src_y);
        if (sy >= src->height) sy = src->height - 1;

        aq_fixed_t src_x = 0;
        for (int32_t dx = 0; dx < new_w; dx++) {
            int32_t sx = AQ_FIXED_TO_INT(src_x);
            if (sx >= src->width) sx = src->width - 1;

            AqColor c = aq_get_pixel(src, sx, sy);
            aq_put_pixel(dst, dx, dy, c);
            src_x += step_x;
        }
        src_y += step_y;
    }

    return dst;
}

AqSurface* aq_surface_scale_bilinear(const AqSurface* src, int32_t new_w, int32_t new_h) {
    if (!src || new_w <= 0 || new_h <= 0) return 0;

    AqSurface* dst = aq_surface_create(new_w, new_h, src->format);
    if (!dst) return 0;

    aq_fixed_t step_x = aq_fixed_div(AQ_INT_TO_FIXED(src->width - 1), AQ_INT_TO_FIXED(new_w > 1 ? new_w - 1 : 1));
    aq_fixed_t step_y = aq_fixed_div(AQ_INT_TO_FIXED(src->height - 1), AQ_INT_TO_FIXED(new_h > 1 ? new_h - 1 : 1));

    aq_fixed_t src_y = 0;
    for (int32_t dy = 0; dy < new_h; dy++) {
        int32_t sy = AQ_FIXED_TO_INT(src_y);
        uint32_t fy = (AQ_FIXED_FRAC(src_y) >> 8) & 0xFF; /* 0-255 */
        int32_t sy1 = sy + 1;
        if (sy1 >= src->height) sy1 = src->height - 1;

        aq_fixed_t src_x = 0;
        for (int32_t dx = 0; dx < new_w; dx++) {
            int32_t sx = AQ_FIXED_TO_INT(src_x);
            uint32_t fx = (AQ_FIXED_FRAC(src_x) >> 8) & 0xFF;
            int32_t sx1 = sx + 1;
            if (sx1 >= src->width) sx1 = src->width - 1;

            AqColor c00 = aq_get_pixel(src, sx, sy);
            AqColor c10 = aq_get_pixel(src, sx1, sy);
            AqColor c01 = aq_get_pixel(src, sx, sy1);
            AqColor c11 = aq_get_pixel(src, sx1, sy1);

            /* Bilinear interpolation */
            AqColor top = aq_color_lerp(c00, c10, (uint8_t)fx);
            AqColor bot = aq_color_lerp(c01, c11, (uint8_t)fx);
            AqColor final_c = aq_color_lerp(top, bot, (uint8_t)fy);

            aq_put_pixel(dst, dx, dy, final_c);
            src_x += step_x;
        }
        src_y += step_y;
    }

    return dst;
}

/* ═══════════════════════════════════════════════════════════════
 * Rotate
 * ═══════════════════════════════════════════════════════════════*/

AqSurface* aq_surface_rotate(const AqSurface* src, int32_t angle_deg) {
    if (!src) return 0;

    angle_deg = angle_deg % 360;
    if (angle_deg < 0) angle_deg += 360;

    /* Special cases for 90-degree multiples */
    if (angle_deg == 0) {
        return aq_surface_scale_nn(src, src->width, src->height);
    }

    /* Calculate bounding box of rotated surface */
    aq_fixed_t cos_a = aq_cos_fixed(angle_deg);
    aq_fixed_t sin_a = aq_sin_fixed(angle_deg);

    int32_t half_w = src->width / 2;
    int32_t half_h = src->height / 2;

    /* Corners relative to center */
    int32_t cx[4] = {-half_w, half_w, half_w, -half_w};
    int32_t cy[4] = {-half_h, -half_h, half_h, half_h};

    int32_t min_x = 0x7FFFFFFF, max_x = -0x7FFFFFFF;
    int32_t min_y = 0x7FFFFFFF, max_y = -0x7FFFFFFF;

    for (int i = 0; i < 4; i++) {
        int32_t rx = AQ_FIXED_TO_INT(aq_fixed_mul(AQ_INT_TO_FIXED(cx[i]), cos_a) -
                                     aq_fixed_mul(AQ_INT_TO_FIXED(cy[i]), sin_a));
        int32_t ry = AQ_FIXED_TO_INT(aq_fixed_mul(AQ_INT_TO_FIXED(cx[i]), sin_a) +
                                     aq_fixed_mul(AQ_INT_TO_FIXED(cy[i]), cos_a));
        if (rx < min_x) min_x = rx;
        if (rx > max_x) max_x = rx;
        if (ry < min_y) min_y = ry;
        if (ry > max_y) max_y = ry;
    }

    int32_t new_w = max_x - min_x + 1;
    int32_t new_h = max_y - min_y + 1;

    AqSurface* dst = aq_surface_create(new_w, new_h, src->format);
    if (!dst) return 0;

    /* Inverse rotation: for each dst pixel, find the src pixel */
    aq_fixed_t icos = aq_cos_fixed(-angle_deg);
    aq_fixed_t isin = aq_sin_fixed(-angle_deg);

    int32_t dst_cx = new_w / 2;
    int32_t dst_cy = new_h / 2;
    int32_t src_cx = src->width / 2;
    int32_t src_cy = src->height / 2;

    for (int32_t dy = 0; dy < new_h; dy++) {
        for (int32_t dx = 0; dx < new_w; dx++) {
            aq_fixed_t rel_x = AQ_INT_TO_FIXED(dx - dst_cx);
            aq_fixed_t rel_y = AQ_INT_TO_FIXED(dy - dst_cy);

            int32_t sx = AQ_FIXED_TO_INT(aq_fixed_mul(rel_x, icos) -
                                         aq_fixed_mul(rel_y, isin)) + src_cx;
            int32_t sy = AQ_FIXED_TO_INT(aq_fixed_mul(rel_x, isin) +
                                         aq_fixed_mul(rel_y, icos)) + src_cy;

            if (sx >= 0 && sx < src->width && sy >= 0 && sy < src->height) {
                aq_put_pixel(dst, dx, dy, aq_get_pixel(src, sx, sy));
            }
        }
    }

    return dst;
}

/* ═══════════════════════════════════════════════════════════════
 * Scale blit
 * ═══════════════════════════════════════════════════════════════*/

#define AQ_SCALE_BLIT_PIXEL_BUDGET UINT64_C(1048576)

static int aq_valid_rect(const AqSurface* surface, AqRect rect) {
    if (!surface || rect.w <= 0 || rect.h <= 0) return 0;
    if (rect.x < 0 || rect.y < 0) return 0;
    if (rect.x > surface->width || rect.y > surface->height) return 0;
    if (rect.w > surface->width - rect.x || rect.h > surface->height - rect.y) return 0;
    return 1;
}

static int aq_fixed_dimension_valid(int32_t value) {
    return value > 0 && value <= INT32_MAX / AQ_FIXED_ONE;
}

/* AQ_INT_TO_FIXED is a signed 32-bit 16.16 conversion. Keep every source
 * coordinate and extent representable before entering the fixed-point loop;
 * otherwise a large surface could wrap into an invalid pixel coordinate. */
static int aq_fixed_source_rect_valid(AqRect rect) {
    const int32_t max_integer = INT32_MAX / AQ_FIXED_ONE;
    if (!aq_fixed_dimension_valid(rect.w) || !aq_fixed_dimension_valid(rect.h))
        return 0;
    if (rect.x < 0 || rect.y < 0 || rect.x > max_integer || rect.y > max_integer)
        return 0;
    if (rect.x > max_integer - (rect.w - 1) || rect.y > max_integer - (rect.h - 1))
        return 0;
    return 1;
}

static aq_fixed_t aq_fixed_div_dimension(int32_t numerator, int64_t denominator) {
    if (numerator <= 0 || denominator <= 0) return 0;
    int64_t value = ((int64_t)numerator * AQ_FIXED_ONE) / denominator;
    if (value > INT32_MAX) value = INT32_MAX;
    return (aq_fixed_t)value;
}

void aq_blit_scaled_alpha(AqSurface* dst, AqRect dst_rect,
                          const AqSurface* src, AqRect src_rect,
                          uint8_t alpha, int bilinear, int copy) {
    if (!dst || !src || !dst->pixels || !src->pixels || !aq_valid_rect(src, src_rect)
        || dst_rect.w <= 0 || dst_rect.h <= 0 || (alpha == 0 && !copy))
        return;

    if (!aq_fixed_source_rect_valid(src_rect))
        return;

    /* Clip before iterating. Do the edge arithmetic in int64_t so hostile
     * rectangle coordinates cannot overflow the destination loop variables. */
    int64_t requested_left = dst_rect.x;
    int64_t requested_top = dst_rect.y;
    int64_t requested_right = requested_left + dst_rect.w;
    int64_t requested_bottom = requested_top + dst_rect.h;
    int64_t clip_left_edge = dst->clip.x;
    int64_t clip_top_edge = dst->clip.y;
    int64_t clip_right_edge = clip_left_edge + dst->clip.w;
    int64_t clip_bottom_edge = clip_top_edge + dst->clip.h;
    int64_t visible_left = requested_left > clip_left_edge ? requested_left : clip_left_edge;
    int64_t visible_top = requested_top > clip_top_edge ? requested_top : clip_top_edge;
    int64_t visible_right = requested_right < clip_right_edge ? requested_right : clip_right_edge;
    int64_t visible_bottom = requested_bottom < clip_bottom_edge ? requested_bottom : clip_bottom_edge;
    if (visible_right <= visible_left || visible_bottom <= visible_top)
        return;

    uint64_t visible_width = (uint64_t)(visible_right - visible_left);
    uint64_t visible_height = (uint64_t)(visible_bottom - visible_top);
    if (visible_width == 0 || visible_height > AQ_SCALE_BLIT_PIXEL_BUDGET / visible_width)
        return;
    if (visible_left < INT32_MIN || visible_top < INT32_MIN ||
        visible_right > INT32_MAX || visible_bottom > INT32_MAX)
        return;

    aq_fixed_t step_x = bilinear
        ? aq_fixed_div_dimension(src_rect.w - 1, dst_rect.w > 1 ? (int64_t)dst_rect.w - 1 : 1)
        : aq_fixed_div_dimension(src_rect.w, dst_rect.w);
    aq_fixed_t step_y = bilinear
        ? aq_fixed_div_dimension(src_rect.h - 1, dst_rect.h > 1 ? (int64_t)dst_rect.h - 1 : 1)
        : aq_fixed_div_dimension(src_rect.h, dst_rect.h);

    int32_t dx0 = (int32_t)visible_left;
    int32_t dy0 = (int32_t)visible_top;
    int32_t dw = (int32_t)(visible_right - visible_left);
    int32_t dh = (int32_t)(visible_bottom - visible_top);
    int32_t clip_left = (int32_t)(visible_left - requested_left);
    int32_t clip_top = (int32_t)(visible_top - requested_top);

    int64_t src_y_fp = (int64_t)AQ_INT_TO_FIXED(src_rect.y) + (int64_t)step_y * clip_top;
    for (int32_t y = 0; y < dh; y++) {
        aq_fixed_t src_y = (aq_fixed_t)src_y_fp;
        int32_t sy = AQ_FIXED_TO_INT(src_y);
        if (sy >= src_rect.y + src_rect.h) sy = src_rect.y + src_rect.h - 1;

        int64_t src_x_fp = (int64_t)AQ_INT_TO_FIXED(src_rect.x) + (int64_t)step_x * clip_left;
        for (int32_t x = 0; x < dw; x++) {
            aq_fixed_t src_x = (aq_fixed_t)src_x_fp;
            int32_t sx = AQ_FIXED_TO_INT(src_x);
            if (sx >= src_rect.x + src_rect.w) sx = src_rect.x + src_rect.w - 1;

            AqColor c;
            if (!bilinear) {
                c = aq_get_pixel(src, sx, sy);
            } else {
                uint32_t fx = (AQ_FIXED_FRAC(src_x) >> 8) & 0xFF;
                int32_t sx1 = sx + 1;
                if (sx1 >= src_rect.x + src_rect.w) sx1 = src_rect.x + src_rect.w - 1;
                uint32_t fy = (AQ_FIXED_FRAC(src_y) >> 8) & 0xFF;
                int32_t sy1 = sy + 1;
                if (sy1 >= src_rect.y + src_rect.h) sy1 = src_rect.y + src_rect.h - 1;
                AqColor top = aq_color_lerp(aq_get_pixel(src, sx, sy), aq_get_pixel(src, sx1, sy), (uint8_t)fx);
                AqColor bottom = aq_color_lerp(aq_get_pixel(src, sx, sy1), aq_get_pixel(src, sx1, sy1), (uint8_t)fx);
                c = aq_color_lerp(top, bottom, (uint8_t)fy);
            }
            c.a = (uint8_t)(((uint32_t)c.a * alpha + 127) / 255);
            if (copy) {
                aq_put_pixel(dst, dx0 + x, dy0 + y, c);
            } else if (c.a == 255) {
                aq_put_pixel(dst, dx0 + x, dy0 + y, c);
            } else if (c.a > 0) {
                aq_put_pixel_blend(dst, dx0 + x, dy0 + y, c);
            }
            src_x_fp += step_x;
        }
        src_y_fp += step_y;
    }
}

void aq_blit_scaled(AqSurface* dst, AqRect dst_rect,
                    const AqSurface* src, AqRect src_rect) {
    aq_blit_scaled_alpha(dst, dst_rect, src, src_rect, 255, 0, 0);
}

void aq_blit_scaled_bilinear(AqSurface* dst, AqRect dst_rect,
                             const AqSurface* src, AqRect src_rect) {
    aq_blit_scaled_alpha(dst, dst_rect, src, src_rect, 255, 1, 0);
}
