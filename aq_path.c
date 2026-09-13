/* Aquamarine bounded adaptive path implementation. */

#include "aq_path.h"

#include "aq_draw.h"
#include "aq_text.h"

#include <limits.h>
#include <stddef.h>

#define AQ_Q30_ONE 1073741824LL
#define AQ_Q30_PI 3373259426LL
#define AQ_Q30_TWO_PI 6746518852LL
#define AQ_Q30_HALF_PI 1686629713LL

/* Path filling is a public direct-rendering entry point.  Keep its work
 * bounded independently from the larger retained-path storage limits. */
#define AQ_PATH_FILL_PIXEL_BUDGET UINT64_C(1048576)
#define AQ_PATH_FILL_MAX_EDGES 4096u

static const int32_t g_aq_cordic_atan_q30[31] = {
    843314857, 497837829, 263043836, 133525159, 67021687, 33543516,
    16775851, 8388437, 4194283, 2097149, 1048575, 524288, 262144,
    131072, 65536, 32768, 16384, 8192, 4096, 2048, 1024, 512, 256,
    128, 64, 32, 16, 8, 4, 2, 1,
};

static int64_t aq_path_abs64(int64_t value) {
    return value < 0 ? -value : value;
}

static int aq_path_coordinate_valid(int32_t value) {
    return value >= -AQ_PATH_COORDINATE_LIMIT * 4 &&
           value <= AQ_PATH_COORDINATE_LIMIT * 4;
}

static int aq_path_point_valid(int32_t x_q2, int32_t y_q2) {
    return aq_path_coordinate_valid(x_q2) && aq_path_coordinate_valid(y_q2);
}

static void aq_path_fail(AqPath* path) {
    if (!path) return;
    aq_path_clear(path);
    path->failed = 1;
}

void aq_path_clear(AqPath* path) {
    if (!path) return;
    path->point_count = 0;
    path->contour_count = 0;
    path->current_contour = 0;
    path->current_x_q2 = 0;
    path->current_y_q2 = 0;
    path->has_current = 0;
    path->failed = 0;
}

int aq_path_init(AqPath* path, AqPathPoint* points, uint32_t point_capacity,
                 AqPathContour* contours, uint32_t contour_capacity) {
    if (!path || !points || !contours || point_capacity == 0 || contour_capacity == 0 ||
        point_capacity > AQ_PATH_MAX_POINTS || contour_capacity > AQ_PATH_MAX_CONTOURS) {
        if (path) aq_path_fail(path);
        return -1;
    }

    path->points = points;
    path->contours = contours;
    path->point_capacity = point_capacity;
    path->contour_capacity = contour_capacity;
    aq_path_clear(path);
    return 0;
}

static int aq_path_append_point(AqPath* path, int32_t x_q2, int32_t y_q2) {
    if (!path || path->failed || !aq_path_point_valid(x_q2, y_q2) ||
        path->point_count >= path->point_capacity || path->point_count >= AQ_PATH_MAX_POINTS) {
        aq_path_fail(path);
        return -1;
    }

    path->points[path->point_count].x_q2 = x_q2;
    path->points[path->point_count].y_q2 = y_q2;
    path->point_count++;
    path->current_x_q2 = x_q2;
    path->current_y_q2 = y_q2;
    return 0;
}

int aq_path_move_to(AqPath* path, int32_t x_q2, int32_t y_q2) {
    if (!path || path->failed || !aq_path_point_valid(x_q2, y_q2) ||
        path->contour_count >= path->contour_capacity ||
        path->contour_count >= AQ_PATH_MAX_CONTOURS) {
        aq_path_fail(path);
        return -1;
    }

    AqPathContour* contour = &path->contours[path->contour_count];
    contour->first_point = path->point_count;
    contour->point_count = 0;
    contour->closed = 0;
    contour->reserved[0] = contour->reserved[1] = contour->reserved[2] = 0;
    path->current_contour = path->contour_count++;
    path->has_current = 1;
    if (aq_path_append_point(path, x_q2, y_q2) != 0) return -1;
    contour->point_count = 1;
    return 0;
}

int aq_path_line_to(AqPath* path, int32_t x_q2, int32_t y_q2) {
    if (!path || path->failed || !path->has_current ||
        path->current_contour >= path->contour_count) {
        aq_path_fail(path);
        return -1;
    }
    if (aq_path_append_point(path, x_q2, y_q2) != 0) return -1;
    path->contours[path->current_contour].point_count++;
    return 0;
}

int aq_path_close(AqPath* path) {
    if (!path || path->failed || !path->has_current ||
        path->current_contour >= path->contour_count ||
        path->contours[path->current_contour].point_count == 0) {
        aq_path_fail(path);
        return -1;
    }
    path->contours[path->current_contour].closed = 1;
    return 0;
}

