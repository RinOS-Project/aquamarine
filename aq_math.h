/*
 * Aquamarine - RinOS 2D Graphics Library
 * aq_math.h - Math utilities (fixed-point, trig tables)
 */

#ifndef AQ_MATH_H
#define AQ_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * Fixed-point 16.16
 * ═══════════════════════════════════════════════════════════════*/

typedef int32_t aq_fixed_t;

#define AQ_FIXED_SHIFT  16
#define AQ_FIXED_ONE    (1 << AQ_FIXED_SHIFT)
#define AQ_FIXED_HALF   (1 << (AQ_FIXED_SHIFT - 1))

#define AQ_INT_TO_FIXED(i)   ((aq_fixed_t)((aq_fixed_t)(i) * AQ_FIXED_ONE))
#define AQ_FIXED_TO_INT(f)   ((f) >> AQ_FIXED_SHIFT)
#define AQ_FIXED_FRAC(f)     ((f) & (AQ_FIXED_ONE - 1))
#define AQ_FIXED_ROUND(f)    (((f) + AQ_FIXED_HALF) >> AQ_FIXED_SHIFT)

static inline aq_fixed_t aq_fixed_mul(aq_fixed_t a, aq_fixed_t b) {
    return (aq_fixed_t)(((int64_t)a * b) >> AQ_FIXED_SHIFT);
}

static inline aq_fixed_t aq_fixed_div(aq_fixed_t a, aq_fixed_t b) {
    if (b == 0) return 0;
    return (aq_fixed_t)(((int64_t)a << AQ_FIXED_SHIFT) / b);
}

/* ═══════════════════════════════════════════════════════════════
 * Integer math
 * ═══════════════════════════════════════════════════════════════*/

#define AQ_ABS(x)        ((x) < 0 ? -(x) : (x))
#define AQ_MIN(a,b)      ((a) < (b) ? (a) : (b))
#define AQ_MAX(a,b)      ((a) > (b) ? (a) : (b))
#define AQ_CLAMP(x,lo,hi) AQ_MIN(AQ_MAX((x),(lo)),(hi))
#define AQ_SWAP(a,b,t)   do { t _tmp = (a); (a) = (b); (b) = _tmp; } while(0)

static inline int32_t aq_isqrt(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n;
    int32_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

/* ═══════════════════════════════════════════════════════════════
 * Sine/cosine table (0-90 degrees, value * 1024)
 * ═══════════════════════════════════════════════════════════════*/

static const int16_t aq_sin_table_q10[91] = {
       0,   18,   36,   54,   71,   89,  107,  125,  143,  160,
     178,  195,  213,  230,  248,  265,  282,  299,  316,  333,
     350,  366,  383,  399,  416,  431,  447,  462,  478,  493,
     508,  522,  537,  551,  565,  579,  593,  606,  619,  632,
     644,  656,  669,  680,  691,  702,  713,  724,  734,  744,
     754,  764,  773,  782,  790,  799,  807,  814,  822,  829,
     835,  842,  848,  854,  860,  865,  870,  875,  880,  884,
     888,  892,  895,  898,  901,  904,  906,  908,  910,  911,
     912,  913,  914,  914,  915,  915,  915,  914,  914,  913,
    1024
};

/* Returns sin(deg) * 1024, for arbitrary angle */
static inline int32_t aq_sin_q10(int32_t deg) {
    deg = deg % 360;
    if (deg < 0) deg += 360;

    if (deg <= 90) return aq_sin_table_q10[deg];
    if (deg <= 180) return aq_sin_table_q10[180 - deg];
    if (deg <= 270) return -aq_sin_table_q10[deg - 180];
    return -aq_sin_table_q10[360 - deg];
}

/* Returns cos(deg) * 1024 */
static inline int32_t aq_cos_q10(int32_t deg) {
    return aq_sin_q10(deg + 90);
}

/* Returns sin(deg) as fixed-point 16.16 */
static inline aq_fixed_t aq_sin_fixed(int32_t deg) {
    return ((aq_fixed_t)aq_sin_q10(deg) << AQ_FIXED_SHIFT) / 1024;
}

/* Returns cos(deg) as fixed-point 16.16 */
static inline aq_fixed_t aq_cos_fixed(int32_t deg) {
    return ((aq_fixed_t)aq_cos_q10(deg) << AQ_FIXED_SHIFT) / 1024;
}

#ifdef __cplusplus
}
#endif

#endif /* AQ_MATH_H */
