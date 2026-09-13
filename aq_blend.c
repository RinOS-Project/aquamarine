/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_blend.c - Alpha blending and blit implementation
 */

#include "aq_blend.h"
#include "aq_clip.h"

#if defined(AQ_ENABLE_SSE2) && \
    (defined(__i386__) || defined(__x86_64__)) && \
    (defined(__GNUC__) || defined(__clang__))
#include <emmintrin.h>
#define AQ_BLEND_HAS_COMPILED_SSE2 1
#else
#define AQ_BLEND_HAS_COMPILED_SSE2 0
#endif

static inline uint32_t aq_divide_by_255(uint32_t value) {
    /* Exact floor(value / 255) for the 0..65025 blend-product range. */
    return (value + 1u + (value >> 8)) >> 8;
}

static inline uint32_t aq_blend_bgra32_pixel(uint32_t sp, uint32_t dp) {
    uint32_t sa = sp >> 24;
    uint32_t da = 255u - sa;
    uint32_t rb;
    uint32_t g;
    uint32_t a;

    if (sa == 255u) return sp;
    if (sa == 0u) return dp;

    rb = (aq_divide_by_255((sp & 0x000000ffu) * sa +
                            (dp & 0x000000ffu) * da)) |
         (aq_divide_by_255(((sp >> 16) & 0xffu) * sa +
                            ((dp >> 16) & 0xffu) * da) << 16);
    g = aq_divide_by_255(((sp >> 8) & 0xffu) * sa +
                         ((dp >> 8) & 0xffu) * da) << 8;
    a = (sa + aq_divide_by_255((dp >> 24) * da)) << 24;
    return rb | g | a;
}

static void aq_blend_bgra32_row_scalar(uint32_t* dst,
                                        const uint32_t* src,
                                        int32_t count) {
    int32_t x;
    for (x = 0; x < count; x++) {
        dst[x] = aq_blend_bgra32_pixel(src[x], dst[x]);
    }
}

#if AQ_BLEND_HAS_COMPILED_SSE2
static int aq_sse2_state;

static int aq_cpu_has_sse2(void) {
    int state = __atomic_load_n(&aq_sse2_state, __ATOMIC_ACQUIRE);
    uint32_t eax = 0u;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    if (state != 0) return state == 2;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "cc");
    if (eax >= 1u) {
        eax = 1u;
        __asm__ volatile("cpuid"
                         : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         :
                         : "cc");
        state = (edx & (1u << 26)) != 0u ? 2 : 1;
    } else {
        state = 1;
    }
    {
        int expected = 0;
        (void)__atomic_compare_exchange_n(&aq_sse2_state, &expected, state, 0,
                                          __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
    return __atomic_load_n(&aq_sse2_state, __ATOMIC_ACQUIRE) == 2;
}

__attribute__((target("sse2")))
static __m128i aq_divide_by_255_sse2(__m128i value) {
    const __m128i one = _mm_set1_epi16(1);
    __m128i high = _mm_srli_epi16(value, 8);
    value = _mm_add_epi16(value, one);
    value = _mm_add_epi16(value, high);
    return _mm_srli_epi16(value, 8);
}

__attribute__((target("sse2")))
static void aq_blend_bgra32_row_sse2(uint32_t* dst,
                                     const uint32_t* src,
                                     int32_t count) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i all_255 = _mm_set1_epi16(255);
    const __m128i alpha_word_mask =
        _mm_set_epi16(255, 0, 0, 0, 255, 0, 0, 0);
    int32_t x = 0;

    for (; x + 4 <= count; x += 4) {
        __m128i src_bytes = _mm_loadu_si128((const __m128i*)(src + x));
        __m128i dst_bytes = _mm_loadu_si128((const __m128i*)(dst + x));
        __m128i src_lo = _mm_unpacklo_epi8(src_bytes, zero);
        __m128i src_hi = _mm_unpackhi_epi8(src_bytes, zero);
        __m128i dst_lo = _mm_unpacklo_epi8(dst_bytes, zero);
        __m128i dst_hi = _mm_unpackhi_epi8(dst_bytes, zero);
        __m128i alpha_lo = _mm_shufflelo_epi16(src_lo, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i alpha_hi = _mm_shufflelo_epi16(src_hi, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i inverse_lo;
        __m128i inverse_hi;
        __m128i result_lo;
        __m128i result_hi;

        alpha_lo = _mm_shufflehi_epi16(alpha_lo, _MM_SHUFFLE(3, 3, 3, 3));
        alpha_hi = _mm_shufflehi_epi16(alpha_hi, _MM_SHUFFLE(3, 3, 3, 3));
        inverse_lo = _mm_sub_epi16(all_255, alpha_lo);
        inverse_hi = _mm_sub_epi16(all_255, alpha_hi);

        /* Alpha output uses 255 * source-alpha, not source-alpha squared. */
        src_lo = _mm_or_si128(src_lo, alpha_word_mask);
        src_hi = _mm_or_si128(src_hi, alpha_word_mask);
        result_lo = _mm_add_epi16(_mm_mullo_epi16(src_lo, alpha_lo),
                                  _mm_mullo_epi16(dst_lo, inverse_lo));
        result_hi = _mm_add_epi16(_mm_mullo_epi16(src_hi, alpha_hi),
                                  _mm_mullo_epi16(dst_hi, inverse_hi));
        result_lo = aq_divide_by_255_sse2(result_lo);
        result_hi = aq_divide_by_255_sse2(result_hi);
        _mm_storeu_si128((__m128i*)(dst + x),
                         _mm_packus_epi16(result_lo, result_hi));
    }

    aq_blend_bgra32_row_scalar(dst + x, src + x, count - x);
}
#endif

void aq_blit(AqSurface* dst, int32_t dx, int32_t dy, const AqSurface* src) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    int32_t sx = 0, sy = 0;
    int32_t w = src->width, h = src->height;

    if (!aq_clip_blit(dst, &dx, &dy, src, &sx, &sy, &w, &h)) return;

    /* Fast path: BGRA32 to BGRA32 */
    if (src->format == AQ_FORMAT_BGRA32 && dst->format == AQ_FORMAT_BGRA32) {
#if AQ_BLEND_HAS_COMPILED_SSE2
        int use_sse2 = w >= 4 && aq_cpu_has_sse2();
#endif
        for (int32_t y = 0; y < h; y++) {
            const uint32_t* srow = (const uint32_t*)(src->pixels + (sy + y) * src->pitch) + sx;
            uint32_t* drow = (uint32_t*)(dst->pixels + (dy + y) * dst->pitch) + dx;
#if AQ_BLEND_HAS_COMPILED_SSE2
            if (use_sse2) {
                aq_blend_bgra32_row_sse2(drow, srow, w);
            } else
#endif
            {
                aq_blend_bgra32_row_scalar(drow, srow, w);
            }
        }
    } else {
        /* Generic path */
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                AqColor sc = aq_get_pixel(src, sx + x, sy + y);
                if (sc.a == 255) {
                    aq_put_pixel(dst, dx + x, dy + y, sc);
                } else if (sc.a > 0) {
                    AqColor dc = aq_get_pixel(dst, dx + x, dy + y);
                    aq_put_pixel(dst, dx + x, dy + y, aq_blend_over(sc, dc));
                }
            }
        }
    }
}

