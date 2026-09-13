/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_draw.c - Drawing primitives implementation
 */

#include "aq_draw.h"
#include "aq_blend.h"
#include "aq_clip.h"
#include "aq_math.h"
#include "aq_path.h"

/* Public direct-draw entry points share the same world-coordinate contract as
 * AqPath.  The older midpoint/scanline primitives predated that contract and
 * could subtract or multiply hostile values before clipping them. */
#define AQ_DRAW_PRIMITIVE_PIXEL_BUDGET UINT64_C(1048576)
#define AQ_DRAW_CONVEX_POINT_LIMIT 1024

typedef struct AqDrawVisibleBounds {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} AqDrawVisibleBounds;

static int aq_draw_coordinate_valid(int32_t coordinate) {
    return coordinate >= -AQ_PATH_COORDINATE_LIMIT &&
           coordinate <= AQ_PATH_COORDINATE_LIMIT;
}

static int aq_draw_point_valid(AqPoint point) {
    return aq_draw_coordinate_valid(point.x) &&
           aq_draw_coordinate_valid(point.y);
}

static int aq_draw_surface_clip_valid(const AqSurface* surface) {
    int64_t clip_right;
    int64_t clip_bottom;
    if (!surface || !surface->pixels || surface->width <= 0 ||
        surface->height <= 0 || surface->pitch <= 0 ||
        surface->clip.x < 0 || surface->clip.y < 0 ||
        surface->clip.w <= 0 || surface->clip.h <= 0)
        return 0;
    clip_right = (int64_t)surface->clip.x + surface->clip.w;
    clip_bottom = (int64_t)surface->clip.y + surface->clip.h;
    return clip_right <= surface->width && clip_bottom <= surface->height;
}

static int aq_draw_bounds_valid(int64_t left, int64_t top,
                                int64_t right, int64_t bottom) {
    return left >= -AQ_PATH_COORDINATE_LIMIT &&
           top >= -AQ_PATH_COORDINATE_LIMIT &&
           right <= AQ_PATH_COORDINATE_LIMIT &&
           bottom <= AQ_PATH_COORDINATE_LIMIT && left <= right &&
           top <= bottom;
}

/* Find the visible part before any loop mutates the surface.  All arithmetic
 * deliberately remains wide: AqSurface is caller-owned and direct-draw
 * coordinates may otherwise overflow before aq_clip_rect can run. */
static int aq_draw_visible_bounds(const AqSurface* surface, int64_t left,
                                  int64_t top, int64_t right, int64_t bottom,
                                  AqDrawVisibleBounds* visible_out) {
    int64_t clip_right;
    int64_t clip_bottom;
    int64_t visible_left;
    int64_t visible_top;
    int64_t visible_right;
    int64_t visible_bottom;
    if (!visible_out || !aq_draw_surface_clip_valid(surface) ||
        !aq_draw_bounds_valid(left, top, right, bottom))
        return 0;
    clip_right = (int64_t)surface->clip.x + surface->clip.w - 1;
    clip_bottom = (int64_t)surface->clip.y + surface->clip.h - 1;
    visible_left = left > surface->clip.x ? left : surface->clip.x;
    visible_top = top > surface->clip.y ? top : surface->clip.y;
    visible_right = right < clip_right ? right : clip_right;
    visible_bottom = bottom < clip_bottom ? bottom : clip_bottom;
    if (visible_left > visible_right || visible_top > visible_bottom)
        return 0;
    visible_out->left = (int32_t)visible_left;
    visible_out->top = (int32_t)visible_top;
    visible_out->right = (int32_t)visible_right;
    visible_out->bottom = (int32_t)visible_bottom;
    return 1;
}

static int aq_draw_visible_area_within_budget(
    const AqDrawVisibleBounds* visible) {
    uint64_t width;
    uint64_t height;
    if (!visible) return 0;
    width = (uint64_t)((int64_t)visible->right - visible->left + 1);
    height = (uint64_t)((int64_t)visible->bottom - visible->top + 1);
    return width != 0u && height != 0u &&
           width <= AQ_DRAW_PRIMITIVE_PIXEL_BUDGET / height;
}

static int aq_draw_visible_outline_within_budget(
    const AqDrawVisibleBounds* visible) {
    uint64_t width;
    uint64_t height;
    if (!visible) return 0;
    width = (uint64_t)((int64_t)visible->right - visible->left + 1);
    height = (uint64_t)((int64_t)visible->bottom - visible->top + 1);
    /* AA circles may blend four samples per visible row, while triangles
     * submit three bounded edges.  One quarter of the common budget is a
     * conservative shared upper bound for every outline owner. */
    return width <= AQ_DRAW_PRIMITIVE_PIXEL_BUDGET / 4u &&
           height <= AQ_DRAW_PRIMITIVE_PIXEL_BUDGET / 4u &&
           width + height <= AQ_DRAW_PRIMITIVE_PIXEL_BUDGET / 4u;
}

static uint64_t aq_draw_isqrt_u64(uint64_t value) {
    uint64_t low = 0u;
    uint64_t high = (uint64_t)AQ_PATH_COORDINATE_LIMIT * 64u;
    uint64_t result = 0u;
    if (value < high * high) high = value;
    while (low <= high) {
        uint64_t middle = low + (high - low) / 2u;
        if (middle == 0u || middle <= value / middle) {
            result = middle;
            low = middle + 1u;
        } else {
            high = middle - 1u;
        }
    }
    return result;
}

