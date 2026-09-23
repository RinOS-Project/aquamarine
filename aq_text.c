/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_text.c - Text rendering implementation
 */

#include "aq_text.h"
#include "aq_blend.h"
#include "aq_font_8x16.h"
#include "rin_unicode.h"
#include <limits.h>
#include <stddef.h>

/* ═══════════════════════════════════════════════════════════════
 * Built-in 8x16 font
 * ═══════════════════════════════════════════════════════════════*/

static const AqFont g_builtin_font = {
    .glyphs = (const uint8_t*)aq_font_data_8x16,
    .glyph_w = 8,
    .glyph_h = 16,
    .first_char = 32,
    .last_char = 126,
    .bytes_per_glyph = 16,
    .unicode_lookup = 0,
    .unicode_lookup_len = 0,
    .owns_unicode_lookup = 0,
    .reserved = {0, 0, 0},
};
static const AqFont* g_default_font;

const AqFont* aq_font_builtin_8x16(void) {
    return &g_builtin_font;
}

void aq_set_default_font(const AqFont* font) {
    __atomic_store_n(&g_default_font,
                     font ? font : &g_builtin_font,
                     __ATOMIC_RELEASE);
}

const AqFont* aq_font_default(void) {
    const AqFont* font = __atomic_load_n(&g_default_font, __ATOMIC_ACQUIRE);
    return font ? font : &g_builtin_font;
}

/* ═══════════════════════════════════════════════════════════════
 * PSF font loading
 * ═══════════════════════════════════════════════════════════════*/

/* PSF1 magic: 0x36, 0x04 */
#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

/* PSF2 magic: 0x72, 0xB5, 0x4A, 0x86 */
#define PSF2_MAGIC0 0x72
#define PSF2_MAGIC1 0xB5
#define PSF2_MAGIC2 0x4A
#define PSF2_MAGIC3 0x86

/* PSF data is accepted from files selected by the user. Keep the native
 * bitmap representation within the signed dimensions used by the drawing
 * loops, and make one pathological glyph no larger than the renderer's
 * bounded per-primitive pixel budget. */
#define AQ_FONT_MAX_GLYPHS 65536u
#define AQ_FONT_MAX_GLYPH_DIMENSION 1024u
#define AQ_FONT_MAX_GLYPH_BYTES \
    ((AQ_FONT_MAX_GLYPH_DIMENSION * AQ_FONT_MAX_GLYPH_DIMENSION) / 8u)

extern AqAllocFn g_aq_alloc;
extern AqFreeFn  g_aq_free;

static void aq_zero_u32(uint32_t* dst, uint32_t count) {
    if (!dst) return;
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = 0;
    }
}

static int aq_utf8_decode_from_bytes(const uint8_t** pp,
                                     const uint8_t* end,
                                     uint32_t* out_codepoint) {
    if (!pp || !*pp || !out_codepoint) return 0;
    const uint8_t* p = *pp;
    if (end && p >= end) return 0;

    uint8_t b0 = *p++;
    uint32_t cp = 0;
    int need = 0;

    if (b0 < 0x80) {
        cp = b0;
    } else if ((b0 & 0xE0) == 0xC0) {
        cp = (uint32_t)(b0 & 0x1F);
        need = 1;
    } else if ((b0 & 0xF0) == 0xE0) {
        cp = (uint32_t)(b0 & 0x0F);
        need = 2;
    } else if ((b0 & 0xF8) == 0xF0) {
        cp = (uint32_t)(b0 & 0x07);
        need = 3;
    } else {
        *out_codepoint = 0xFFFDu;
        *pp = p;
        return 1;
    }

    for (int i = 0; i < need; i++) {
        if (end && p >= end) {
            *out_codepoint = 0xFFFDu;
            *pp = p;
            return 1;
        }
        uint8_t bx = *p;
        if ((bx & 0xC0) != 0x80) {
            *out_codepoint = 0xFFFDu;
            *pp = p;
            return 1;
        }
        cp = (cp << 6) | (uint32_t)(bx & 0x3F);
        p++;
    }

    *out_codepoint = cp;
    *pp = p;
    return 1;
}