static int aq_path_curve_flat(AqPathPoint p0, AqPathPoint control,
                              AqPathPoint p1) {
    int64_t dx = (int64_t)p1.x_q2 - p0.x_q2;
    int64_t dy = (int64_t)p1.y_q2 - p0.y_q2;
    int64_t cross = aq_path_abs64(dx * ((int64_t)control.y_q2 - p0.y_q2) -
                                  dy * ((int64_t)control.x_q2 - p0.x_q2));
    int64_t length = aq_path_abs64(dx) + aq_path_abs64(dy);
    return length == 0 || cross <= (int64_t)AQ_PATH_FLATNESS_Q2 * length;
}

static AqPathPoint aq_path_midpoint(AqPathPoint a, AqPathPoint b) {
    AqPathPoint result;
    result.x_q2 = (int32_t)(((int64_t)a.x_q2 + b.x_q2) / 2);
    result.y_q2 = (int32_t)(((int64_t)a.y_q2 + b.y_q2) / 2);
    return result;
}

static int aq_path_quad_subdivide(AqPath* path, AqPathPoint p0, AqPathPoint p1,
                                  AqPathPoint p2, uint32_t depth) {
    AqPathPoint p01;
    AqPathPoint p12;
    AqPathPoint p012;
    if (aq_path_curve_flat(p0, p1, p2)) return aq_path_line_to(path, p2.x_q2, p2.y_q2);
    if (depth >= AQ_PATH_MAX_DEPTH) {
        aq_path_fail(path);
        return -1;
    }
    p01 = aq_path_midpoint(p0, p1);
    p12 = aq_path_midpoint(p1, p2);
    p012 = aq_path_midpoint(p01, p12);
    if (aq_path_quad_subdivide(path, p0, p01, p012, depth + 1) != 0) return -1;
    return aq_path_quad_subdivide(path, p012, p12, p2, depth + 1);
}

int aq_path_quad_to(AqPath* path, int32_t control_x_q2, int32_t control_y_q2,
                    int32_t x_q2, int32_t y_q2) {
    AqPathPoint p0;
    AqPathPoint p1;
    AqPathPoint p2;
    if (!path || path->failed || !path->has_current ||
        !aq_path_point_valid(control_x_q2, control_y_q2) ||
        !aq_path_point_valid(x_q2, y_q2)) {
        aq_path_fail(path);
        return -1;
    }
    p0.x_q2 = path->current_x_q2; p0.y_q2 = path->current_y_q2;
    p1.x_q2 = control_x_q2; p1.y_q2 = control_y_q2;
    p2.x_q2 = x_q2; p2.y_q2 = y_q2;
    return aq_path_quad_subdivide(path, p0, p1, p2, 0);
}

static int aq_path_cubic_flat(AqPathPoint p0, AqPathPoint p1,
                              AqPathPoint p2, AqPathPoint p3) {
    return aq_path_curve_flat(p0, p1, p3) && aq_path_curve_flat(p0, p2, p3);
}

static int aq_path_cubic_subdivide(AqPath* path, AqPathPoint p0, AqPathPoint p1,
                                   AqPathPoint p2, AqPathPoint p3, uint32_t depth) {
    AqPathPoint p01;
    AqPathPoint p12;
    AqPathPoint p23;
    AqPathPoint p012;
    AqPathPoint p123;
    AqPathPoint p0123;
    if (aq_path_cubic_flat(p0, p1, p2, p3)) return aq_path_line_to(path, p3.x_q2, p3.y_q2);
    if (depth >= AQ_PATH_MAX_DEPTH) {
        aq_path_fail(path);
        return -1;
    }
    p01 = aq_path_midpoint(p0, p1);
    p12 = aq_path_midpoint(p1, p2);
    p23 = aq_path_midpoint(p2, p3);
    p012 = aq_path_midpoint(p01, p12);
    p123 = aq_path_midpoint(p12, p23);
    p0123 = aq_path_midpoint(p012, p123);
    if (aq_path_cubic_subdivide(path, p0, p01, p012, p0123, depth + 1) != 0) return -1;
    return aq_path_cubic_subdivide(path, p0123, p123, p23, p3, depth + 1);
}

int aq_path_cubic_to(AqPath* path, int32_t control1_x_q2, int32_t control1_y_q2,
                     int32_t control2_x_q2, int32_t control2_y_q2,
                     int32_t x_q2, int32_t y_q2) {
    AqPathPoint p0;
    AqPathPoint p1;
    AqPathPoint p2;
    AqPathPoint p3;
    if (!path || path->failed || !path->has_current ||
        !aq_path_point_valid(control1_x_q2, control1_y_q2) ||
        !aq_path_point_valid(control2_x_q2, control2_y_q2) ||
        !aq_path_point_valid(x_q2, y_q2)) {
        aq_path_fail(path);
        return -1;
    }
    p0.x_q2 = path->current_x_q2; p0.y_q2 = path->current_y_q2;
    p1.x_q2 = control1_x_q2; p1.y_q2 = control1_y_q2;
    p2.x_q2 = control2_x_q2; p2.y_q2 = control2_y_q2;
    p3.x_q2 = x_q2; p3.y_q2 = y_q2;
    return aq_path_cubic_subdivide(path, p0, p1, p2, p3, 0);
}

