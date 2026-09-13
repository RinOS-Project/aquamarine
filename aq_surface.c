/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_surface.c - Surface management implementation
 */

#include "aq_surface.h"
#include <rin/render_target.h>
#include <limits.h>

/* ═══════════════════════════════════════════════════════════════
 * Allocator state
 * ═══════════════════════════════════════════════════════════════*/

AqAllocFn g_aq_alloc = 0;
AqFreeFn  g_aq_free  = 0;

void aq_set_allocator(AqAllocFn alloc_fn, AqFreeFn free_fn) {
    g_aq_alloc = alloc_fn;
    g_aq_free  = free_fn;
}

static inline uint8_t aq_format_bpp(AqPixelFormat fmt) {
    switch (fmt) {
    case AQ_FORMAT_BGRA32: return 32;
    case AQ_FORMAT_RGBA32: return 32;
    case AQ_FORMAT_RGB24:  return 24;
    case AQ_FORMAT_A8:     return 8;
    case AQ_FORMAT_BGR24:  return 24;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * Surface lifecycle
 * ═══════════════════════════════════════════════════════════════*/

AqSurface* aq_surface_create(int32_t width, int32_t height, AqPixelFormat format) {
    uint8_t bpp;
    uint64_t row_bytes;
    uint64_t allocation_size;
    if (!g_aq_alloc || !g_aq_free || width <= 0 || height <= 0) return 0;

    bpp = aq_format_bpp(format);
    if (bpp == 0u) return 0;
    row_bytes = (uint64_t)(uint32_t)width * (uint64_t)(bpp / 8u);
    allocation_size = row_bytes * (uint64_t)(uint32_t)height;
    if (row_bytes > INT32_MAX || allocation_size == 0u ||
        allocation_size > UINT32_MAX)
        return 0;

    AqSurface* s = (AqSurface*)g_aq_alloc(sizeof(AqSurface));
    if (!s) return 0;

    int32_t pitch = (int32_t)row_bytes;
    uint32_t size = (uint32_t)allocation_size;

    s->pixels = (uint8_t*)g_aq_alloc(size);
    if (!s->pixels) {
        g_aq_free(s);
        return 0;
    }

    /* Zero the pixel data */
    aq_memset(s->pixels, 0, size);

    s->width = width;
    s->height = height;
    s->pitch = pitch;
    s->format = format;
    s->bpp = bpp;
    s->owns_pixels = 1;
    s->_pad[0] = s->_pad[1] = 0;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = width;
    s->clip.h = height;

    return s;
}

AqSurface* aq_surface_create_from(uint8_t* pixels, int32_t width, int32_t height,
                                  int32_t pitch, AqPixelFormat format) {
    uint8_t bpp;
    uint64_t row_bytes;
    if (!g_aq_alloc || !g_aq_free || !pixels || width <= 0 || height <= 0)
        return 0;
    bpp = aq_format_bpp(format);
    if (bpp == 0u) return 0;
    row_bytes = (uint64_t)(uint32_t)width * (uint64_t)(bpp / 8u);
    if (row_bytes > INT32_MAX || pitch <= 0 ||
        (uint64_t)(uint32_t)pitch < row_bytes)
        return 0;

    AqSurface* s = (AqSurface*)g_aq_alloc(sizeof(AqSurface));
    if (!s) return 0;

    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->pitch = pitch;
    s->format = format;
    s->bpp = bpp;
    s->owns_pixels = 0;
    s->_pad[0] = s->_pad[1] = 0;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = width;
    s->clip.h = height;

    return s;
}

int aq_surface_init_from_rin_render_target(AqSurface* surface,
                                           const RinRenderTarget* target) {
    AqPixelFormat format;
    if (!surface || !target || target->struct_size != sizeof(*target) ||
        target->version != RIN_RENDER_TARGET_VERSION ||
        target->reserved0 != 0u || !target->pixels || target->width == 0u ||
        target->height == 0u || target->width > INT32_MAX ||
        target->height > INT32_MAX ||
        (uint64_t)target->pitch < (uint64_t)target->width * 4u ||
        target->pitch > INT32_MAX ||
        target->generation == 0u || target->buffer_slot >= 2u ||
        target->flags != (RIN_RENDER_TARGET_FLAG_EXTERNAL |
                          RIN_RENDER_TARGET_FLAG_WRITABLE))
        return -1;
    if (target->format == RIN_RENDER_TARGET_FORMAT_BGRA32)
        format = AQ_FORMAT_BGRA32;
    else if (target->format == RIN_RENDER_TARGET_FORMAT_RGBA32)
        format = AQ_FORMAT_RGBA32;
    else
        return -1;
    surface->pixels = (uint8_t*)target->pixels;
    surface->width = (int32_t)target->width;
    surface->height = (int32_t)target->height;
    surface->pitch = (int32_t)target->pitch;
    surface->format = format;
    surface->bpp = 32u;
    surface->owns_pixels = 0u;
    surface->_pad[0] = 0u;
    surface->_pad[1] = 0u;
    surface->clip = (AqRect){0, 0, surface->width, surface->height};
    return 0;
}

AqSurface* aq_surface_create_sub(AqSurface* parent, AqRect region) {
    if (!g_aq_alloc || !parent) return 0;

    /* Clamp region to parent bounds */
    if (region.x < 0) { region.w += region.x; region.x = 0; }
    if (region.y < 0) { region.h += region.y; region.y = 0; }
    if (region.x + region.w > parent->width) region.w = parent->width - region.x;
    if (region.y + region.h > parent->height) region.h = parent->height - region.y;
    if (region.w <= 0 || region.h <= 0) return 0;

    AqSurface* s = (AqSurface*)g_aq_alloc(sizeof(AqSurface));
    if (!s) return 0;

    s->pixels = parent->pixels + region.y * parent->pitch + region.x * (parent->bpp / 8);
    s->width = region.w;
    s->height = region.h;
    s->pitch = parent->pitch;  /* Same pitch as parent */
    s->format = parent->format;
    s->bpp = parent->bpp;
    s->owns_pixels = 0;
    s->_pad[0] = s->_pad[1] = 0;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = region.w;
    s->clip.h = region.h;

    return s;
}

void aq_surface_destroy(AqSurface* surface) {
    if (!surface || !g_aq_free) return;
    if (surface->owns_pixels && surface->pixels) {
        g_aq_free(surface->pixels);
    }
    g_aq_free(surface);
}

/* ═══════════════════════════════════════════════════════════════
 * Clipping
 * ═══════════════════════════════════════════════════════════════*/

void aq_surface_set_clip(AqSurface* s, AqRect clip) {
    if (!s) return;
    /* Intersect with surface bounds */
    AqRect bounds = {0, 0, s->width, s->height};
    s->clip = aq_rect_intersect(clip, bounds);
}

void aq_surface_reset_clip(AqSurface* s) {
    if (!s) return;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = s->width;
    s->clip.h = s->height;
}

AqRect aq_surface_get_clip(const AqSurface* s) {
    if (!s) return AQ_RECT(0, 0, 0, 0);
    return s->clip;
}

/* ═══════════════════════════════════════════════════════════════
 * Surface operations
 * ═══════════════════════════════════════════════════════════════*/

void aq_surface_clear(AqSurface* s, AqColor color) {
    if (!s || !s->pixels) return;

    if (s->format == AQ_FORMAT_BGRA32) {
        for (int32_t y = 0; y < s->height; y++) {
            uint8_t* row = s->pixels + y * s->pitch;
            for (int32_t x = 0; x < s->width; x++) {
                row[x * 4 + 0] = color.b;
                row[x * 4 + 1] = color.g;
                row[x * 4 + 2] = color.r;
                row[x * 4 + 3] = color.a;
            }
        }
    } else if (s->format == AQ_FORMAT_RGBA32) {
        for (int32_t y = 0; y < s->height; y++) {
            uint8_t* row = s->pixels + y * s->pitch;
            for (int32_t x = 0; x < s->width; x++) {
                row[x * 4 + 0] = color.r;
                row[x * 4 + 1] = color.g;
                row[x * 4 + 2] = color.b;
                row[x * 4 + 3] = color.a;
            }
        }
    } else if (s->format == AQ_FORMAT_A8) {
        for (int32_t y = 0; y < s->height; y++) {
            aq_memset(s->pixels + y * s->pitch, color.a, (uint32_t)s->width);
        }
    } else if (s->format == AQ_FORMAT_BGR24) {
        for (int32_t y = 0; y < s->height; y++) {
            uint8_t* row = s->pixels + y * s->pitch;
            for (int32_t x = 0; x < s->width; x++) {
                row[x * 3 + 0] = color.b;
                row[x * 3 + 1] = color.g;
                row[x * 3 + 2] = color.r;
            }
        }
    } else {
        /* RGB24 */
        for (int32_t y = 0; y < s->height; y++) {
            uint8_t* row = s->pixels + y * s->pitch;
            for (int32_t x = 0; x < s->width; x++) {
                row[x * 3 + 0] = color.r;
                row[x * 3 + 1] = color.g;
                row[x * 3 + 2] = color.b;
            }
        }
    }
}