static int32_t aq_draw_circle_x_extent(int32_t radius, int64_t delta) {
    uint64_t radius_u = (uint64_t)radius;
    uint64_t delta_u = (uint64_t)(delta < 0 ? -delta : delta);
    if (delta_u > radius_u) return 0;
    return (int32_t)aq_draw_isqrt_u64(radius_u * radius_u -
                                       delta_u * delta_u);
}

static int32_t aq_draw_ellipse_x_extent(int32_t rx, int32_t ry,
                                        int64_t delta) {
    uint64_t delta_u = (uint64_t)(delta < 0 ? -delta : delta);
    if (delta_u > (uint64_t)ry) return 0;
    uint64_t rem = (uint64_t)ry * ry - delta_u * delta_u;
    /* sqrt(rem) is retained with six fractional bits so an elongated ellipse
     * does not lose its visible edge through integer truncation.  The scaled
     * product remains below UINT64_MAX for the public coordinate limit. */
    uint64_t root_q6 = aq_draw_isqrt_u64(rem * UINT64_C(4096));
    return (int32_t)(((uint64_t)rx * root_q6) /
                     ((uint64_t)ry * UINT64_C(64)));
}

static int aq_draw_rect_visible(const AqSurface* surface, int32_t x,
                                int32_t y, int32_t width, int32_t height,
                                AqDrawVisibleBounds* visible_out) {
    if (width <= 0 || height <= 0) return 0;
    return aq_draw_visible_bounds(surface, x, y,
                                  (int64_t)x + width - 1,
                                  (int64_t)y + height - 1, visible_out);
}

/* ═══════════════════════════════════════════════════════════════
 * Rectangles
 * ═══════════════════════════════════════════════════════════════*/

void aq_fill_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h, AqColor color) {
    if (!s || !s->pixels || color.a == 0) return;
    if (!aq_clip_rect(s, &x, &y, &w, &h)) return;

    if (color.a == 255 && s->format == AQ_FORMAT_BGRA32) {
        /* Optimized: pack color as uint32_t, write directly */
        uint32_t packed = aq_color_to_bgra32(color);
        for (int32_t py = 0; py < h; py++) {
            uint32_t* row = (uint32_t*)(s->pixels + (y + py) * s->pitch) + x;
            aq_memset32(row, packed, (uint32_t)w);
        }
    } else if (color.a == 255) {
        /* Opaque, non-BGRA32 */
        for (int32_t py = 0; py < h; py++) {
            for (int32_t px = 0; px < w; px++) {
                aq_put_pixel(s, x + px, y + py, color);
            }
        }
    } else {
        /* Alpha blending */
        for (int32_t py = 0; py < h; py++) {
            for (int32_t px = 0; px < w; px++) {
                aq_put_pixel_blend(s, x + px, y + py, color);
            }
        }
    }
}

void aq_draw_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h, AqColor color) {
    if (!s || w <= 0 || h <= 0) return;
    aq_hline(s, x, y, w, color);
    aq_hline(s, x, y + h - 1, w, color);
    if (h > 2) {
        aq_vline(s, x, y + 1, h - 2, color);
        aq_vline(s, x + w - 1, y + 1, h - 2, color);
    }
}

void aq_fill_round_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t radius, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t row;
    if (!s || color.a == 0 ||
        !aq_draw_rect_visible(s, x, y, w, h, &visible) ||
        !aq_draw_visible_area_within_budget(&visible))
        return;
    if (radius <= 0) {
        aq_fill_rect(s, x, y, w, h, color);
        return;
    }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    /* Radius one is the pixel-grid rectangle boundary.  Work with the
     * remaining corner extent so a 2x2 radius-one fill cannot collapse to
     * four empty spans. */
    --radius;
    if (radius == 0) {
        aq_fill_rect(s, x, y, w, h, color);
        return;
    }

    for (row = visible.top; row <= visible.bottom; ++row) {
        int64_t local_y = (int64_t)row - y;
        int64_t delta = 0;
        int32_t extent;
        int32_t left;
        int32_t right;
        if (local_y < radius)
            delta = radius - local_y;
        else if (local_y > (int64_t)h - 1 - radius)
            delta = local_y - ((int64_t)h - 1 - radius);
        extent = delta == 0 ? radius :
                 aq_draw_circle_x_extent(radius, delta);
        left = x + radius - extent;
        right = x + w - 1 - radius + extent;
        aq_hline(s, left, row, right - left + 1, color);
    }
}

void aq_draw_round_rect(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t radius, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t row;
    if (!s || color.a == 0 ||
        !aq_draw_rect_visible(s, x, y, w, h, &visible) ||
        !aq_draw_visible_outline_within_budget(&visible))
        return;
    if (radius <= 0) {
        aq_draw_rect(s, x, y, w, h, color);
        return;
    }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    --radius;
    if (radius == 0) {
        aq_draw_rect(s, x, y, w, h, color);
        return;
    }

    for (row = visible.top; row <= visible.bottom; ++row) {
        int64_t local_y = (int64_t)row - y;
        int64_t delta = 0;
        int32_t extent;
        int32_t left;
        int32_t right;
        if (local_y < radius)
            delta = radius - local_y;
        else if (local_y > (int64_t)h - 1 - radius)
            delta = local_y - ((int64_t)h - 1 - radius);
        extent = delta == 0 ? radius :
                 aq_draw_circle_x_extent(radius, delta);
        left = x + radius - extent;
        right = x + w - 1 - radius + extent;
        if ((int64_t)row == y || (int64_t)row == (int64_t)y + h - 1)
            aq_hline(s, left, row, right - left + 1, color);
        else {
            aq_put_pixel_blend(s, left, row, color);
            if (right != left)
                aq_put_pixel_blend(s, right, row, color);
        }
    }
}

