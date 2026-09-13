/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_color.c - HSV/HSL color conversion
 */

#include "aq_color.h"
#include "aq_math.h"

AqColor aq_color_from_hsv(int32_t h, int32_t s, int32_t v) {
    h = h % 360;
    if (h < 0) h += 360;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    if (v < 0) v = 0;
    if (v > 255) v = 255;

    if (s == 0) {
        return AQ_RGB((uint8_t)v, (uint8_t)v, (uint8_t)v);
    }

    int32_t region = h / 60;
    int32_t remainder = (h - region * 60) * 255 / 60;

    int32_t p = (v * (255 - s)) / 255;
    int32_t q = (v * (255 - (s * remainder) / 255)) / 255;
    int32_t t = (v * (255 - (s * (255 - remainder)) / 255)) / 255;

    uint8_t r, g, b;
    switch (region) {
    case 0:  r = (uint8_t)v; g = (uint8_t)t; b = (uint8_t)p; break;
    case 1:  r = (uint8_t)q; g = (uint8_t)v; b = (uint8_t)p; break;
    case 2:  r = (uint8_t)p; g = (uint8_t)v; b = (uint8_t)t; break;
    case 3:  r = (uint8_t)p; g = (uint8_t)q; b = (uint8_t)v; break;
    case 4:  r = (uint8_t)t; g = (uint8_t)p; b = (uint8_t)v; break;
    default: r = (uint8_t)v; g = (uint8_t)p; b = (uint8_t)q; break;
    }

    return AQ_RGB(r, g, b);
}

void aq_color_to_hsv(AqColor c, int32_t* h, int32_t* s, int32_t* v) {
    int32_t r = c.r, g = c.g, b = c.b;
    int32_t max_val = AQ_MAX(r, AQ_MAX(g, b));
    int32_t min_val = AQ_MIN(r, AQ_MIN(g, b));
    int32_t delta = max_val - min_val;

    *v = max_val;

    if (max_val == 0) {
        *s = 0;
        *h = 0;
        return;
    }

    *s = delta * 255 / max_val;

    if (delta == 0) {
        *h = 0;
        return;
    }

    if (r == max_val) {
        *h = 60 * (g - b) / delta;
    } else if (g == max_val) {
        *h = 120 + 60 * (b - r) / delta;
    } else {
        *h = 240 + 60 * (r - g) / delta;
    }

    if (*h < 0) *h += 360;
}

static int32_t hsl_hue_to_rgb(int32_t p, int32_t q, int32_t t) {
    if (t < 0) t += 360;
    if (t > 360) t -= 360;
    if (t < 60)  return p + (q - p) * t / 60;
    if (t < 180) return q;
    if (t < 240) return p + (q - p) * (240 - t) / 60;
    return p;
}

AqColor aq_color_from_hsl(int32_t h, int32_t s, int32_t l) {
    h = h % 360;
    if (h < 0) h += 360;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    if (l < 0) l = 0;
    if (l > 255) l = 255;

    if (s == 0) {
        return AQ_RGB((uint8_t)l, (uint8_t)l, (uint8_t)l);
    }

    int32_t q = l < 128 ? l * (255 + s) / 255 : l + s - l * s / 255;
    int32_t p = 2 * l - q;

    uint8_t r = (uint8_t)AQ_CLAMP(hsl_hue_to_rgb(p, q, h + 120), 0, 255);
    uint8_t g = (uint8_t)AQ_CLAMP(hsl_hue_to_rgb(p, q, h), 0, 255);
    uint8_t b = (uint8_t)AQ_CLAMP(hsl_hue_to_rgb(p, q, h - 120), 0, 255);

    return AQ_RGB(r, g, b);
}