static int aq_font_parse_psf2_unicode_table(AqFont* font,
                                            const uint8_t* table,
                                            const uint8_t* end,
                                            uint32_t num_glyphs) {
    if (!font || !table || !end || table >= end || !g_aq_alloc) return 0;

    uint32_t max_codepoint = 0;
    int saw_mapping = 0;
    const uint8_t* p = table;
    uint32_t glyph = 0;

    while (glyph < num_glyphs && p < end) {
        while (p < end && *p != 0xFF) {
            if (*p == 0xFE) {
                p++;
                while (p < end && *p != 0xFF) {
                    uint32_t ignored = 0;
                    if (!aq_utf8_decode_from_bytes(&p, end, &ignored)) break;
                }
                break;
            }

            uint32_t cp = 0;
            if (!aq_utf8_decode_from_bytes(&p, end, &cp)) {
                break;
            }
            if (cp > max_codepoint) max_codepoint = cp;
            saw_mapping = 1;
        }

        if (p < end && *p == 0xFF) p++;
        glyph++;
    }

    if (!saw_mapping) return 0;

    uint32_t lookup_len = max_codepoint + 1;
    uint32_t* lookup = (uint32_t*)g_aq_alloc((size_t)lookup_len * sizeof(uint32_t));
    if (!lookup) return 0;
    aq_zero_u32(lookup, lookup_len);

    p = table;
    glyph = 0;
    while (glyph < num_glyphs && p < end) {
        while (p < end && *p != 0xFF) {
            if (*p == 0xFE) {
                p++;
                while (p < end && *p != 0xFF) {
                    uint32_t ignored = 0;
                    if (!aq_utf8_decode_from_bytes(&p, end, &ignored)) break;
                }
                break;
            }

            uint32_t cp = 0;
            if (!aq_utf8_decode_from_bytes(&p, end, &cp)) {
                break;
            }
            if (cp < lookup_len && lookup[cp] == 0) {
                lookup[cp] = glyph + 1;
            }
        }

        if (p < end && *p == 0xFF) p++;
        glyph++;
    }

    font->unicode_lookup = lookup;
    font->unicode_lookup_len = lookup_len;
    font->owns_unicode_lookup = 1;
    return 1;
}

AqFont* aq_font_load_psf(const uint8_t* data, uint32_t size) {
    if (!data || size < 4 || !g_aq_alloc) return 0;

    AqFont* font = (AqFont*)g_aq_alloc(sizeof(AqFont));
    if (!font) return 0;
    font->glyphs = 0;
    font->glyph_w = 0;
    font->glyph_h = 0;
    font->first_char = 0;
    font->last_char = -1;
    font->bytes_per_glyph = 0;
    font->unicode_lookup = 0;
    font->unicode_lookup_len = 0;
    font->owns_unicode_lookup = 0;
    font->reserved[0] = font->reserved[1] = font->reserved[2] = 0;

    /* Try PSF2 */
    if (size >= 32 &&
        data[0] == PSF2_MAGIC0 && data[1] == PSF2_MAGIC1 &&
        data[2] == PSF2_MAGIC2 && data[3] == PSF2_MAGIC3) {

        uint32_t header_size = data[8] | ((uint32_t)data[9] << 8) |
                               ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
        uint32_t num_glyphs  = data[16] | ((uint32_t)data[17] << 8) |
                               ((uint32_t)data[18] << 16) | ((uint32_t)data[19] << 24);
        uint32_t glyph_bytes = data[20] | ((uint32_t)data[21] << 8) |
                               ((uint32_t)data[22] << 16) | ((uint32_t)data[23] << 24);
        uint32_t glyph_h     = data[24] | ((uint32_t)data[25] << 8) |
                               ((uint32_t)data[26] << 16) | ((uint32_t)data[27] << 24);
        uint32_t glyph_w     = data[28] | ((uint32_t)data[29] << 8) |
                               ((uint32_t)data[30] << 16) | ((uint32_t)data[31] << 24);
        uint32_t flags       = data[12] | ((uint32_t)data[13] << 8) |
                               ((uint32_t)data[14] << 16) | ((uint32_t)data[15] << 24);
        uint64_t glyph_data_size;
        uint64_t glyph_end;
        uint64_t minimum_glyph_bytes;
        uint64_t glyph_row_bytes;

        glyph_row_bytes = ((uint64_t)glyph_w + 7u) / 8u;
        minimum_glyph_bytes = glyph_row_bytes * glyph_h;
        glyph_data_size = (uint64_t)num_glyphs * glyph_bytes;
        glyph_end = (uint64_t)header_size + glyph_data_size;
        if (header_size < 32u || header_size > size || num_glyphs == 0u ||
            num_glyphs > AQ_FONT_MAX_GLYPHS || glyph_bytes == 0u ||
            glyph_bytes > AQ_FONT_MAX_GLYPH_BYTES || glyph_h == 0u ||
            glyph_h > AQ_FONT_MAX_GLYPH_DIMENSION || glyph_w == 0u ||
            glyph_w > AQ_FONT_MAX_GLYPH_DIMENSION ||
            minimum_glyph_bytes > glyph_bytes || glyph_end > size) {
            g_aq_free(font);
            return 0;
        }

        font->glyphs = data + header_size;
        font->glyph_w = (int32_t)glyph_w;
        font->glyph_h = (int32_t)glyph_h;
        font->first_char = 0;
        font->last_char = (int32_t)(num_glyphs - 1);
        font->bytes_per_glyph = (int32_t)glyph_bytes;
        if ((flags & 0x01U) != 0) {
            const uint8_t* table = data + (size_t)glyph_end;
            const uint8_t* end = data + size;
            (void)aq_font_parse_psf2_unicode_table(font, table, end, num_glyphs);
        }
        return font;
    }

    /* Try PSF1 */
    if (size >= 4 && data[0] == PSF1_MAGIC0 && data[1] == PSF1_MAGIC1) {
        uint8_t mode = data[2];
        uint8_t charsize = data[3];
        uint32_t num_glyphs = (mode & 0x01) ? 512 : 256;

        if (charsize == 0u ||
            4u + num_glyphs * (uint32_t)charsize > size) {
            g_aq_free(font);
            return 0;
        }

        font->glyphs = data + 4;
        font->glyph_w = 8;
        font->glyph_h = (int32_t)charsize;
        font->first_char = 0;
        font->last_char = (int32_t)(num_glyphs - 1);
        font->bytes_per_glyph = (int32_t)charsize;
        return font;
    }

    g_aq_free(font);
    return 0;
}

