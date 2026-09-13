/*
 * Aquamarine - RinOS 2D/3D Graphics Library
 *
 * A freestanding, surface-based graphics library for kernel
 * and userspace rendering. No libc dependency.
 *
 * Usage:
 *   #include "aquamarine.h"
 *
 *   // Initialize allocator (kernel: kmalloc/kfree)
 *   aq_set_allocator(kmalloc, kfree);
 *
 *   // Wrap existing framebuffer
 *   AqSurface* screen = aq_surface_create_from(
 *       g_backbuffer, g_width, g_height, g_pitch, AQ_FORMAT_BGRA32);
 *
 *   // Draw
 *   aq_fill_rect(screen, 10, 10, 200, 100, AQ_RGB(0x7D, 0xD3, 0xFC));
 *   aq_draw_string(screen, 20, 40, "Hello Aquamarine!", AQ_WHITE, NULL);
 *
 *   // 3D rendering
 *   AqRenderCtx* r3d = aq_render3d_create(screen);
 *   r3d->projection = aq_mat4_perspective(60, aspect, near, far);
 *   aq_render3d_update_mvp(r3d);
 *   aq_render3d_mesh(r3d, cube_verts, 36);
 */

#ifndef AQUAMARINE_H
#define AQUAMARINE_H

/* Core types and utilities */
#include "aq_types.h"
#include "aq_math.h"
#include "aq_clip.h"

/* Surface management */
#include "aq_surface.h"

/* Color utilities */
#include "aq_color.h"

/* Alpha blending and blit */
#include "aq_blend.h"

/* 2D drawing primitives */
#include "aq_draw.h"

/* Bounded adaptive paths */
#include "aq_path.h"

/* Gradient rendering */
#include "aq_gradient.h"

/* Text rendering */
#include "aq_text.h"

/* Surface transforms */
#include "aq_transform.h"

/* 3D math */
#include "aq_math3d.h"

/* 3D software rasterizer */
#include "aq_render3d.h"

#endif /* AQUAMARINE_H */