void aq_draw_rect_stroke(AqSurface* s, int32_t x, int32_t y, int32_t w,
                         int32_t h, int32_t thickness, AqColor color) {
    AqDrawVisibleBounds visible;
    if (!s || w <= 0 || h <= 0 || thickness <= 0 ||
        !aq_draw_rect_visible(s, x, y, w, h, &visible) ||
        !aq_draw_visible_outline_within_budget(&visible)) return;
    if (thickness > w) thickness = w;
    if (thickness > h) thickness = h;
    aq_fill_rect(s, x, y, w, thickness, color);
    aq_fill_rect(s, x, (int32_t)((int64_t)y + h - thickness), w,
                 thickness, color);
    if ((int64_t)h > (int64_t)thickness * 2) {
        int32_t inner_height =
            (int32_t)((int64_t)h - (int64_t)thickness * 2);
        aq_fill_rect(s, x, y + thickness, thickness, inner_height, color);
        aq_fill_rect(s, (int32_t)((int64_t)x + w - thickness),
                     y + thickness, thickness, inner_height, color);
    }
}

void aq_draw_round_rect_stroke(AqSurface* s, int32_t x, int32_t y,
                               int32_t w, int32_t h, int32_t radius,
                               int32_t thickness, AqColor color) {
    AqDrawVisibleBounds visible;
    if (!s || w <= 0 || h <= 0 || radius < 0 || thickness <= 0 ||
        !aq_draw_rect_visible(s, x, y, w, h, &visible) ||
        !aq_draw_visible_outline_within_budget(&visible)) return;
    if (thickness > w) thickness = w;
    if (thickness > h) thickness = h;
    aq_fill_round_rect(s, x, y, w, thickness, radius, color);
    aq_fill_round_rect(s, x, (int32_t)((int64_t)y + h - thickness), w,
                       thickness, radius, color);
    if ((int64_t)h > (int64_t)thickness * 2) {
        int32_t inner_height =
            (int32_t)((int64_t)h - (int64_t)thickness * 2);
        aq_fill_round_rect(s, x, y + thickness, thickness, inner_height,
                           radius, color);
        aq_fill_round_rect(s, (int32_t)((int64_t)x + w - thickness),
                           y + thickness, thickness, inner_height, radius,
                           color);
    }
}