void aq_blit_pixels(AqSurface* dst, int32_t dx, int32_t dy, int32_t width,
                    int32_t height, const uint32_t* pixels) {
    AqSurface source;
    uint64_t row_bytes;
    uint64_t bytes;
    if (!dst || !pixels || width <= 0 || height <= 0 ||
        (row_bytes = (uint64_t)(uint32_t)width * 4u) == 0u ||
        row_bytes > INT32_MAX ||
        (bytes = row_bytes * (uint64_t)(uint32_t)height) == 0u ||
        bytes > 64u * 1024u * 1024u)
        return;
    source.pixels = (uint8_t*)pixels;
    source.width = width;
    source.height = height;
    source.pitch = (int32_t)row_bytes;
    source.format = AQ_FORMAT_BGRA32;
    source.bpp = 32u;
    source.owns_pixels = 0u;
    source._pad[0] = source._pad[1] = 0u;
    source.clip = (AqRect){0, 0, width, height};
    aq_blit(dst, dx, dy, &source);
}

void aq_blit_region(AqSurface* dst, int32_t dx, int32_t dy,
                    const AqSurface* src, AqRect src_rect) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    int32_t sx = src_rect.x, sy = src_rect.y;
    int32_t w = src_rect.w, h = src_rect.h;

    if (!aq_clip_blit(dst, &dx, &dy, src, &sx, &sy, &w, &h)) return;

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            AqColor sc = aq_get_pixel(src, sx + x, sy + y);
            if (sc.a == 255) {
                aq_put_pixel(dst, dx + x, dy + y, sc);
            } else if (sc.a > 0) {
                AqColor dc = aq_get_pixel(dst, dx + x, dy + y);
                aq_put_pixel(dst, dx + x, dy + y, aq_blend_over(sc, dc));
            }
        }
    }
}

void aq_blit_alpha(AqSurface* dst, int32_t dx, int32_t dy,
                   const AqSurface* src, uint8_t alpha) {
    if (!dst || !src || !dst->pixels || !src->pixels || alpha == 0) return;

    int32_t sx = 0, sy = 0;
    int32_t w = src->width, h = src->height;

    if (!aq_clip_blit(dst, &dx, &dy, src, &sx, &sy, &w, &h)) return;

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            AqColor sc = aq_get_pixel(src, sx + x, sy + y);
            AqColor dc = aq_get_pixel(dst, dx + x, dy + y);
            aq_put_pixel(dst, dx + x, dy + y, aq_blend_over_alpha(sc, dc, alpha));
        }
    }
}

void aq_blit_copy(AqSurface* dst, int32_t dx, int32_t dy,
                  const AqSurface* src, AqRect src_rect) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    int32_t sx = src_rect.x, sy = src_rect.y;
    int32_t w = src_rect.w, h = src_rect.h;

    if (!aq_clip_blit(dst, &dx, &dy, src, &sx, &sy, &w, &h)) return;

    /* Fast path: same format, same bpp */
    if (src->format == dst->format && src->bpp == dst->bpp) {
        int32_t bytes_per_pixel = src->bpp / 8;
        int32_t row_bytes = w * bytes_per_pixel;

        for (int32_t y = 0; y < h; y++) {
            const uint8_t* sptr = src->pixels + (sy + y) * src->pitch + sx * bytes_per_pixel;
            uint8_t* dptr = dst->pixels + (dy + y) * dst->pitch + dx * bytes_per_pixel;
            aq_memcpy(dptr, sptr, (uint32_t)row_bytes);
        }
    } else {
        /* Generic: pixel-by-pixel copy */
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                AqColor c = aq_get_pixel(src, sx + x, sy + y);
                aq_put_pixel(dst, dx + x, dy + y, c);
            }
        }
    }
}
