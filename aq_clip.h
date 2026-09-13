/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_clip.h - Clipping operations
 */

#ifndef AQ_CLIP_H
#define AQ_CLIP_H

#include "aq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * Rectangle clipping
 * Clips (x, y, w, h) to the surface clip region.
 * Returns 0 if fully clipped, 1 if visible.
 * ═══════════════════════════════════════════════════════════════*/

static inline int aq_clip_rect(const AqSurface* s, int32_t* x, int32_t* y,
                               int32_t* w, int32_t* h) {
    int32_t cx = s->clip.x;
    int32_t cy = s->clip.y;
    int32_t cw = s->clip.w;
    int32_t ch = s->clip.h;

    /* Left edge */
    if (*x < cx) {
        *w -= (cx - *x);
        *x = cx;
    }
    /* Top edge */
    if (*y < cy) {
        *h -= (cy - *y);
        *y = cy;
    }
    /* Right edge */
    if (*x + *w > cx + cw) {
        *w = cx + cw - *x;
    }
    /* Bottom edge */
    if (*y + *h > cy + ch) {
        *h = cy + ch - *y;
    }

    return (*w > 0 && *h > 0);
}

/* ═══════════════════════════════════════════════════════════════
 * Cohen-Sutherland line clipping
 * Clips line (x0,y0)-(x1,y1) to clip region.
 * Returns 0 if fully clipped.
 * ═══════════════════════════════════════════════════════════════*/

#define AQ_CS_INSIDE 0
#define AQ_CS_LEFT   1
#define AQ_CS_RIGHT  2
#define AQ_CS_BOTTOM 4
#define AQ_CS_TOP    8

static inline int aq_cs_code(const AqSurface* s, int32_t x, int32_t y) {
    int code = AQ_CS_INSIDE;
    int32_t xmin = s->clip.x;
    int32_t ymin = s->clip.y;
    int32_t xmax = s->clip.x + s->clip.w - 1;
    int32_t ymax = s->clip.y + s->clip.h - 1;

    if (x < xmin) code |= AQ_CS_LEFT;
    else if (x > xmax) code |= AQ_CS_RIGHT;
    if (y < ymin) code |= AQ_CS_TOP;
    else if (y > ymax) code |= AQ_CS_BOTTOM;
    return code;
}

static inline int aq_clip_line(const AqSurface* s, int32_t* x0, int32_t* y0,
                               int32_t* x1, int32_t* y1) {
    int32_t xmin = s->clip.x;
    int32_t ymin = s->clip.y;
    int32_t xmax = s->clip.x + s->clip.w - 1;
    int32_t ymax = s->clip.y + s->clip.h - 1;

    int code0 = aq_cs_code(s, *x0, *y0);
    int code1 = aq_cs_code(s, *x1, *y1);

    for (;;) {
        if (!(code0 | code1)) return 1;  /* Both inside */
        if (code0 & code1) return 0;     /* Both in same outside zone */

        int code_out = code0 ? code0 : code1;
        int32_t x = 0, y = 0;
        int32_t dx = *x1 - *x0;
        int32_t dy = *y1 - *y0;

        if (code_out & AQ_CS_BOTTOM) {
            x = *x0 + dx * (ymax - *y0) / (dy ? dy : 1);
            y = ymax;
        } else if (code_out & AQ_CS_TOP) {
            x = *x0 + dx * (ymin - *y0) / (dy ? dy : 1);
            y = ymin;
        } else if (code_out & AQ_CS_RIGHT) {
            y = *y0 + dy * (xmax - *x0) / (dx ? dx : 1);
            x = xmax;
        } else if (code_out & AQ_CS_LEFT) {
            y = *y0 + dy * (xmin - *x0) / (dx ? dx : 1);
            x = xmin;
        }

        if (code_out == code0) {
            *x0 = x; *y0 = y;
            code0 = aq_cs_code(s, x, y);
        } else {
            *x1 = x; *y1 = y;
            code1 = aq_cs_code(s, x, y);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Blit clipping
 * Clips source and destination rectangles for blit operations.
 * Returns 0 if fully clipped.
 * ═══════════════════════════════════════════════════════════════*/

static inline int aq_clip_blit(const AqSurface* dst, int32_t* dx, int32_t* dy,
                               const AqSurface* src, int32_t* sx, int32_t* sy,
                               int32_t* w, int32_t* h) {
    /* Clip to source bounds */
    if (*sx < 0) { *dx -= *sx; *w += *sx; *sx = 0; }
    if (*sy < 0) { *dy -= *sy; *h += *sy; *sy = 0; }
    if (*sx + *w > src->width) *w = src->width - *sx;
    if (*sy + *h > src->height) *h = src->height - *sy;

    /* Clip to dest clip region */
    int32_t cx = dst->clip.x;
    int32_t cy = dst->clip.y;
    int32_t cx2 = cx + dst->clip.w;
    int32_t cy2 = cy + dst->clip.h;

    if (*dx < cx) { int32_t d = cx - *dx; *sx += d; *w -= d; *dx = cx; }
    if (*dy < cy) { int32_t d = cy - *dy; *sy += d; *h -= d; *dy = cy; }
    if (*dx + *w > cx2) *w = cx2 - *dx;
    if (*dy + *h > cy2) *h = cy2 - *dy;

    return (*w > 0 && *h > 0);
}

#ifdef __cplusplus
}
#endif

#endif /* AQ_CLIP_H */