AqFont* aq_font_load_resource_psf(
    const RinResourceCatalogV1* catalog, uint32_t resource_id,
    RinResourceCatalogReadPathFunction read_path, void* context,
    uint8_t* storage, uint64_t storage_capacity, uint64_t* storage_size) {
    RinResourceCatalogStatus status;

    if (storage_size == 0) return 0;
    *storage_size = 0u;
    if (storage_capacity > UINT32_MAX ||
        (storage_capacity != 0u && storage == 0))
        return 0;

    status = rin_resource_catalog_load(
        catalog, RIN_RESOURCE_CATALOG_TYPE_FONT, resource_id, read_path,
        context, storage, storage_capacity, storage_size);
    if (status != RIN_RESOURCE_CATALOG_OK || *storage_size > UINT32_MAX ||
        *storage_size == 0u)
        return 0;

    AqFont* font = aq_font_load_psf(storage, (uint32_t)*storage_size);
    if (!font) *storage_size = 0u;
    return font;
}

void aq_font_destroy(AqFont* font) {
    if (font && font != &g_builtin_font && g_aq_free) {
        if (font->owns_unicode_lookup && font->unicode_lookup) {
            g_aq_free(font->unicode_lookup);
        }
        g_aq_free(font);
    }
}

uint32_t aq_text_decode_utf8(const char** str) {
    if (!str || !*str || **str == '\0') return 0;
    const char* p = *str;
    uint32_t cp = 0;
    size_t remaining = 0u;
    size_t consumed = 0u;
    while (p[remaining] != '\0') remaining++;
    (void)rin_unicode_decode_utf8_lossy(p, remaining, &cp, &consumed);
    *str = p + consumed;
    return cp;
}

int32_t aq_font_lookup_glyph(const AqFont* font, uint32_t codepoint) {
    if (!font) font = aq_font_default();

    if (font->unicode_lookup &&
        codepoint < font->unicode_lookup_len &&
        font->unicode_lookup[codepoint] != 0) {
        return (int32_t)(font->unicode_lookup[codepoint] - 1);
    }

    if (!font->unicode_lookup && codepoint >= (uint32_t)font->first_char &&
        codepoint <= (uint32_t)font->last_char) {
        return (int32_t)codepoint - font->first_char;
    }

    /* A valid-but-unmapped scalar is drawn as a cell-sized tofu outline by
     * aq_draw_string.  U+FFFD itself remains a normal mapped glyph, so an
     * invalid UTF-8 maximal subpart is represented exactly once. */
    return -1;
}

int32_t aq_text_codepoint_advance(const AqFont* font, uint32_t codepoint) {
    int cells;
    int32_t narrow;
    if (!font) font = aq_font_default();
    cells = rin_unicode_cell_width(codepoint);
    if (cells <= 0) return 0;
    narrow = font->glyph_w >= 16 ? font->glyph_w / 2 : font->glyph_w;
    return cells == 2 ? narrow * 2 : narrow;
}

const uint8_t* aq_font_glyph_data(const AqFont* font, int32_t glyph_index) {
    int64_t glyph_count;
    int64_t glyph_offset;
    if (!font) font = aq_font_default();
    if (!font->glyphs || glyph_index < 0 || font->bytes_per_glyph <= 0) return 0;

    glyph_count = (int64_t)font->last_char - font->first_char + 1;
    if (glyph_count <= 0 || glyph_index >= glyph_count) return 0;
    glyph_offset = (int64_t)glyph_index * font->bytes_per_glyph;
    if (glyph_offset < 0 || glyph_offset > PTRDIFF_MAX) return 0;
    return font->glyphs + glyph_offset;
}

