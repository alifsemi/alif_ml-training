/* ILI9806E MIPI DSI display, driven by the CDC200 display controller
 *
 * Simplified from the Alif viewfinder demo:
 * https://github.com/alifsemi/alif_M55-viewfinder
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#include "RTE_Device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panel resolution, configured in RTE_Device.h */
#define DISPLAY_WIDTH   RTE_PANEL_HACTIVE_TIME
#define DISPLAY_HEIGHT  RTE_PANEL_VACTIVE_LINE

#if RTE_CDC200_PIXEL_FORMAT != 2
#error "display.c expects RGB565 frame buffers (RTE_CDC200_PIXEL_FORMAT 2)"
#endif

/* RGB565 color from 8-bit R, G, B */
#define DISPLAY_RGB565(r, g, b)  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define DISPLAY_COLOR_BLACK      DISPLAY_RGB565(0, 0, 0)
#define DISPLAY_COLOR_GREEN      DISPLAY_RGB565(0, 255, 0)

/**
 * @brief Power up and start the display (SE power, CDC200, MIPI DSI, ILI9806E panel).
 *        Call before tflm_init(), see power_enable().
 * @return 0 on success, negative on failure
 */
int display_init(void);

/**
 * @brief Draw an RGB888 packed image to the back buffer, scaled up by an
 *        integer factor (nearest neighbor: each pixel becomes scale x scale pixels).
 * @param rgb    RGB888 packed image (R, G, B per pixel)
 * @param width  image width
 * @param height image height
 * @param x, y   top left corner on the display
 * @param scale  integer scale factor (1, 2, ...)
 */
void display_draw_image(const uint8_t *rgb, int width, int height, int x, int y, int scale);

/**
 * @brief Draw a rectangle outline to the back buffer. Clipped to the display.
 * @param x, y       top left corner on the display
 * @param w, h       size in pixels
 * @param thickness  line width in pixels
 * @param color      RGB565 color, e.g. DISPLAY_COLOR_GREEN
 */
void display_draw_box(int x, int y, int w, int h, int thickness, uint16_t color);

/**
 * @brief Show the back buffer: write it from the CPU cache to memory and
 *        switch the CDC200 to it on the next vertical blanking. The other
 *        buffer becomes the new back buffer.
 */
void display_update(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