static uint64_t aq_path_isqrt_u64(uint64_t value) {
    uint64_t bit = 1ULL << 62;
    uint64_t result = 0;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static int64_t aq_path_div_q20(int64_t numerator, int64_t denominator) {
    if (numerator < 0 || denominator <= 0) return -1;
    if (numerator > (INT64_MAX >> 20)) return INT64_MAX;
    return (numerator << 20) / denominator;
}

static int64_t aq_path_signed_div_q20(int64_t numerator, int64_t denominator) {
    int64_t magnitude;
    if (denominator <= 0 || numerator == INT64_MIN) return INT64_MIN;
    magnitude = aq_path_abs64(numerator);
    if (magnitude > (INT64_MAX >> 20)) return numerator < 0 ? INT64_MIN : INT64_MAX;
    magnitude = (magnitude << 20) / denominator;
    return numerator < 0 ? -magnitude : magnitude;
}

static int64_t aq_path_mul_q30(int64_t a, int64_t b) {
    return (a * b) / AQ_Q30_ONE;
}

static int aq_path_mul_div_q10(int64_t a, int64_t b, int64_t* result) {
    int64_t magnitude_a;
    int64_t magnitude_b;
    int negative;
    if (!result || a == INT64_MIN || b == INT64_MIN) return -1;
    magnitude_a = aq_path_abs64(a);
    magnitude_b = aq_path_abs64(b);
    if (magnitude_a != 0 && magnitude_b > INT64_MAX / magnitude_a) return -1;
    negative = (a < 0) != (b < 0);
    *result = (magnitude_a * magnitude_b) / 1024;
    if (negative) *result = -*result;
    return 0;
}

static void aq_path_sin_cos_q30(int64_t angle_q30, int64_t* cosine, int64_t* sine) {
    int64_t x = 652032874LL; /* CORDIC gain reciprocal in Q30. */
    int64_t y = 0;
    int64_t z;
    int32_t cos_sign = 1;
    uint32_t index;
    while (angle_q30 > AQ_Q30_PI) angle_q30 -= AQ_Q30_TWO_PI;
    while (angle_q30 < -AQ_Q30_PI) angle_q30 += AQ_Q30_TWO_PI;
    if (angle_q30 > AQ_Q30_HALF_PI) {
        angle_q30 = AQ_Q30_PI - angle_q30;
        cos_sign = -1;
    } else if (angle_q30 < -AQ_Q30_HALF_PI) {
        angle_q30 = -AQ_Q30_PI - angle_q30;
        cos_sign = -1;
    }
    z = angle_q30;
    for (index = 0; index < 31; ++index) {
        int64_t old_x = x;
        if (z >= 0) {
            x -= y / (1LL << index);
            y += old_x / (1LL << index);
            z -= g_aq_cordic_atan_q30[index];
        } else {
            x += y / (1LL << index);
            y -= old_x / (1LL << index);
            z += g_aq_cordic_atan_q30[index];
        }
    }
    *cosine = cos_sign < 0 ? -x : x;
    *sine = y;
}

static int64_t aq_path_atan2_q30(int64_t y, int64_t x) {
    int64_t angle = 0;
    uint32_t index;
    if (x == 0 && y == 0) return 0;
    if (x < 0) {
        angle = y >= 0 ? AQ_Q30_PI : -AQ_Q30_PI;
        x = -x;
        y = -y;
    }
    for (index = 0; index < 31; ++index) {
        int64_t old_x = x;
        if (y > 0) {
            x += y / (1LL << index);
            y -= old_x / (1LL << index);
            angle += g_aq_cordic_atan_q30[index];
        } else {
            x -= y / (1LL << index);
            y += old_x / (1LL << index);
            angle -= g_aq_cordic_atan_q30[index];
        }
    }
    return angle;
}

static int aq_path_arc_point(int64_t center_x_q2, int64_t center_y_q2,
                             int64_t radius_x_q2, int64_t radius_y_q2,
                             int64_t cos_rotation, int64_t sin_rotation,
                             int64_t angle, AqPathPoint* point,
                             AqPathPoint* derivative) {
    int64_t cosine;
    int64_t sine;
    int64_t x;
    int64_t y;
    aq_path_sin_cos_q30(angle, &cosine, &sine);
    x = center_x_q2 + aq_path_mul_q30(radius_x_q2, aq_path_mul_q30(cosine, cos_rotation)) -
        aq_path_mul_q30(radius_y_q2, aq_path_mul_q30(sine, sin_rotation));
    y = center_y_q2 + aq_path_mul_q30(radius_x_q2, aq_path_mul_q30(cosine, sin_rotation)) +
        aq_path_mul_q30(radius_y_q2, aq_path_mul_q30(sine, cos_rotation));
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
        !aq_path_point_valid((int32_t)x, (int32_t)y)) return -1;
    point->x_q2 = (int32_t)x;
    point->y_q2 = (int32_t)y;
    if (derivative) {
        x = -aq_path_mul_q30(radius_x_q2, aq_path_mul_q30(sine, cos_rotation)) -
            aq_path_mul_q30(radius_y_q2, aq_path_mul_q30(cosine, sin_rotation));
        y = -aq_path_mul_q30(radius_x_q2, aq_path_mul_q30(sine, sin_rotation)) +
            aq_path_mul_q30(radius_y_q2, aq_path_mul_q30(cosine, cos_rotation));
        if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) return -1;
        derivative->x_q2 = (int32_t)x;
        derivative->y_q2 = (int32_t)y;
    }
    return 0;
}