static void aq_draw_missing_glyph(AqSurface* s, int32_t x, int32_t y,
                                  int32_t width, int32_t height,
                                  AqColor color) {
    int32_t col;
    int32_t row;
    if (!s || width < 3 || height < 3) return;
    for (col = 1; col < width - 1; ++col) {
        aq_put_pixel_blend(s, x + col, y + 1, color);
        aq_put_pixel_blend(s, x + col, y + height - 2, color);
    }
    for (row = 1; row < height - 1; ++row) {
        aq_put_pixel_blend(s, x + 1, y + row, color);
        aq_put_pixel_blend(s, x + width - 2, y + row, color);
    }
    for (row = 3; row < height - 3 && row < width - 2; ++row) {
        aq_put_pixel_blend(s, x + row, y + row, color);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Text rendering
 * ═══════════════════════════════════════════════════════════════*/

static int aq_text_scale_valid(uint32_t numerator, uint32_t denominator)
{
    return numerator != 0u && denominator != 0u &&
           (uint64_t)numerator <= (uint64_t)denominator * 8u &&
           (uint64_t)denominator <= (uint64_t)numerator * 8u;
}

static int32_t aq_text_scale_floor(int32_t value, uint32_t numerator,
                                   uint32_t denominator)
{
    return (int32_t)(((int64_t)value * numerator) / denominator);
}

static int32_t aq_text_scale_ceil(int32_t value, uint32_t numerator,
                                  uint32_t denominator)
{
    return (int32_t)(((int64_t)value * numerator + denominator - 1u) /
                     denominator);
}

static int aq_text_scale_extent(int32_t value, uint32_t numerator,
                                uint32_t denominator, int32_t* out)
{
    int64_t rounded;
    if (!out || value < 0 || !aq_text_scale_valid(numerator, denominator))
        return 0;
    rounded = ((int64_t)value * numerator + denominator / 2u) / denominator;
    if (value > 0 && rounded == 0) rounded = 1;
    if (rounded > INT32_MAX) return 0;
    *out = (int32_t)rounded;
    return 1;
}

static void aq_draw_string_scaled_impl(
    AqSurface* s, int32_t x, int32_t y, const char* str, AqColor color,
    const AqFont* font, uint32_t numerator, uint32_t denominator,
    AqTextClipContainsFn clip_contains, void* clip_context)
{
    int32_t cx;
    int32_t previous_advance = 0;
    int32_t scaled_glyph_h;
    if (!s || !str || color.a == 0) return;
    if (!font) font = aq_font_default();
    if (!aq_text_scale_extent(font->glyph_h, numerator, denominator,
                              &scaled_glyph_h)) return;
    cx = x;
    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        int32_t advance;
        int32_t scaled_advance = 0;
        int32_t idx;
        const uint8_t* glyph;
        int32_t draw_x;
        if (cp == 0u) break;
        if (cp == '\r') continue;
        if (cp == '\n') {
            cx = x;
            y += scaled_glyph_h;
            previous_advance = 0;
            continue;
        }
        advance = aq_text_codepoint_advance(font, cp);
        if (advance < 0) continue;
        if (advance > 0 && !aq_text_scale_extent(advance, numerator,
                                                 denominator,
                                                 &scaled_advance))
            continue;
        idx = aq_font_lookup_glyph(font, cp);
        glyph = aq_font_glyph_data(font, idx);
        draw_x = advance == 0 ? cx - previous_advance : cx;
        if (glyph) {
            int32_t bytes_per_row = (font->glyph_w + 7) / 8;
            int32_t draw_columns = advance > 0 && advance < font->glyph_w
                                       ? advance : font->glyph_w;
            int32_t row;
            for (row = 0; row < font->glyph_h; ++row) {
                int32_t y0 = aq_text_scale_floor(row, numerator, denominator);
                int32_t y1 = aq_text_scale_ceil(row + 1, numerator, denominator);
                int32_t col;
                for (col = 0; col < draw_columns; ++col) {
                    int32_t byte_idx = row * bytes_per_row + col / 8;
                    int32_t bit_idx = 7 - (col % 8);
                    int32_t x0;
                    int32_t x1;
                    int32_t py;
                    int32_t px;
                    if ((glyph[byte_idx] & (1 << bit_idx)) == 0) continue;
                    x0 = aq_text_scale_floor(col, numerator, denominator);
                    x1 = aq_text_scale_ceil(col + 1, numerator, denominator);
                    for (py = y0; py < y1; ++py)
                        for (px = x0; px < x1; ++px)
                            if (!clip_contains || clip_contains(
                                    clip_context, draw_x + px, y + py))
                                aq_put_pixel_blend(s, draw_x + px, y + py,
                                                   color);
                }
            }
        } else {
            /* Missing glyphs remain visible while respecting the scaled
             * advance; the loaded-font path above performs true raster
             * scaling for every bitmap bit. */
            aq_draw_missing_glyph(s, draw_x, y, scaled_advance,
                                  scaled_glyph_h, color);
        }
        cx += scaled_advance;
        previous_advance = scaled_advance;
    }
}

void aq_draw_char(AqSurface* s, int32_t x, int32_t y, char c,
                  AqColor color, const AqFont* font) {
    if (!s || color.a == 0) return;
    if (!font) font = aq_font_default();

    int32_t idx = aq_font_lookup_glyph(font, (uint32_t)(uint8_t)c);
    const uint8_t* glyph = aq_font_glyph_data(font, idx);
    if (!glyph) {
        aq_draw_missing_glyph(s, x, y, font->glyph_w, font->glyph_h, color);
        return;
    }
    int32_t bytes_per_row = (font->glyph_w + 7) / 8;

    for (int32_t row = 0; row < font->glyph_h; row++) {
        for (int32_t col = 0; col < font->glyph_w; col++) {
            int32_t byte_idx = row * bytes_per_row + col / 8;
            int32_t bit_idx = 7 - (col % 8);

            if (glyph[byte_idx] & (1 << bit_idx)) {
                aq_put_pixel_blend(s, x + col, y + row, color);
            }
        }
    }
}

void aq_draw_string(AqSurface* s, int32_t x, int32_t y, const char* str,
                    AqColor color, const AqFont* font) {
    if (!s || !str) return;
    if (!font) font = aq_font_default();

    int32_t cx = x;
    int32_t previous_advance = 0;
    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        if (cp == 0) break;
        if (cp == '\r') continue;
        if (cp == '\n') {
            cx = x;
            y += font->glyph_h;
            previous_advance = 0;
        } else {
            int32_t advance = aq_text_codepoint_advance(font, cp);
            int32_t idx = aq_font_lookup_glyph(font, cp);
            const uint8_t* glyph = aq_font_glyph_data(font, idx);
            int32_t draw_x = advance == 0 ? cx - previous_advance : cx;
            if (glyph) {
                int32_t bytes_per_row = (font->glyph_w + 7) / 8;
                int32_t draw_columns =
                    advance > 0 && advance < font->glyph_w
                        ? advance : font->glyph_w;
                for (int32_t row = 0; row < font->glyph_h; row++) {
                    for (int32_t col = 0; col < draw_columns; col++) {
                        int32_t byte_idx = row * bytes_per_row + col / 8;
                        int32_t bit_idx = 7 - (col % 8);
                        if (glyph[byte_idx] & (1 << bit_idx)) {
                            aq_put_pixel_blend(s, draw_x + col, y + row, color);
                        }
                    }
                }
            } else if (advance > 0) {
                aq_draw_missing_glyph(s, draw_x, y, advance,
                                      font->glyph_h, color);
            }
            if (advance > 0) {
                cx += advance;
                previous_advance = advance;
            }
        }
    }
}

