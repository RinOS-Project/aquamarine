/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_types.h - Core type definitions
 */

#ifndef AQ_TYPES_H
#define AQ_TYPES_H

#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
 * Pixel Format
 * ═══════════════════════════════════════════════════════════════*/

typedef enum {
    AQ_FORMAT_BGRA32 = 0,  /* Native framebuffer: B,G,R,A byte order */
    AQ_FORMAT_RGBA32 = 1,  /* Standard: R,G,B,A byte order */
    AQ_FORMAT_RGB24  = 2,  /* 3 bytes per pixel, no alpha */
    AQ_FORMAT_A8     = 3,  /* 1 byte per pixel, alpha/grayscale mask */
    AQ_FORMAT_BGR24  = 4,  /* 3 bytes per pixel, B,G,R byte order */
} AqPixelFormat;

/* ═══════════════════════════════════════════════════════════════
 * Color (RGBA)
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    uint8_t r, g, b, a;
} AqColor;

#define AQ_RGBA(rv,gv,bv,av)  ((AqColor){(rv),(gv),(bv),(av)})
#define AQ_RGB(rv,gv,bv)      ((AqColor){(rv),(gv),(bv),255})

/* Common colors */
#define AQ_WHITE      AQ_RGB(255,255,255)
#define AQ_BLACK      AQ_RGB(0,0,0)
#define AQ_RED        AQ_RGB(255,0,0)
#define AQ_GREEN      AQ_RGB(0,255,0)
#define AQ_BLUE       AQ_RGB(0,0,255)
#define AQ_TRANSPARENT AQ_RGBA(0,0,0,0)

/* ═══════════════════════════════════════════════════════════════
 * Geometry
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    int32_t x, y;
} AqPoint;

typedef struct {
    int32_t x, y, w, h;
} AqRect;

#define AQ_PT(xv,yv)        ((AqPoint){(xv),(yv)})
#define AQ_RECT(xv,yv,wv,hv) ((AqRect){(xv),(yv),(wv),(hv)})

/* ═══════════════════════════════════════════════════════════════
 * Font
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    const uint8_t* glyphs;
    int32_t glyph_w;
    int32_t glyph_h;
    int32_t first_char;
    int32_t last_char;
    int32_t bytes_per_glyph;
    uint32_t* unicode_lookup;
    uint32_t unicode_lookup_len;
    uint8_t owns_unicode_lookup;
    uint8_t reserved[3];
} AqFont;

/* ═══════════════════════════════════════════════════════════════
 * Surface
 * ═══════════════════════════════════════════════════════════════*/

typedef struct {
    uint8_t*      pixels;
    int32_t       width;
    int32_t       height;
    int32_t       pitch;        /* bytes per row */
    AqPixelFormat format;
    uint8_t       bpp;          /* 32, 24, or 8 */
    uint8_t       owns_pixels;  /* 1 = surface owns pixel data */
    uint8_t       _pad[2];
    AqRect        clip;         /* current clipping rectangle */
} AqSurface;

/* Allocator function types */
typedef void* (*AqAllocFn)(uint32_t size);
typedef void  (*AqFreeFn)(void* ptr);

/* ═══════════════════════════════════════════════════════════════
 * Color pack/unpack
 * ═══════════════════════════════════════════════════════════════*/

/* BGRA32: byte order [B, G, R, A] = native framebuffer */
static inline uint32_t aq_color_to_bgra32(AqColor c) {
    return (uint32_t)c.b | ((uint32_t)c.g << 8) |
           ((uint32_t)c.r << 16) | ((uint32_t)c.a << 24);
}

static inline AqColor aq_color_from_bgra32(uint32_t p) {
    AqColor c;
    c.b = (uint8_t)(p);
    c.g = (uint8_t)(p >> 8);
    c.r = (uint8_t)(p >> 16);
    c.a = (uint8_t)(p >> 24);
    return c;
}

/* RGBA32: byte order [R, G, B, A] */
static inline uint32_t aq_color_to_rgba32(AqColor c) {
    return (uint32_t)c.r | ((uint32_t)c.g << 8) |
           ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

static inline AqColor aq_color_from_rgba32(uint32_t p) {
    AqColor c;
    c.r = (uint8_t)(p);
    c.g = (uint8_t)(p >> 8);
    c.b = (uint8_t)(p >> 16);
    c.a = (uint8_t)(p >> 24);
    return c;
}

/* ARGB32: packed uint32_t 0xAARRGGBB (used by Image struct) */
static inline uint32_t aq_color_to_argb32(AqColor c) {
    return ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
           ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static inline AqColor aq_color_from_argb32(uint32_t p) {
    AqColor c;
    c.a = (uint8_t)(p >> 24);
    c.r = (uint8_t)(p >> 16);
    c.g = (uint8_t)(p >> 8);
    c.b = (uint8_t)(p);
    return c;
}

/* ═══════════════════════════════════════════════════════════════
 * GfxRGB interop (for migration from aqk_primitives.h)
 * ═══════════════════════════════════════════════════════════════*/

#ifdef GFX_RGB_DEFINED
static inline AqColor aq_color_from_gfx(GfxRGB rgb) {
    return AQ_RGB(rgb.r, rgb.g, rgb.b);
}
static inline GfxRGB aq_color_to_gfx(AqColor c) {
    GfxRGB rgb = {c.r, c.g, c.b};
    return rgb;
}
#endif

/* ═══════════════════════════════════════════════════════════════
 * Rect utilities
 * ═══════════════════════════════════════════════════════════════*/

static inline int aq_rect_is_empty(AqRect r) {
    return r.w <= 0 || r.h <= 0;
}

static inline int aq_rect_contains(AqRect r, int32_t x, int32_t y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static inline AqRect aq_rect_intersect(AqRect a, AqRect b) {
    int32_t x0 = a.x > b.x ? a.x : b.x;
    int32_t y0 = a.y > b.y ? a.y : b.y;
    int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    AqRect r;
    r.x = x0;
    r.y = y0;
    r.w = x1 > x0 ? x1 - x0 : 0;
    r.h = y1 > y0 ? y1 - y0 : 0;
    return r;
}

static inline AqRect aq_rect_union(AqRect a, AqRect b) {
    if (aq_rect_is_empty(a)) return b;
    if (aq_rect_is_empty(b)) return a;
    int32_t x0 = a.x < b.x ? a.x : b.x;
    int32_t y0 = a.y < b.y ? a.y : b.y;
    int32_t x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    AqRect r;
    r.x = x0;
    r.y = y0;
    r.w = x1 - x0;
    r.h = y1 - y0;
    return r;
}

/* ═══════════════════════════════════════════════════════════════
 * Memory helpers (no libc dependency)
 * ═══════════════════════════════════════════════════════════════*/

static inline void aq_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    /* Word-at-a-time copy for aligned, large blocks */
    while (n >= 8 && ((uintptr_t)d & 7) == 0 && ((uintptr_t)s & 7) == 0) {
        *(uint64_t*)d = *(const uint64_t*)s;
        d += 8; s += 8; n -= 8;
    }
    while (n--) *d++ = *s++;
}

static inline void aq_memset(void* dst, uint8_t val, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = val;
}

static inline void aq_memset32(void* dst, uint32_t val, uint32_t count) {
    uint32_t* d = (uint32_t*)dst;
    while (count--) *d++ = val;
}

#endif /* AQ_TYPES_H */