int aq_path_svg_arc_to(AqPath* path, int32_t radius_x_q2, int32_t radius_y_q2,
                       int32_t rotation_degrees, int large_arc, int sweep,
                       int32_t x_q2, int32_t y_q2) {
    int64_t rx;
    int64_t ry;
    int64_t dx;
    int64_t dy;
    int64_t cos_rotation;
    int64_t sin_rotation;
    int64_t x_prime;
    int64_t y_prime;
    int64_t lambda;
    int64_t factor;
    int64_t center_x_prime;
    int64_t center_y_prime;
    int64_t center_x;
    int64_t center_y;
    int64_t u_x;
    int64_t u_y;
    int64_t v_x;
    int64_t v_y;
    int64_t start_angle;
    int64_t delta_angle;
    uint32_t segments;
    uint32_t segment;

    if (!path || path->failed || !path->has_current || !aq_path_point_valid(x_q2, y_q2) ||
        (large_arc != 0 && large_arc != 1) || (sweep != 0 && sweep != 1) ||
        rotation_degrees < -360 || rotation_degrees > 360) {
        aq_path_fail(path);
        return -1;
    }
    if (radius_x_q2 == INT32_MIN || radius_y_q2 == INT32_MIN) {
        aq_path_fail(path);
        return -1;
    }
    rx = aq_path_abs64(radius_x_q2);
    ry = aq_path_abs64(radius_y_q2);
    if (rx == 0 || ry == 0) return aq_path_line_to(path, x_q2, y_q2);
    if (path->current_x_q2 == x_q2 && path->current_y_q2 == y_q2) return 0;

    aq_path_sin_cos_q30(((int64_t)rotation_degrees * AQ_Q30_PI) / 180,
                        &cos_rotation, &sin_rotation);
    dx = ((int64_t)path->current_x_q2 - x_q2) / 2;
    dy = ((int64_t)path->current_y_q2 - y_q2) / 2;
    x_prime = aq_path_mul_q30(cos_rotation, dx) + aq_path_mul_q30(sin_rotation, dy);
    y_prime = -aq_path_mul_q30(sin_rotation, dx) + aq_path_mul_q30(cos_rotation, dy);
    lambda = aq_path_div_q20(x_prime * x_prime, rx * rx);
    if (lambda < 0 || lambda == INT64_MAX) {
        aq_path_fail(path);
        return -1;
    }
    {
        int64_t y_ratio = aq_path_div_q20(y_prime * y_prime, ry * ry);
        if (y_ratio < 0 || y_ratio == INT64_MAX || lambda > INT64_MAX - y_ratio) {
            aq_path_fail(path);
            return -1;
        }
        lambda += y_ratio;
    }
    if (lambda > (1LL << 20)) {
        int64_t scale_q10 = (int64_t)aq_path_isqrt_u64((uint64_t)lambda);
        /* SVG radii correction must round outwards: rounding to nearest can
         * leave lambda just above one and incorrectly reject a valid arc. */
        rx = (rx * scale_q10 + 1023) / 1024;
        ry = (ry * scale_q10 + 1023) / 1024;
        if (rx == 0 || ry == 0 || rx > AQ_PATH_COORDINATE_LIMIT * 4 ||
            ry > AQ_PATH_COORDINATE_LIMIT * 4) {
            aq_path_fail(path);
            return -1;
        }
        lambda = aq_path_div_q20(x_prime * x_prime, rx * rx) +
                 aq_path_div_q20(y_prime * y_prime, ry * ry);
        if (lambda < 0 || lambda > (1LL << 20)) {
            aq_path_fail(path);
            return -1;
        }
    }
    if (lambda == 0) {
        aq_path_fail(path);
        return -1;
    }
    factor = aq_path_div_q20((1LL << 20) - lambda, lambda);
    if (factor < 0 || factor == INT64_MAX) {
        aq_path_fail(path);
        return -1;
    }
    factor = (int64_t)aq_path_isqrt_u64((uint64_t)factor); /* Q10 */
    if ((large_arc == sweep) != 0) factor = -factor;
    {
        int64_t base_x = (rx * y_prime) / ry;
        int64_t base_y = (-ry * x_prime) / rx;
        if (aq_path_mul_div_q10(base_x, factor, &center_x_prime) != 0 ||
            aq_path_mul_div_q10(base_y, factor, &center_y_prime) != 0) {
            aq_path_fail(path);
            return -1;
        }
    }
    center_x = aq_path_mul_q30(cos_rotation, center_x_prime) -
               aq_path_mul_q30(sin_rotation, center_y_prime) +
               ((int64_t)path->current_x_q2 + x_q2) / 2;
    center_y = aq_path_mul_q30(sin_rotation, center_x_prime) +
               aq_path_mul_q30(cos_rotation, center_y_prime) +
               ((int64_t)path->current_y_q2 + y_q2) / 2;
    if (center_x < INT32_MIN || center_x > INT32_MAX || center_y < INT32_MIN || center_y > INT32_MAX) {
        aq_path_fail(path);
        return -1;
    }
    u_x = aq_path_signed_div_q20(x_prime - center_x_prime, rx);
    u_y = aq_path_signed_div_q20(y_prime - center_y_prime, ry);
    v_x = aq_path_signed_div_q20(-x_prime - center_x_prime, rx);
    v_y = aq_path_signed_div_q20(-y_prime - center_y_prime, ry);
    if (u_x == INT64_MIN || u_y == INT64_MIN || v_x == INT64_MIN || v_y == INT64_MIN) {
        aq_path_fail(path);
        return -1;
    }
    start_angle = aq_path_atan2_q30(u_y, u_x);
    delta_angle = aq_path_atan2_q30(v_y, v_x) - start_angle;
    while (delta_angle > AQ_Q30_PI) delta_angle -= AQ_Q30_TWO_PI;
    while (delta_angle <= -AQ_Q30_PI) delta_angle += AQ_Q30_TWO_PI;
    if (sweep && delta_angle < 0) delta_angle += AQ_Q30_TWO_PI;
    if (!sweep && delta_angle > 0) delta_angle -= AQ_Q30_TWO_PI;
    segments = (uint32_t)((aq_path_abs64(delta_angle) + AQ_Q30_HALF_PI - 1) / AQ_Q30_HALF_PI);
    if (segments == 0 || segments > 4) {
        aq_path_fail(path);
        return -1;
    }
    for (segment = 0; segment < segments; ++segment) {
        int64_t a0 = start_angle + (delta_angle * (int64_t)segment) / segments;
        int64_t a1 = start_angle + (delta_angle * (int64_t)(segment + 1)) / segments;
        int64_t half_cos;
        int64_t half_sin;
        int64_t alpha_q30;
        AqPathPoint p0;
        AqPathPoint d0;
        AqPathPoint p3;
        AqPathPoint d1;
        AqPathPoint p1;
        AqPathPoint p2;
        aq_path_sin_cos_q30((a1 - a0) / 4, &half_cos, &half_sin);
        if (half_cos == 0) {
            aq_path_fail(path);
            return -1;
        }
        alpha_q30 = ((4LL * half_sin) / 3 * AQ_Q30_ONE) / half_cos;
        if (aq_path_arc_point(center_x, center_y, rx, ry, cos_rotation, sin_rotation,
                              a0, &p0, &d0) != 0 ||
            aq_path_arc_point(center_x, center_y, rx, ry, cos_rotation, sin_rotation,
                              a1, &p3, &d1) != 0) {
            aq_path_fail(path);
            return -1;
        }
        p1.x_q2 = (int32_t)(p0.x_q2 + aq_path_mul_q30(d0.x_q2, alpha_q30));
        p1.y_q2 = (int32_t)(p0.y_q2 + aq_path_mul_q30(d0.y_q2, alpha_q30));
        p2.x_q2 = (int32_t)(p3.x_q2 - aq_path_mul_q30(d1.x_q2, alpha_q30));
        p2.y_q2 = (int32_t)(p3.y_q2 - aq_path_mul_q30(d1.y_q2, alpha_q30));
        if (!aq_path_point_valid(p1.x_q2, p1.y_q2) || !aq_path_point_valid(p2.x_q2, p2.y_q2)) {
            aq_path_fail(path);
            return -1;
        }
        if (segment + 1 == segments) {
            p3.x_q2 = x_q2;
            p3.y_q2 = y_q2;
        }
        if (aq_path_cubic_to(path, p1.x_q2, p1.y_q2, p2.x_q2, p2.y_q2,
                             p3.x_q2, p3.y_q2) != 0) return -1;
    }
    return 0;
}