void aq_draw_string_scaled(AqSurface* s, int32_t x, int32_t y,
                           const char* str, AqColor color,
                           const AqFont* font, uint32_t numerator,
                           uint32_t denominator)
{
    aq_draw_string_scaled_impl(s, x, y, str, color, font, numerator,
                               denominator, NULL, NULL);
}

static void aq_draw_missing_glyph_clipped(
    AqSurface* s, int32_t x, int32_t y, int32_t width, int32_t height,
    AqColor color, AqTextClipContainsFn clip_contains, void* clip_context) {
    int32_t col;
    int32_t row;
    if (!s || width < 3 || height < 3) return;
    for (col = 1; col < width - 1; ++col) {
        if (!clip_contains || clip_contains(clip_context, x + col, y + 1))
            aq_put_pixel_blend(s, x + col, y + 1, color);
        if (!clip_contains ||
            clip_contains(clip_context, x + col, y + height - 2))
            aq_put_pixel_blend(s, x + col, y + height - 2, color);
    }
    for (row = 1; row < height - 1; ++row) {
        if (!clip_contains || clip_contains(clip_context, x + 1, y + row))
            aq_put_pixel_blend(s, x + 1, y + row, color);
        if (!clip_contains ||
            clip_contains(clip_context, x + width - 2, y + row))
            aq_put_pixel_blend(s, x + width - 2, y + row, color);
    }
    for (row = 3; row < height - 3 && row < width - 2; ++row) {
        if (!clip_contains || clip_contains(clip_context, x + row, y + row))
            aq_put_pixel_blend(s, x + row, y + row, color);
    }
}