void aq_invert_rect(AqSurface* s, int32_t x, int32_t y, int32_t w,
                    int32_t h) {
    AqDrawVisibleBounds visible;
    int32_t row;
    int32_t column;
    if (!s || w <= 0 || h <= 0 ||
        !aq_draw_rect_visible(s, x, y, w, h, &visible) ||
        !aq_draw_visible_area_within_budget(&visible)) return;
    for (row = visible.top; row <= visible.bottom; ++row) {
        for (column = visible.left; column <= visible.right; ++column) {
            AqColor color = aq_get_pixel(s, column, row);
            color.r = (uint8_t)(255u - color.r);
            color.g = (uint8_t)(255u - color.g);
            color.b = (uint8_t)(255u - color.b);
            aq_put_pixel(s, column, row, color);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Lines
 * ═══════════════════════════════════════════════════════════════*/

void aq_hline(AqSurface* s, int32_t x, int32_t y, int32_t w, AqColor color) {
    if (!s || !s->pixels || color.a == 0 || w <= 0) return;
    int32_t h = 1;
    if (!aq_clip_rect(s, &x, &y, &w, &h)) return;

    if (color.a == 255 && s->format == AQ_FORMAT_BGRA32) {
        uint32_t packed = aq_color_to_bgra32(color);
        uint32_t* row = (uint32_t*)(s->pixels + y * s->pitch) + x;
        aq_memset32(row, packed, (uint32_t)w);
    } else if (color.a == 255) {
        for (int32_t i = 0; i < w; i++) aq_put_pixel(s, x + i, y, color);
    } else {
        for (int32_t i = 0; i < w; i++) aq_put_pixel_blend(s, x + i, y, color);
    }
}

void aq_vline(AqSurface* s, int32_t x, int32_t y, int32_t h, AqColor color) {
    if (!s || !s->pixels || color.a == 0 || h <= 0) return;
    int32_t w = 1;
    if (!aq_clip_rect(s, &x, &y, &w, &h)) return;

    if (color.a == 255) {
        for (int32_t i = 0; i < h; i++) aq_put_pixel(s, x, y + i, color);
    } else {
        for (int32_t i = 0; i < h; i++) aq_put_pixel_blend(s, x, y + i, color);
    }
}

static int aq_draw_line_coordinate_valid(int32_t coordinate) {
    return coordinate >= -AQ_PATH_COORDINATE_LIMIT &&
           coordinate <= AQ_PATH_COORDINATE_LIMIT;
}

static int aq_draw_line_input_valid(int32_t x0, int32_t y0, int32_t x1,
                                    int32_t y1) {
    return aq_draw_line_coordinate_valid(x0) &&
           aq_draw_line_coordinate_valid(y0) &&
           aq_draw_line_coordinate_valid(x1) &&
           aq_draw_line_coordinate_valid(y1);
}

void aq_line(AqSurface* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, AqColor color) {
    if (!s || color.a == 0) return;
    /* Keep the Cohen-Sutherland arithmetic and Bresenham work bounded before
     * subtracting endpoints.  Callers that exceed the public path range get
     * a fail-closed no-op rather than an overflowing or multi-million-pixel
     * traversal. */
    if (!aq_draw_line_input_valid(x0, y0, x1, y1)) return;
    if (!aq_clip_line(s, &x0, &y0, &x1, &y1)) return;

    int32_t dx = AQ_ABS(x1 - x0);
    int32_t dy = -AQ_ABS(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;

    for (;;) {
        aq_put_pixel_blend(s, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void aq_line_aa(AqSurface* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, AqColor color) {
    /* Wu's antialiased line algorithm */
    if (!s || color.a == 0) return;
    if (!aq_draw_line_input_valid(x0, y0, x1, y1)) return;
    /* Wu iteration is proportional to the endpoint distance. Clip before
     * choosing the major axis so a valid off-screen path is bounded by the
     * visible surface, rather than by its caller-owned world coordinates. */
    if (!aq_clip_line(s, &x0, &y0, &x1, &y1)) return;

    int32_t dx = AQ_ABS(x1 - x0);
    int32_t dy = AQ_ABS(y1 - y0);
    int steep = dy > dx;

    if (steep) {
        AQ_SWAP(x0, y0, int32_t);
        AQ_SWAP(x1, y1, int32_t);
        dx = AQ_ABS(x1 - x0);
    }
    if (x0 > x1) {
        AQ_SWAP(x0, x1, int32_t);
        AQ_SWAP(y0, y1, int32_t);
    }

    if (dx == 0) {
        if (steep) aq_put_pixel_blend(s, y0, x0, color);
        else aq_put_pixel_blend(s, x0, y0, color);
        return;
    }

    /* Fixed-point gradient (16.16) */
    aq_fixed_t gradient = aq_fixed_div(AQ_INT_TO_FIXED(y1 - y0), AQ_INT_TO_FIXED(dx));
    aq_fixed_t intery = AQ_INT_TO_FIXED(y0);

    for (int32_t x = x0; x <= x1; x++) {
        int32_t iy = AQ_FIXED_TO_INT(intery);
        uint32_t frac = AQ_FIXED_FRAC(intery) >> 8; /* 0-255 */
        uint8_t a1 = (uint8_t)(255 - frac);
        uint8_t a2 = (uint8_t)frac;

        AqColor c1 = color; c1.a = (uint8_t)((uint32_t)color.a * a1 / 255);
        AqColor c2 = color; c2.a = (uint8_t)((uint32_t)color.a * a2 / 255);

        if (steep) {
            aq_put_pixel_blend(s, iy, x, c1);
            aq_put_pixel_blend(s, iy + 1, x, c2);
        } else {
            aq_put_pixel_blend(s, x, iy, c1);
            aq_put_pixel_blend(s, x, iy + 1, c2);
        }
        intery += gradient;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Circles / Ellipses
 * ═══════════════════════════════════════════════════════════════*/

void aq_fill_circle(AqSurface* s, int32_t cx, int32_t cy, int32_t r, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t row;
    if (!s || r <= 0 || color.a == 0 ||
        !aq_draw_visible_bounds(s, (int64_t)cx - r, (int64_t)cy - r,
                                (int64_t)cx + r, (int64_t)cy + r,
                                &visible) ||
        !aq_draw_visible_area_within_budget(&visible))
        return;
    for (row = visible.top; row <= visible.bottom; ++row) {
        int32_t extent = aq_draw_circle_x_extent(r, (int64_t)row - cy);
        aq_hline(s, cx - extent, row, 2 * extent + 1, color);
    }
}

void aq_draw_circle(AqSurface* s, int32_t cx, int32_t cy, int32_t r, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t row;
    if (!s || r <= 0 || color.a == 0 ||
        !aq_draw_visible_bounds(s, (int64_t)cx - r, (int64_t)cy - r,
                                (int64_t)cx + r, (int64_t)cy + r,
                                &visible) ||
        !aq_draw_visible_outline_within_budget(&visible))
        return;
    for (row = visible.top; row <= visible.bottom; ++row) {
        int32_t extent = aq_draw_circle_x_extent(r, (int64_t)row - cy);
        aq_put_pixel_blend(s, cx - extent, row, color);
        if (extent != 0)
            aq_put_pixel_blend(s, cx + extent, row, color);
    }
}

void aq_draw_circle_aa(AqSurface* s, int32_t cx, int32_t cy, int32_t r, AqColor color) {
    AqDrawVisibleBounds visible;
    uint64_t radius_squared;
    int32_t row;
    if (!s || r <= 0 || color.a == 0 ||
        /* The horizontal AA fringe is part of the write footprint too. */
        !aq_draw_visible_bounds(s, (int64_t)cx - r - 1,
                                (int64_t)cy - r,
                                (int64_t)cx + r + 1,
                                (int64_t)cy + r,
                                &visible) ||
        !aq_draw_visible_outline_within_budget(&visible))
        return;
    radius_squared = (uint64_t)r * r;
    for (row = visible.top; row <= visible.bottom; ++row) {
        int64_t delta = (int64_t)row - cy;
        uint64_t delta_squared = (uint64_t)(delta < 0 ? -delta : delta);
        uint64_t remainder;
        int32_t extent;
        uint32_t fringe;
        AqColor core = color;
        AqColor outer = color;
        delta_squared *= delta_squared;
        remainder = radius_squared - delta_squared;
        extent = (int32_t)aq_draw_isqrt_u64(remainder);
        fringe = (uint32_t)(((remainder - (uint64_t)extent * extent) * 255u) /
                            ((uint64_t)extent * 2u + 1u));
        core.a = (uint8_t)((uint32_t)color.a * (255u - fringe) / 255u);
        outer.a = (uint8_t)((uint32_t)color.a * fringe / 255u);
        aq_put_pixel_blend(s, cx - extent, row, core);
        if (extent != 0)
            aq_put_pixel_blend(s, cx + extent, row, core);
        if (outer.a != 0u) {
            aq_put_pixel_blend(s, cx - extent - 1, row, outer);
            aq_put_pixel_blend(s, cx + extent + 1, row, outer);
        }
    }
}

void aq_fill_ellipse(AqSurface* s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t row;
    if (!s || rx <= 0 || ry <= 0 || color.a == 0 ||
        !aq_draw_visible_bounds(s, (int64_t)cx - rx, (int64_t)cy - ry,
                                (int64_t)cx + rx, (int64_t)cy + ry,
                                &visible) ||
        !aq_draw_visible_area_within_budget(&visible))
        return;
    for (row = visible.top; row <= visible.bottom; ++row) {
        int32_t extent = aq_draw_ellipse_x_extent(rx, ry,
                                                   (int64_t)row - cy);
        aq_hline(s, cx - extent, row, 2 * extent + 1, color);
    }
}

void aq_draw_ellipse(AqSurface* s, int32_t cx, int32_t cy, int32_t rx, int32_t ry, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t row;
    if (!s || rx <= 0 || ry <= 0 || color.a == 0 ||
        !aq_draw_visible_bounds(s, (int64_t)cx - rx, (int64_t)cy - ry,
                                (int64_t)cx + rx, (int64_t)cy + ry,
                                &visible) ||
        !aq_draw_visible_outline_within_budget(&visible))
        return;
    for (row = visible.top; row <= visible.bottom; ++row) {
        int32_t extent = aq_draw_ellipse_x_extent(rx, ry,
                                                   (int64_t)row - cy);
        aq_put_pixel_blend(s, cx - extent, row, color);
        if (extent != 0)
            aq_put_pixel_blend(s, cx + extent, row, color);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Triangles / Polygons
 * ═══════════════════════════════════════════════════════════════*/

static int32_t aq_draw_line_x_at_y(AqPoint first, AqPoint second,
                                    int32_t row) {
    int64_t height = (int64_t)second.y - first.y;
    int64_t numerator = ((int64_t)second.x - first.x) *
                        ((int64_t)row - first.y);
    if (height == 0) return first.x;
    return (int32_t)((int64_t)first.x + numerator / height);
}

void aq_fill_triangle(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t minx;
    int32_t maxx;
    int32_t miny;
    int32_t maxy;
    int32_t row;
    if (!s || color.a == 0 || !aq_draw_point_valid(p0) ||
        !aq_draw_point_valid(p1) || !aq_draw_point_valid(p2))
        return;
    minx = AQ_MIN(p0.x, AQ_MIN(p1.x, p2.x));
    maxx = AQ_MAX(p0.x, AQ_MAX(p1.x, p2.x));
    miny = AQ_MIN(p0.y, AQ_MIN(p1.y, p2.y));
    maxy = AQ_MAX(p0.y, AQ_MAX(p1.y, p2.y));
    if (!aq_draw_visible_bounds(s, minx, miny, maxx, maxy, &visible) ||
        !aq_draw_visible_area_within_budget(&visible))
        return;

    /* Sort by y: p0.y <= p1.y <= p2.y */
    if (p0.y > p1.y) { AQ_SWAP(p0, p1, AqPoint); }
    if (p0.y > p2.y) { AQ_SWAP(p0, p2, AqPoint); }
    if (p1.y > p2.y) { AQ_SWAP(p1, p2, AqPoint); }

    int32_t total_h = p2.y - p0.y;
    if (total_h == 0) {
        /* Degenerate: horizontal line */
        aq_hline(s, minx, p0.y, maxx - minx + 1, color);
        return;
    }

    for (row = visible.top; row <= visible.bottom; ++row) {
        int second_half = row > p1.y || p1.y == p0.y;
        int32_t xa = aq_draw_line_x_at_y(p0, p2, row);
        int32_t xb;

        xb = second_half
                 ? aq_draw_line_x_at_y(p1, p2, row)
                 : aq_draw_line_x_at_y(p0, p1, row);

        if (xa > xb) { AQ_SWAP(xa, xb, int32_t); }
        aq_hline(s, xa, row, xb - xa + 1, color);
    }
}

void aq_draw_triangle(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t minx;
    int32_t maxx;
    int32_t miny;
    int32_t maxy;
    if (!s || color.a == 0 || !aq_draw_point_valid(p0) ||
        !aq_draw_point_valid(p1) || !aq_draw_point_valid(p2))
        return;
    minx = AQ_MIN(p0.x, AQ_MIN(p1.x, p2.x));
    maxx = AQ_MAX(p0.x, AQ_MAX(p1.x, p2.x));
    miny = AQ_MIN(p0.y, AQ_MIN(p1.y, p2.y));
    maxy = AQ_MAX(p0.y, AQ_MAX(p1.y, p2.y));
    if (!aq_draw_visible_bounds(s, minx, miny, maxx, maxy, &visible) ||
        !aq_draw_visible_outline_within_budget(&visible))
        return;
    aq_line(s, p0.x, p0.y, p1.x, p1.y, color);
    aq_line(s, p1.x, p1.y, p2.x, p2.y, color);
    aq_line(s, p2.x, p2.y, p0.x, p0.y, color);
}

void aq_fill_convex_polygon(AqSurface* s, const AqPoint* pts, int32_t count, AqColor color) {
    AqDrawVisibleBounds visible;
    int32_t ymin;
    int32_t ymax;
    int32_t xmin;
    int32_t xmax;
    int32_t index;
    int32_t row;
    uint64_t rows;
    uint64_t width;
    if (!s || !pts || count < 3 || count > AQ_DRAW_CONVEX_POINT_LIMIT ||
        color.a == 0 || !aq_draw_point_valid(pts[0]))
        return;

    /* Find y range */
    ymin = ymax = pts[0].y;
    xmin = xmax = pts[0].x;
    for (index = 1; index < count; ++index) {
        if (!aq_draw_point_valid(pts[index])) return;
        if (pts[index].y < ymin) ymin = pts[index].y;
        if (pts[index].y > ymax) ymax = pts[index].y;
        if (pts[index].x < xmin) xmin = pts[index].x;
        if (pts[index].x > xmax) xmax = pts[index].x;
    }
    if (!aq_draw_visible_bounds(s, xmin, ymin, xmax, ymax, &visible) ||
        !aq_draw_visible_area_within_budget(&visible))
        return;
    rows = (uint64_t)((int64_t)visible.bottom - visible.top + 1);
    width = (uint64_t)((int64_t)visible.right - visible.left + 1);
    if (width > AQ_DRAW_PRIMITIVE_PIXEL_BUDGET / rows ||
        (uint64_t)count >
            (AQ_DRAW_PRIMITIVE_PIXEL_BUDGET - width * rows) / rows)
        return;

    /* Scanline fill */
    for (row = visible.top; row <= visible.bottom; ++row) {
        int32_t scan_min = INT32_MAX;
        int32_t scan_max = INT32_MIN;

        for (index = 0; index < count; ++index) {
            int32_t next = index + 1 == count ? 0 : index + 1;
            int32_t y0 = pts[index].y, y1 = pts[next].y;
            int32_t x0 = pts[index].x, x1 = pts[next].x;

            if ((y0 <= row && y1 > row) || (y1 <= row && y0 > row)) {
                int32_t x = (int32_t)((int64_t)x0 +
                    ((int64_t)row - y0) * ((int64_t)x1 - x0) /
                    ((int64_t)y1 - y0));
                if (x < scan_min) scan_min = x;
                if (x > scan_max) scan_max = x;
            }
        }

        if (scan_min <= scan_max) {
            aq_hline(s, scan_min, row, scan_max - scan_min + 1, color);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Bezier Curves
 * ═══════════════════════════════════════════════════════════════*/

static int aq_draw_curve_point_valid(AqPathPoint point) {
    return point.x_q2 >= -AQ_PATH_COORDINATE_LIMIT * 4 &&
           point.x_q2 <= AQ_PATH_COORDINATE_LIMIT * 4 &&
           point.y_q2 >= -AQ_PATH_COORDINATE_LIMIT * 4 &&
           point.y_q2 <= AQ_PATH_COORDINATE_LIMIT * 4;
}

static int aq_draw_input_point_valid(AqPoint point) {
    return point.x >= -AQ_PATH_COORDINATE_LIMIT && point.x <= AQ_PATH_COORDINATE_LIMIT &&
           point.y >= -AQ_PATH_COORDINATE_LIMIT && point.y <= AQ_PATH_COORDINATE_LIMIT;
}

static int64_t aq_draw_abs64(int64_t value) {
    return value < 0 ? -value : value;
}

static AqPathPoint aq_draw_midpoint(AqPathPoint a, AqPathPoint b) {
    AqPathPoint result;
    result.x_q2 = (int32_t)(((int64_t)a.x_q2 + b.x_q2) / 2);
    result.y_q2 = (int32_t)(((int64_t)a.y_q2 + b.y_q2) / 2);
    return result;
}

static int aq_draw_curve_flat(AqPathPoint p0, AqPathPoint control, AqPathPoint p1) {
    int64_t dx = (int64_t)p1.x_q2 - p0.x_q2;
    int64_t dy = (int64_t)p1.y_q2 - p0.y_q2;
    int64_t cross = aq_draw_abs64(dx * ((int64_t)control.y_q2 - p0.y_q2) -
                                  dy * ((int64_t)control.x_q2 - p0.x_q2));
    int64_t length = aq_draw_abs64(dx) + aq_draw_abs64(dy);
    return length == 0 || cross <= length;
}

/* First determine that the whole curve can be decomposed within the bound.
 * The second pass may then draw, so a depth failure cannot leave a partial
 * curve on the destination surface. */
static int aq_draw_quad_preflight(AqPathPoint p0, AqPathPoint p1, AqPathPoint p2, uint32_t depth) {
    AqPathPoint p01;
    AqPathPoint p12;
    AqPathPoint p012;
    if (aq_draw_curve_flat(p0, p1, p2)) return 1;
    if (depth >= AQ_PATH_MAX_DEPTH) return 0;
    p01 = aq_draw_midpoint(p0, p1);
    p12 = aq_draw_midpoint(p1, p2);
    p012 = aq_draw_midpoint(p01, p12);
    return aq_draw_quad_preflight(p0, p01, p012, depth + 1) &&
           aq_draw_quad_preflight(p012, p12, p2, depth + 1);
}

static void aq_draw_quad_adaptive(AqSurface* s, AqPathPoint p0, AqPathPoint p1, AqPathPoint p2,
                                  AqColor color) {
    AqPathPoint p01;
    AqPathPoint p12;
    AqPathPoint p012;
    if (aq_draw_curve_flat(p0, p1, p2)) {
        aq_line(s, AQ_PATH_Q2_TO_INT(p0.x_q2), AQ_PATH_Q2_TO_INT(p0.y_q2),
                AQ_PATH_Q2_TO_INT(p2.x_q2), AQ_PATH_Q2_TO_INT(p2.y_q2), color);
        return;
    }
    p01 = aq_draw_midpoint(p0, p1);
    p12 = aq_draw_midpoint(p1, p2);
    p012 = aq_draw_midpoint(p01, p12);
    aq_draw_quad_adaptive(s, p0, p01, p012, color);
    aq_draw_quad_adaptive(s, p012, p12, p2, color);
}

void aq_bezier_quad(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqColor color) {
    AqPathPoint q0;
    AqPathPoint q1;
    AqPathPoint q2;
    if (!s || !s->pixels || color.a == 0 || !aq_draw_input_point_valid(p0) ||
        !aq_draw_input_point_valid(p1) || !aq_draw_input_point_valid(p2)) return;
    q0 = (AqPathPoint){ p0.x * 4, p0.y * 4 };
    q1 = (AqPathPoint){ p1.x * 4, p1.y * 4 };
    q2 = (AqPathPoint){ p2.x * 4, p2.y * 4 };
    if (!aq_draw_curve_point_valid(q0) ||
        !aq_draw_curve_point_valid(q1) || !aq_draw_curve_point_valid(q2) ||
        !aq_draw_quad_preflight(q0, q1, q2, 0)) return;
    aq_draw_quad_adaptive(s, q0, q1, q2, color);
}

static int aq_draw_cubic_preflight(AqPathPoint p0, AqPathPoint p1, AqPathPoint p2,
                                   AqPathPoint p3, uint32_t depth) {
    AqPathPoint p01;
    AqPathPoint p12;
    AqPathPoint p23;
    AqPathPoint p012;
    AqPathPoint p123;
    AqPathPoint p0123;
    if (aq_draw_curve_flat(p0, p1, p3) && aq_draw_curve_flat(p0, p2, p3)) return 1;
    if (depth >= AQ_PATH_MAX_DEPTH) return 0;
    p01 = aq_draw_midpoint(p0, p1);
    p12 = aq_draw_midpoint(p1, p2);
    p23 = aq_draw_midpoint(p2, p3);
    p012 = aq_draw_midpoint(p01, p12);
    p123 = aq_draw_midpoint(p12, p23);
    p0123 = aq_draw_midpoint(p012, p123);
    return aq_draw_cubic_preflight(p0, p01, p012, p0123, depth + 1) &&
           aq_draw_cubic_preflight(p0123, p123, p23, p3, depth + 1);
}

static void aq_draw_cubic_adaptive(AqSurface* s, AqPathPoint p0, AqPathPoint p1, AqPathPoint p2,
                                   AqPathPoint p3, AqColor color) {
    AqPathPoint p01;
    AqPathPoint p12;
    AqPathPoint p23;
    AqPathPoint p012;
    AqPathPoint p123;
    AqPathPoint p0123;
    if (aq_draw_curve_flat(p0, p1, p3) && aq_draw_curve_flat(p0, p2, p3)) {
        aq_line(s, AQ_PATH_Q2_TO_INT(p0.x_q2), AQ_PATH_Q2_TO_INT(p0.y_q2),
                AQ_PATH_Q2_TO_INT(p3.x_q2), AQ_PATH_Q2_TO_INT(p3.y_q2), color);
        return;
    }
    p01 = aq_draw_midpoint(p0, p1);
    p12 = aq_draw_midpoint(p1, p2);
    p23 = aq_draw_midpoint(p2, p3);
    p012 = aq_draw_midpoint(p01, p12);
    p123 = aq_draw_midpoint(p12, p23);
    p0123 = aq_draw_midpoint(p012, p123);
    aq_draw_cubic_adaptive(s, p0, p01, p012, p0123, color);
    aq_draw_cubic_adaptive(s, p0123, p123, p23, p3, color);
}

void aq_bezier_cubic(AqSurface* s, AqPoint p0, AqPoint p1, AqPoint p2, AqPoint p3, AqColor color) {
    AqPathPoint q0;
    AqPathPoint q1;
    AqPathPoint q2;
    AqPathPoint q3;
    if (!s || !s->pixels || color.a == 0 || !aq_draw_input_point_valid(p0) ||
        !aq_draw_input_point_valid(p1) || !aq_draw_input_point_valid(p2) ||
        !aq_draw_input_point_valid(p3)) return;
    q0 = (AqPathPoint){ p0.x * 4, p0.y * 4 };
    q1 = (AqPathPoint){ p1.x * 4, p1.y * 4 };
    q2 = (AqPathPoint){ p2.x * 4, p2.y * 4 };
    q3 = (AqPathPoint){ p3.x * 4, p3.y * 4 };
    if (!aq_draw_curve_point_valid(q0) ||
        !aq_draw_curve_point_valid(q1) || !aq_draw_curve_point_valid(q2) ||
        !aq_draw_curve_point_valid(q3) || !aq_draw_cubic_preflight(q0, q1, q2, q3, 0)) return;
    aq_draw_cubic_adaptive(s, q0, q1, q2, q3, color);
}

/* ═══════════════════════════════════════════════════════════════
 * Arcs
 * ═══════════════════════════════════════════════════════════════*/

void aq_draw_arc(AqSurface* s, int32_t cx, int32_t cy, int32_t r,
                 int32_t start_deg, int32_t end_deg, AqColor color) {
    if (!s || !s->pixels || r <= 0 || color.a == 0 ||
        cx < -AQ_PATH_COORDINATE_LIMIT || cx > AQ_PATH_COORDINATE_LIMIT ||
        cy < -AQ_PATH_COORDINATE_LIMIT || cy > AQ_PATH_COORDINATE_LIMIT ||
        r > AQ_PATH_COORDINATE_LIMIT || (int64_t)cx - r < -AQ_PATH_COORDINATE_LIMIT ||
        (int64_t)cx + r > AQ_PATH_COORDINATE_LIMIT || (int64_t)cy - r < -AQ_PATH_COORDINATE_LIMIT ||
        (int64_t)cy + r > AQ_PATH_COORDINATE_LIMIT) return;

    start_deg = start_deg % 360;
    if (start_deg < 0) start_deg += 360;
    end_deg = end_deg % 360;
    if (end_deg < 0) end_deg += 360;
    if (end_deg <= start_deg) end_deg += 360;

    for (int32_t deg = start_deg; deg < end_deg;) {
        int32_t next_deg = AQ_MIN(deg + 90, end_deg);
        int32_t delta = next_deg - deg;
        int32_t tangent_q10 = (aq_sin_q10(delta / 4) * 1024) /
                              AQ_MAX(aq_cos_q10(delta / 4), 1);
        int32_t kappa_q10 = (4 * tangent_q10) / 3;
        AqPoint p0 = AQ_PT(cx + (r * aq_cos_q10(deg)) / 1024,
                           cy - (r * aq_sin_q10(deg)) / 1024);
        AqPoint p3 = AQ_PT(cx + (r * aq_cos_q10(next_deg)) / 1024,
                           cy - (r * aq_sin_q10(next_deg)) / 1024);
        AqPoint p1 = AQ_PT(p0.x - ((r * aq_sin_q10(deg) / 1024) * kappa_q10) / 1024,
                           p0.y - ((r * aq_cos_q10(deg) / 1024) * kappa_q10) / 1024);
        AqPoint p2 = AQ_PT(p3.x + ((r * aq_sin_q10(next_deg) / 1024) * kappa_q10) / 1024,
                           p3.y + ((r * aq_cos_q10(next_deg) / 1024) * kappa_q10) / 1024);
        aq_bezier_cubic(s, p0, p1, p2, p3, color);
        deg = next_deg;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Effects
 * ═══════════════════════════════════════════════════════════════*/

void aq_box_shadow(AqSurface* s, int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t blur, int32_t spread, AqColor color) {
    if (!s || blur <= 0 || color.a == 0) return;

    int32_t sx = x - spread;
    int32_t sy = y - spread;
    int32_t sw = w + 2 * spread;
    int32_t sh = h + 2 * spread;

    for (int32_t ring = 1; ring <= blur; ring++) {
        uint8_t alpha = (uint8_t)((uint32_t)color.a * (blur - ring) / blur);
        if (alpha == 0) continue;

        AqColor rc = color;
        rc.a = alpha;

        int32_t rx = sx - ring;
        int32_t ry = sy - ring;
        int32_t rw = sw + 2 * ring;
        int32_t rh = sh + 2 * ring;

        /* Top and bottom edges */
        aq_hline(s, rx, ry, rw, rc);
        aq_hline(s, rx, ry + rh - 1, rw, rc);
        /* Left and right edges */
        aq_vline(s, rx, ry + 1, rh - 2, rc);
        aq_vline(s, rx + rw - 1, ry + 1, rh - 2, rc);
    }
}