int aq_path_append_psf_glyph(AqPath* path, const AqFont* font,
                             uint32_t codepoint, int32_t x_q2, int32_t y_q2) {
    int32_t glyph_index;
    const uint8_t* glyph;
    int32_t bytes_per_row;
    int32_t row;
    int64_t required_bytes;
    int64_t right_q2;
    int64_t bottom_q2;
    if (!path || path->failed || !font || !aq_path_point_valid(x_q2, y_q2) ||
        font->glyph_w <= 0 || font->glyph_h <= 0 || font->bytes_per_glyph <= 0) {
        aq_path_fail(path);
        return -1;
    }
    /* Validate the complete glyph extent and row-stride product before a
     * single Q2 addition.  A malformed external PSF descriptor must not turn
     * into signed-overflow before the normal path fail-close boundary. */
    if (font->glyph_w > INT32_MAX - 7) {
        aq_path_fail(path);
        return -1;
    }
    bytes_per_row = (font->glyph_w + 7) / 8;
    required_bytes = (int64_t)bytes_per_row * font->glyph_h;
    right_q2 = (int64_t)x_q2 + (int64_t)font->glyph_w * 4;
    bottom_q2 = (int64_t)y_q2 + (int64_t)font->glyph_h * 4;
    if (required_bytes > font->bytes_per_glyph ||
        right_q2 < -AQ_PATH_COORDINATE_LIMIT * 4 ||
        right_q2 > AQ_PATH_COORDINATE_LIMIT * 4 ||
        bottom_q2 < -AQ_PATH_COORDINATE_LIMIT * 4 ||
        bottom_q2 > AQ_PATH_COORDINATE_LIMIT * 4) {
        aq_path_fail(path);
        return -1;
    }
    glyph_index = aq_font_lookup_glyph(font, codepoint);
    glyph = aq_font_glyph_data(font, glyph_index);
    if (!glyph || bytes_per_row <= 0) {
        aq_path_fail(path);
        return -1;
    }
    for (row = 0; row < font->glyph_h; ++row) {
        int32_t col = 0;
        while (col < font->glyph_w) {
            int32_t run_start;
            int32_t run_end;
            while (col < font->glyph_w &&
                   (glyph[row * bytes_per_row + col / 8] & (1u << (7 - (col & 7)))) == 0) col++;
            if (col == font->glyph_w) break;
            run_start = col;
            while (col < font->glyph_w &&
                   (glyph[row * bytes_per_row + col / 8] & (1u << (7 - (col & 7)))) != 0) col++;
            run_end = col;
            if (aq_path_move_to(path, x_q2 + run_start * 4, y_q2 + row * 4) != 0 ||
                aq_path_line_to(path, x_q2 + run_end * 4, y_q2 + row * 4) != 0 ||
                aq_path_line_to(path, x_q2 + run_end * 4, y_q2 + (row + 1) * 4) != 0 ||
                aq_path_line_to(path, x_q2 + run_start * 4, y_q2 + (row + 1) * 4) != 0 ||
                aq_path_close(path) != 0) return -1;
        }
    }
    return 0;
}