void aq_draw_string_clipped(AqSurface* s, int32_t x, int32_t y,
                            const char* str, AqColor color,
                            const AqFont* font,
                            AqTextClipContainsFn clip_contains,
                            void* clip_context) {
    int32_t cx;
    int32_t previous_advance = 0;
    if (!s || !str) return;
    if (!font) font = aq_font_default();

    cx = x;
    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        if (cp == 0) break;
        if (cp == '\r') continue;
        if (cp == '\n') {
            cx = x;
            y += font->glyph_h;
            previous_advance = 0;
        } else {
            int32_t advance = aq_text_codepoint_advance(font, cp);
            int32_t idx = aq_font_lookup_glyph(font, cp);
            const uint8_t* glyph = aq_font_glyph_data(font, idx);
            int32_t draw_x = advance == 0 ? cx - previous_advance : cx;
            if (glyph) {
                int32_t bytes_per_row = (font->glyph_w + 7) / 8;
                int32_t draw_columns =
                    advance > 0 && advance < font->glyph_w
                        ? advance : font->glyph_w;
                for (int32_t row = 0; row < font->glyph_h; ++row) {
                    for (int32_t col = 0; col < draw_columns; ++col) {
                        int32_t byte_idx = row * bytes_per_row + col / 8;
                        int32_t bit_idx = 7 - (col % 8);
                        if ((glyph[byte_idx] & (1 << bit_idx)) &&
                            (!clip_contains || clip_contains(
                                clip_context, draw_x + col, y + row))) {
                            aq_put_pixel_blend(
                                s, draw_x + col, y + row, color);
                        }
                    }
                }
            } else if (advance > 0) {
                aq_draw_missing_glyph_clipped(
                    s, draw_x, y, advance, font->glyph_h, color,
                    clip_contains, clip_context);
            }
            if (advance > 0) {
                cx += advance;
                previous_advance = advance;
            }
        }
    }
}

void aq_draw_string_clipped_scaled(AqSurface* s, int32_t x, int32_t y,
                                   const char* str, AqColor color,
                                   const AqFont* font, uint32_t numerator,
                                   uint32_t denominator,
                                   AqTextClipContainsFn clip_contains,
                                   void* clip_context)
{
    aq_draw_string_scaled_impl(s, x, y, str, color, font, numerator,
                               denominator, clip_contains, clip_context);
}

static void aq_draw_missing_glyph_to_target(
    int32_t x, int32_t y, int32_t width, int32_t height, AqColor color,
    AqTextPixelFn put_pixel, void* context) {
    int32_t col;
    int32_t row;
    if (!put_pixel || width < 3 || height < 3 || color.a == 0) return;
    for (col = 1; col < width - 1; ++col) {
        put_pixel(context, x + col, y + 1, color);
        put_pixel(context, x + col, y + height - 2, color);
    }
    for (row = 1; row < height - 1; ++row) {
        put_pixel(context, x + 1, y + row, color);
        put_pixel(context, x + width - 2, y + row, color);
    }
    for (row = 3; row < height - 3 && row < width - 2; ++row)
        put_pixel(context, x + row, y + row, color);
}

void aq_draw_string_to_target(int32_t x, int32_t y, const char* str,
                              AqColor color, const AqFont* font,
                              AqTextPixelFn put_pixel, void* context) {
    int32_t cx;
    int32_t previous_advance = 0;
    if (!str || !put_pixel || color.a == 0) return;
    if (!font) font = aq_font_default();
    cx = x;
    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        if (cp == 0) break;
        if (cp == '\r') continue;
        if (cp == '\n') {
            cx = x;
            y += font->glyph_h;
            previous_advance = 0;
        } else {
            int32_t advance = aq_text_codepoint_advance(font, cp);
            int32_t glyph_index = aq_font_lookup_glyph(font, cp);
            const uint8_t* glyph = aq_font_glyph_data(font, glyph_index);
            int32_t draw_x = advance == 0 ? cx - previous_advance : cx;
            if (glyph) {
                int32_t bytes_per_row = (font->glyph_w + 7) / 8;
                int32_t draw_columns =
                    advance > 0 && advance < font->glyph_w
                        ? advance : font->glyph_w;
                for (int32_t row = 0; row < font->glyph_h; ++row) {
                    for (int32_t col = 0; col < draw_columns; ++col) {
                        int32_t byte_idx = row * bytes_per_row + col / 8;
                        int32_t bit_idx = 7 - (col % 8);
                        if (glyph[byte_idx] & (1 << bit_idx))
                            put_pixel(context, draw_x + col, y + row, color);
                    }
                }
            } else if (advance > 0) {
                aq_draw_missing_glyph_to_target(
                    draw_x, y, advance, font->glyph_h, color, put_pixel,
                    context);
            }
            if (advance > 0) {
                cx += advance;
                previous_advance = advance;
            }
        }
    }
}

