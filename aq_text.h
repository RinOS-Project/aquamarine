/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_text.h - Text rendering
 */

#ifndef AQ_TEXT_H
#define AQ_TEXT_H

#include "aq_types.h"
#include "aq_surface.h"
#include "../rinresource/include/rinresource/loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Get the built-in 8x16 bitmap font (no allocation needed) */
const AqFont* aq_font_builtin_8x16(void);

/* Load a PSF1/PSF2 font from memory buffer.
 * The data buffer must remain valid for the lifetime of the font. The loader
 * rejects malformed/overflowing headers and bitmap glyphs larger than
 * 1024x1024, rather than admitting a font whose native drawing loops cannot
 * bound safely. */
AqFont* aq_font_load_psf(const uint8_t* data, uint32_t size);

/* Load a PSF font selected by a public resource catalog.  The catalog and
 * path reader remain caller-owned; this function only copies the selected
 * blob/path result into storage and parses it.  storage must remain valid for
 * the lifetime of the returned font, and storage_size is cleared on failure. */
AqFont* aq_font_load_resource_psf(
    const RinResourceCatalogV1* catalog, uint32_t resource_id,
    RinResourceCatalogReadPathFunction read_path, void* context,
    uint8_t* storage, uint64_t storage_capacity, uint64_t* storage_size);

/* Destroy a dynamically loaded font */
void aq_font_destroy(AqFont* font);

/* Decode one UTF-8 codepoint and advance the pointer.
 * Invalid sequences produce U+FFFD and still consume input. */
uint32_t aq_text_decode_utf8(const char** str);

/* Look up a glyph index for a Unicode codepoint. Returns -1 on failure. */
int32_t aq_font_lookup_glyph(const AqFont* font, uint32_t codepoint);
int32_t aq_text_codepoint_advance(const AqFont* font, uint32_t codepoint);

/* Return the glyph bitmap pointer for a glyph index, or null if invalid. */
const uint8_t* aq_font_glyph_data(const AqFont* font, int32_t glyph_index);

/* Draw a single character */
void aq_draw_char(AqSurface* s, int32_t x, int32_t y, char c,
                  AqColor color, const AqFont* font);

/* Draw a string (supports \n for newlines) */
void aq_draw_string(AqSurface* s, int32_t x, int32_t y, const char* str,
                    AqColor color, const AqFont* font);

/* Draw and measure a string using a bounded device-independent scale.  The
 * bitmap is sampled with nearest-neighbour coverage so fractional DPI does
 * not fall back to the native fixed glyph raster. */
void aq_draw_string_scaled(AqSurface* s, int32_t x, int32_t y,
                           const char* str, AqColor color,
                           const AqFont* font, uint32_t numerator,
                           uint32_t denominator);
void aq_draw_string_scaled_xy(AqSurface* s, int32_t x, int32_t y,
                              const char* str, AqColor color,
                              const AqFont* font, uint32_t x_numerator,
                              uint32_t x_denominator, uint32_t y_numerator,
                              uint32_t y_denominator);

/* Optional per-pixel predicate for consumers whose clip is not rectangular.
 * Coordinates are in the destination surface's coordinate system. */
typedef int (*AqTextClipContainsFn)(void* context, int32_t x, int32_t y);
void aq_draw_string_clipped(AqSurface* s, int32_t x, int32_t y,
                            const char* str, AqColor color,
                            const AqFont* font,
                            AqTextClipContainsFn clip_contains,
                            void* clip_context);
void aq_draw_string_clipped_scaled(AqSurface* s, int32_t x, int32_t y,
                                   const char* str, AqColor color,
                                   const AqFont* font, uint32_t numerator,
                                   uint32_t denominator,
                                   AqTextClipContainsFn clip_contains,
                                   void* clip_context);
void aq_draw_string_clipped_scaled_xy(
    AqSurface* s, int32_t x, int32_t y, const char* str, AqColor color,
    const AqFont* font, uint32_t x_numerator, uint32_t x_denominator,
    uint32_t y_numerator, uint32_t y_denominator,
    AqTextClipContainsFn clip_contains, void* clip_context);

/*
 * Enumerate the opaque glyph pixels of a string without requiring a linear
 * AqSurface.  This is for destination layouts such as a bottom-up DIB where
 * the caller owns address translation and exact clipping.  The callback sees
 * destination coordinates and the requested text colour.
 */
typedef void (*AqTextPixelFn)(void* context, int32_t x, int32_t y,
                              AqColor color);
void aq_draw_string_to_target(int32_t x, int32_t y, const char* str,
                              AqColor color, const AqFont* font,
                              AqTextPixelFn put_pixel, void* context);

/* Draw to an arbitrary target using independent bounded horizontal and
 * vertical bitmap scales.  This is useful for compatibility APIs whose
 * selected font has a LOGFONT width that is not exactly half its height. */
void aq_draw_string_to_target_scaled_xy(
    int32_t x, int32_t y, const char* str, AqColor color,
    const AqFont* font, uint32_t x_numerator, uint32_t x_denominator,
    uint32_t y_numerator, uint32_t y_denominator,
    AqTextPixelFn put_pixel, void* context);

/* Draw string centered horizontally within a rect */
void aq_draw_string_centered(AqSurface* s, AqRect rect, const char* str,
                             AqColor color, const AqFont* font);

/* Measure text dimensions */
int32_t aq_text_width(const char* str, const AqFont* font);
int32_t aq_text_height(const char* str, const AqFont* font);
void    aq_text_measure(const char* str, const AqFont* font, int32_t* out_w, int32_t* out_h);
void    aq_text_measure_scaled(const char* str, const AqFont* font,
                               uint32_t numerator, uint32_t denominator,
                               int32_t* out_w, int32_t* out_h);

#ifdef __cplusplus
}
#endif

#endif /* AQ_TEXT_H */