void aq_path_stroke(AqSurface* surface, const AqPath* path, AqColor color) {
    uint32_t contour_index;
    if (!surface || !surface->pixels || !path || path->failed || color.a == 0 ||
        !path->points || !path->contours || path->point_count > path->point_capacity ||
        path->contour_count > path->contour_capacity || path->point_count > AQ_PATH_MAX_POINTS ||
        path->contour_count > AQ_PATH_MAX_CONTOURS) return;
    /* Validate before touching the surface so a corrupted path cannot produce
     * a partially rendered result. */
    for (contour_index = 0; contour_index < path->contour_count; ++contour_index) {
        const AqPathContour* contour = &path->contours[contour_index];
        uint32_t point_index;
        if (contour->point_count == 0 || contour->first_point > path->point_count ||
            contour->point_count > path->point_count - contour->first_point) return;
        for (point_index = 0; point_index < contour->point_count; ++point_index) {
            const AqPathPoint* point = &path->points[contour->first_point + point_index];
            if (!aq_path_point_valid(point->x_q2, point->y_q2)) return;
        }
    }
    for (contour_index = 0; contour_index < path->contour_count; ++contour_index) {
        const AqPathContour* contour = &path->contours[contour_index];
        uint32_t point_index;
        for (point_index = 1; point_index < contour->point_count; ++point_index) {
            const AqPathPoint* from = &path->points[contour->first_point + point_index - 1];
            const AqPathPoint* to = &path->points[contour->first_point + point_index];
            aq_line(surface, AQ_PATH_Q2_TO_INT(from->x_q2), AQ_PATH_Q2_TO_INT(from->y_q2),
                    AQ_PATH_Q2_TO_INT(to->x_q2), AQ_PATH_Q2_TO_INT(to->y_q2), color);
        }
        if (contour->closed && contour->point_count > 1) {
            const AqPathPoint* from = &path->points[contour->first_point + contour->point_count - 1];
            const AqPathPoint* to = &path->points[contour->first_point];
            aq_line(surface, AQ_PATH_Q2_TO_INT(from->x_q2), AQ_PATH_Q2_TO_INT(from->y_q2),
                    AQ_PATH_Q2_TO_INT(to->x_q2), AQ_PATH_Q2_TO_INT(to->y_q2), color);
        }
    }
}