static int aq_text_scale_axis_valid(uint32_t numerator,
                                    uint32_t denominator) {
    return numerator != 0u && denominator != 0u &&
           (uint64_t)numerator <= (uint64_t)denominator * 8u &&
           (uint64_t)denominator <= (uint64_t)numerator * 8u;
}

static int aq_text_scale_axis_extent(int32_t value, uint32_t numerator,
                                     uint32_t denominator,
                                     int32_t* out) {
    int64_t rounded;
    if (!out || value < 0 || !aq_text_scale_axis_valid(numerator, denominator))
        return 0;
    rounded = ((int64_t)value * numerator + denominator / 2u) / denominator;
    if (value > 0 && rounded == 0) rounded = 1;
    if (rounded > INT32_MAX) return 0;
    *out = (int32_t)rounded;
    return 1;
}

static int32_t aq_text_scale_axis_floor(int32_t value, uint32_t numerator,
                                         uint32_t denominator) {
    return (int32_t)(((int64_t)value * numerator) / denominator);
}

static int32_t aq_text_scale_axis_ceil(int32_t value, uint32_t numerator,
                                       uint32_t denominator) {
    return (int32_t)(((int64_t)value * numerator + denominator - 1u) /
                     denominator);
}

void aq_draw_string_to_target_scaled_xy(
    int32_t x, int32_t y, const char* str, AqColor color,
    const AqFont* font, uint32_t x_numerator, uint32_t x_denominator,
    uint32_t y_numerator, uint32_t y_denominator,
    AqTextPixelFn put_pixel, void* context) {
    int32_t cx;
    int32_t previous_advance = 0;
    int32_t scaled_glyph_h;
    if (!str || !put_pixel || color.a == 0u ||
        !aq_text_scale_axis_extent(
            font ? font->glyph_h : 16, y_numerator, y_denominator,
            &scaled_glyph_h))
        return;
    if (!font) font = aq_font_default();
    if (!aq_text_scale_axis_valid(x_numerator, x_denominator) ||
        !aq_text_scale_axis_valid(y_numerator, y_denominator))
        return;
    cx = x;
    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        int32_t advance;
        int32_t scaled_advance = 0;
        int32_t idx;
        const uint8_t* glyph;
        int32_t draw_x;
        if (cp == 0u) break;
        if (cp == '\r') continue;
        if (cp == '\n') {
            cx = x;
            y += scaled_glyph_h;
            previous_advance = 0;
            continue;
        }
        advance = aq_text_codepoint_advance(font, cp);
        if (advance < 0 ||
            (advance > 0 && !aq_text_scale_axis_extent(
                advance, x_numerator, x_denominator, &scaled_advance)))
            continue;
        idx = aq_font_lookup_glyph(font, cp);
        glyph = aq_font_glyph_data(font, idx);
        draw_x = advance == 0 ? cx - previous_advance : cx;
        if (glyph) {
            int32_t bytes_per_row = (font->glyph_w + 7) / 8;
            int32_t draw_columns = advance > 0 && advance < font->glyph_w
                                       ? advance : font->glyph_w;
            int32_t row;
            for (row = 0; row < font->glyph_h; ++row) {
                int32_t y0 = aq_text_scale_axis_floor(
                    row, y_numerator, y_denominator);
                int32_t y1 = aq_text_scale_axis_ceil(
                    row + 1, y_numerator, y_denominator);
                int32_t col;
                for (col = 0; col < draw_columns; ++col) {
                    int32_t byte_idx = row * bytes_per_row + col / 8;
                    int32_t bit_idx = 7 - (col % 8);
                    int32_t x0;
                    int32_t x1;
                    int32_t py;
                    int32_t px;
                    if ((glyph[byte_idx] & (1 << bit_idx)) == 0) continue;
                    x0 = aq_text_scale_axis_floor(
                        col, x_numerator, x_denominator);
                    x1 = aq_text_scale_axis_ceil(
                        col + 1, x_numerator, x_denominator);
                    for (py = y0; py < y1; ++py)
                        for (px = x0; px < x1; ++px)
                            put_pixel(context, draw_x + px, y + py, color);
                }
            }
        } else if (advance > 0) {
            int32_t col;
            int32_t row;
            int32_t width = scaled_advance;
            if (width >= 3 && scaled_glyph_h >= 3) {
                for (col = 1; col < width - 1; ++col) {
                    put_pixel(context, draw_x + col, y + 1, color);
                    put_pixel(context, draw_x + col,
                              y + scaled_glyph_h - 2, color);
                }
                for (row = 1; row < scaled_glyph_h - 1; ++row) {
                    put_pixel(context, draw_x + 1, y + row, color);
                    put_pixel(context, draw_x + width - 2, y + row, color);
                }
                for (row = 3; row < scaled_glyph_h - 3 && row < width - 2;
                     ++row)
                    put_pixel(context, draw_x + row, y + row, color);
            }
        }
        if (advance > 0) {
            cx += scaled_advance;
            previous_advance = scaled_advance;
        }
    }
}

