/*
 * Aquamarine - bounded retained paths
 *
 * Path coordinates use quarter-device-unit (Q2) values.  Storage belongs to
 * the caller so paths are usable in the freestanding kernel and no draw can
 * silently allocate.  Every malformed or capacity-exhausting operation clears
 * the complete path before returning failure.
 */

#ifndef AQ_PATH_H
#define AQ_PATH_H

#include "aq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AQ_PATH_COORDINATE_LIMIT 1048576
#define AQ_PATH_MAX_CONTOURS     16384u
#define AQ_PATH_MAX_POINTS       131072u
#define AQ_PATH_MAX_DEPTH        12u
#define AQ_PATH_FLATNESS_Q2      1

#define AQ_PATH_Q2_FROM_INT(value) ((int32_t)((value) * 4))
#define AQ_PATH_Q2_TO_INT(value)   ((int32_t)((value) / 4))

typedef struct {
    int32_t x_q2;
    int32_t y_q2;
} AqPathPoint;

typedef struct {
    uint32_t first_point;
    uint32_t point_count;
    uint8_t closed;
    uint8_t reserved[3];
} AqPathContour;

typedef struct {
    AqPathPoint* points;
    AqPathContour* contours;
    uint32_t point_capacity;
    uint32_t contour_capacity;
    uint32_t point_count;
    uint32_t contour_count;
    uint32_t current_contour;
    uint8_t has_current;
    uint8_t failed;
    uint8_t reserved[2];
    int32_t current_x_q2;
    int32_t current_y_q2;
} AqPath;

/* Initialise caller-provided storage.  Capacities above the hard limits are
 * rejected: accepting a larger buffer would let callers bypass path bounds. */
int aq_path_init(AqPath* path, AqPathPoint* points, uint32_t point_capacity,
                 AqPathContour* contours, uint32_t contour_capacity);
void aq_path_clear(AqPath* path);

int aq_path_move_to(AqPath* path, int32_t x_q2, int32_t y_q2);
int aq_path_line_to(AqPath* path, int32_t x_q2, int32_t y_q2);
int aq_path_close(AqPath* path);
int aq_path_quad_to(AqPath* path, int32_t control_x_q2, int32_t control_y_q2,
                    int32_t x_q2, int32_t y_q2);
int aq_path_cubic_to(AqPath* path, int32_t control1_x_q2, int32_t control1_y_q2,
                     int32_t control2_x_q2, int32_t control2_y_q2,
                     int32_t x_q2, int32_t y_q2);

/* SVG endpoint-arc command. rotation_degrees is deliberately integral because
 * Aquamarine's public transform API is integral; the curve itself is resolved
 * in Q2 using a Q30 CORDIC implementation. */
int aq_path_svg_arc_to(AqPath* path, int32_t radius_x_q2, int32_t radius_y_q2,
                       int32_t rotation_degrees, int large_arc, int sweep,
                       int32_t x_q2, int32_t y_q2);

/* Convert a PSF glyph selected through the font's Unicode lookup into closed
 * contours.  Each continuous set-bit run becomes one rectangle contour. */
int aq_path_append_psf_glyph(AqPath* path, const AqFont* font,
                             uint32_t codepoint, int32_t x_q2, int32_t y_q2);

/* Draw a validated path as connected one-device-pixel strokes. */
void aq_path_stroke(AqSurface* surface, const AqPath* path, AqColor color);

/* Fill closed contours with the even-odd rule.  The intersection array is
 * caller-owned scratch; it must hold every edge in the path.  Malformed paths,
 * invalid surfaces, insufficient scratch, and work-budget failures leave the
 * destination unchanged. */
void aq_path_fill_even_odd(AqSurface* surface, const AqPath* path,
                           int32_t* intersections_q2,
                           uint32_t intersection_capacity, AqColor color);

#ifdef __cplusplus
}
#endif

#endif /* AQ_PATH_H */