typedef struct AqPathFillVisibleBounds {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} AqPathFillVisibleBounds;

static int64_t aq_path_floor_div4(int64_t value) {
    return value >= 0 ? value / 4 : -((-value + 3) / 4);
}

static int64_t aq_path_ceil_div4(int64_t value) {
    return -aq_path_floor_div4(-value);
}

static int aq_path_fill_surface_valid(const AqSurface* surface) {
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

static int aq_path_fill_visible_bounds(
    const AqSurface* surface, int64_t min_x_q2, int64_t min_y_q2,
    int64_t max_x_q2, int64_t max_y_q2, AqPathFillVisibleBounds* visible) {
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t clip_right;
    int64_t clip_bottom;
    if (!visible || !aq_path_fill_surface_valid(surface) ||
        min_x_q2 < -(int64_t)AQ_PATH_COORDINATE_LIMIT * 4 ||
        min_y_q2 < -(int64_t)AQ_PATH_COORDINATE_LIMIT * 4 ||
        max_x_q2 > (int64_t)AQ_PATH_COORDINATE_LIMIT * 4 ||
        max_y_q2 > (int64_t)AQ_PATH_COORDINATE_LIMIT * 4 ||
        min_x_q2 > max_x_q2 || min_y_q2 > max_y_q2)
        return 0;
    left = aq_path_floor_div4(min_x_q2);
    top = aq_path_floor_div4(min_y_q2);
    right = aq_path_ceil_div4(max_x_q2);
    bottom = aq_path_ceil_div4(max_y_q2);
    clip_right = (int64_t)surface->clip.x + surface->clip.w - 1;
    clip_bottom = (int64_t)surface->clip.y + surface->clip.h - 1;
    if (left < surface->clip.x) left = surface->clip.x;
    if (top < surface->clip.y) top = surface->clip.y;
    if (right > clip_right) right = clip_right;
    if (bottom > clip_bottom) bottom = clip_bottom;
    if (left > right || top > bottom) return 0;
    visible->left = (int32_t)left;
    visible->top = (int32_t)top;
    visible->right = (int32_t)right;
    visible->bottom = (int32_t)bottom;
    return 1;
}

static int aq_path_fill_validate(const AqPath* path,
                                 uint32_t intersection_capacity,
                                 int64_t* min_x_q2_out,
                                 int64_t* min_y_q2_out,
                                 int64_t* max_x_q2_out,
                                 int64_t* max_y_q2_out,
                                 uint32_t* edge_count_out) {
    uint32_t contour_index;
    uint32_t edge_count = 0u;
    int64_t min_x_q2 = 0;
    int64_t min_y_q2 = 0;
    int64_t max_x_q2 = 0;
    int64_t max_y_q2 = 0;
    int have_point = 0;
    if (!path || path->failed || !path->points || !path->contours ||
        !min_x_q2_out || !min_y_q2_out || !max_x_q2_out ||
        !max_y_q2_out || !edge_count_out ||
        path->point_count > path->point_capacity ||
        path->contour_count > path->contour_capacity ||
        path->point_count > AQ_PATH_MAX_POINTS ||
        path->contour_count > AQ_PATH_MAX_CONTOURS)
        return 0;
    for (contour_index = 0u; contour_index < path->contour_count;
         ++contour_index) {
        const AqPathContour* contour = &path->contours[contour_index];
        uint32_t point_index;
        if (!contour->closed || contour->point_count < 3u ||
            contour->first_point > path->point_count ||
            contour->point_count > path->point_count - contour->first_point ||
            contour->point_count > AQ_PATH_FILL_MAX_EDGES - edge_count)
            return 0;
        edge_count += contour->point_count;
        for (point_index = 0u; point_index < contour->point_count;
             ++point_index) {
            const AqPathPoint* point =
                &path->points[contour->first_point + point_index];
            if (!aq_path_point_valid(point->x_q2, point->y_q2)) return 0;
            if (!have_point) {
                min_x_q2 = max_x_q2 = point->x_q2;
                min_y_q2 = max_y_q2 = point->y_q2;
                have_point = 1;
            } else {
                if (point->x_q2 < min_x_q2) min_x_q2 = point->x_q2;
                if (point->x_q2 > max_x_q2) max_x_q2 = point->x_q2;
                if (point->y_q2 < min_y_q2) min_y_q2 = point->y_q2;
                if (point->y_q2 > max_y_q2) max_y_q2 = point->y_q2;
            }
        }
    }
    if (!have_point || edge_count == 0u || edge_count > intersection_capacity)
        return 0;
    *min_x_q2_out = min_x_q2;
    *min_y_q2_out = min_y_q2;
    *max_x_q2_out = max_x_q2;
    *max_y_q2_out = max_y_q2;
    *edge_count_out = edge_count;
    return 1;
}

static int aq_path_fill_intersections(const AqPath* path, int64_t y_q2,
                                      int32_t* intersections_q2,
                                      uint32_t intersection_capacity,
                                      uint32_t* intersection_count_out) {
    uint32_t contour_index;
    uint32_t count = 0u;
    if (!path || !intersection_count_out) return 0;
    for (contour_index = 0u; contour_index < path->contour_count;
         ++contour_index) {
        const AqPathContour* contour = &path->contours[contour_index];
        uint32_t point_index;
        for (point_index = 0u; point_index < contour->point_count;
             ++point_index) {
            const AqPathPoint* first =
                &path->points[contour->first_point + point_index];
            const AqPathPoint* second = &path->points[
                contour->first_point +
                (point_index + 1u == contour->point_count ? 0u :
                                                            point_index + 1u)];
            int64_t first_y = first->y_q2;
            int64_t second_y = second->y_q2;
            if ((first_y <= y_q2 && second_y > y_q2) ||
                (second_y <= y_q2 && first_y > y_q2)) {
                int64_t numerator = (y_q2 - first_y) *
                    ((int64_t)second->x_q2 - first->x_q2);
                int64_t x_q2 = (int64_t)first->x_q2 + numerator /
                    (second_y - first_y);
                if (x_q2 < INT32_MIN || x_q2 > INT32_MAX ||
                    count >= intersection_capacity)
                    return 0;
                if (intersections_q2)
                    intersections_q2[count] = (int32_t)x_q2;
                ++count;
            }
        }
    }
    *intersection_count_out = count;
    return (count & 1u) == 0u;
}

static void aq_path_fill_sort_intersections(int32_t* intersections_q2,
                                            uint32_t count) {
    uint32_t index;
    for (index = 1u; index < count; ++index) {
        int32_t value = intersections_q2[index];
        uint32_t cursor = index;
        while (cursor != 0u && intersections_q2[cursor - 1u] > value) {
            intersections_q2[cursor] = intersections_q2[cursor - 1u];
            --cursor;
        }
        intersections_q2[cursor] = value;
    }
}

void aq_path_fill_even_odd(AqSurface* surface, const AqPath* path,
                           int32_t* intersections_q2,
                           uint32_t intersection_capacity, AqColor color) {
    AqPathFillVisibleBounds visible;
    int64_t min_x_q2;
    int64_t min_y_q2;
    int64_t max_x_q2;
    int64_t max_y_q2;
    uint32_t edge_count;
    uint64_t width;
    uint64_t height;
    uint64_t area;
    uint64_t per_row_work;
    int32_t row;
    if (!surface || !path || !intersections_q2 || color.a == 0 ||
        !aq_path_fill_validate(path, intersection_capacity, &min_x_q2,
                               &min_y_q2, &max_x_q2, &max_y_q2,
                               &edge_count) ||
        !aq_path_fill_visible_bounds(surface, min_x_q2, min_y_q2,
                                     max_x_q2, max_y_q2, &visible))
        return;
    width = (uint64_t)((int64_t)visible.right - visible.left + 1);
    height = (uint64_t)((int64_t)visible.bottom - visible.top + 1);
    if (width == 0u || height == 0u ||
        width > AQ_PATH_FILL_PIXEL_BUDGET / height)
        return;
    area = width * height;
    /* One pass proves every scanline's crossing parity before any target
     * write.  The rendering pass then collects/sorts and emits spans. */
    per_row_work = (uint64_t)edge_count * edge_count +
                   (uint64_t)edge_count * 2u;
    if (per_row_work >
        (AQ_PATH_FILL_PIXEL_BUDGET - area) / height)
        return;
    for (row = visible.top; row <= visible.bottom; ++row) {
        uint32_t count;
        if (!aq_path_fill_intersections(path, (int64_t)row * 4 + 2,
                                        NULL, edge_count, &count))
            return;
    }
    for (row = visible.top; row <= visible.bottom; ++row) {
        uint32_t count;
        uint32_t index;
        if (!aq_path_fill_intersections(path, (int64_t)row * 4 + 2,
                                        intersections_q2, edge_count, &count))
            return;
        aq_path_fill_sort_intersections(intersections_q2, count);
        for (index = 0u; index < count; index += 2u) {
            int64_t first = aq_path_ceil_div4(
                (int64_t)intersections_q2[index] - 2);
            int64_t last = aq_path_floor_div4(
                (int64_t)intersections_q2[index + 1u] - 3);
            int32_t left;
            int32_t right;
            if (first < visible.left) first = visible.left;
            if (last > visible.right) last = visible.right;
            if (first > last) continue;
            left = (int32_t)first;
            right = (int32_t)last;
            aq_hline(surface, left, row, right - left + 1, color);
        }
    }
}