typedef struct AqScaledSurfaceTextContext {
    AqSurface* surface;
    AqTextClipContainsFn clip_contains;
    void* clip_context;
} AqScaledSurfaceTextContext;

static void aq_scaled_surface_text_put_pixel(
    void* opaque, int32_t x, int32_t y, AqColor color) {
    AqScaledSurfaceTextContext* context =
        (AqScaledSurfaceTextContext*)opaque;
    if (!context || !context->surface ||
        (context->clip_contains && !context->clip_contains(
            context->clip_context, x, y)))
        return;
    aq_put_pixel_blend(context->surface, x, y, color);
}

void aq_draw_string_scaled_xy(AqSurface* s, int32_t x, int32_t y,
                              const char* str, AqColor color,
                              const AqFont* font, uint32_t x_numerator,
                              uint32_t x_denominator, uint32_t y_numerator,
                              uint32_t y_denominator) {
    AqScaledSurfaceTextContext context = {s, NULL, NULL};
    aq_draw_string_to_target_scaled_xy(
        x, y, str, color, font, x_numerator, x_denominator, y_numerator,
        y_denominator, aq_scaled_surface_text_put_pixel, &context);
}

void aq_draw_string_clipped_scaled_xy(
    AqSurface* s, int32_t x, int32_t y, const char* str, AqColor color,
    const AqFont* font, uint32_t x_numerator, uint32_t x_denominator,
    uint32_t y_numerator, uint32_t y_denominator,
    AqTextClipContainsFn clip_contains, void* clip_context) {
    AqScaledSurfaceTextContext context = {s, clip_contains, clip_context};
    aq_draw_string_to_target_scaled_xy(
        x, y, str, color, font, x_numerator, x_denominator, y_numerator,
        y_denominator, aq_scaled_surface_text_put_pixel, &context);
}

void aq_draw_string_centered(AqSurface* s, AqRect rect, const char* str,
                             AqColor color, const AqFont* font) {
    if (!s || !str) return;
    if (!font) font = aq_font_default();

    int32_t tw = aq_text_width(str, font);
    int32_t th = font->glyph_h;

    int32_t x = rect.x + (rect.w - tw) / 2;
    int32_t y = rect.y + (rect.h - th) / 2;

    aq_draw_string(s, x, y, str, color, font);
}

int32_t aq_text_width(const char* str, const AqFont* font) {
    if (!str) return 0;
    if (!font) font = aq_font_default();

    int32_t max_w = 0;
    int32_t cur_w = 0;

    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        if (cp == 0) break;
        if (cp == '\r') continue;
        if (cp == '\n') {
            if (cur_w > max_w) max_w = cur_w;
            cur_w = 0;
        } else {
            cur_w += aq_text_codepoint_advance(font, cp);
        }
    }

    return cur_w > max_w ? cur_w : max_w;
}

int32_t aq_text_height(const char* str, const AqFont* font) {
    if (!str || !*str) return 0;
    if (!font) font = aq_font_default();

    int32_t lines = 1;
    while (*str) {
        uint32_t cp = aq_text_decode_utf8(&str);
        if (cp == 0) break;
        if (cp == '\n') lines++;
    }
    return lines * font->glyph_h;
}

void aq_text_measure(const char* str, const AqFont* font, int32_t* out_w, int32_t* out_h) {
    if (out_w) *out_w = aq_text_width(str, font);
    if (out_h) *out_h = aq_text_height(str, font);
}

void aq_text_measure_scaled(const char* str, const AqFont* font,
                            uint32_t numerator, uint32_t denominator,
                            int32_t* out_w, int32_t* out_h)
{
    int32_t width = 0;
    int32_t height = 0;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!out_w || !out_h || !aq_text_scale_valid(numerator, denominator))
        return;
    aq_text_measure(str, font, &width, &height);
    if (!aq_text_scale_extent(width, numerator, denominator, out_w) ||
        !aq_text_scale_extent(height, numerator, denominator, out_h)) {
        *out_w = 0;
        *out_h = 0;
    }
}
