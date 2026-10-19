/* ILI9806E MIPI DSI display, driven by the CDC200 display controller
 *
 * Data path:
 *   frame buffer in SRAM (RGB565) -> CDC200 (reads with DMA) -> MIPI DSI -> ILI9806E panel
 *
 * Two frame buffers: the CPU draws into the back buffer while the CDC200
 * shows the front buffer, so the image does not tear or flicker.
 */
#include <stdio.h>
#include <string.h>

#include "RTE_Components.h"
#include CMSIS_device_header

#include "Driver_CDC200.h"
#include "services_lib_api.h"

#include "display.h"
#include "power.h"

/* The CDC200 reads the frame buffers with DMA, so they must be in SRAM, not in the CPU local TCM */
static uint16_t frame_buffer[2][DISPLAY_HEIGHT][DISPLAY_WIDTH]
    __attribute__((aligned(32), section(".bss.lcd_frame_buf")));
static int back_buffer = 1;

extern ARM_DRIVER_CDC200 Driver_CDC200;
static ARM_DRIVER_CDC200 *const cdc = &Driver_CDC200;

static volatile uint32_t dsi_errors;

static void display_callback(uint32_t event)
{
  if (event & ARM_CDC_DSI_ERROR_EVENT) {
    dsi_errors++;
  }
}

int display_init(void)
{
  int ret;

  /* CDC200 and MIPI DSI clocks, MIPI transmitter PHY power */
  if (power_enable(CDC200_MASK | MIPI_DSI_MASK,
                   LDO_PHY_MASK | MIPI_TX_DPHY_MASK | MIPI_PLL_DPHY_MASK) != 0) {
    return -1;
  }

  /* SRAM is not zero initialized at startup: start from a black screen */
  memset(frame_buffer, 0, sizeof(frame_buffer));
  RTSS_CleanDCache_by_Addr(frame_buffer, sizeof(frame_buffer));

  ret = cdc->Initialize(display_callback);
  if (ret != ARM_DRIVER_OK) {
    printf("  [DISP] ERROR: Initialize failed (%d)\n", ret);
    return ret;
  }

  /* Also initializes MIPI DSI and the ILI9806E panel (reset, backlight, panel registers) */
  ret = cdc->PowerControl(ARM_POWER_FULL);
  if (ret != ARM_DRIVER_OK) {
    printf("  [DISP] ERROR: PowerControl failed (%d)\n", ret);
    return ret;
  }

  ret = cdc->Control(CDC200_CONFIGURE_DISPLAY, (uint32_t)frame_buffer[1 - back_buffer]);
  if (ret != ARM_DRIVER_OK) {
    printf("  [DISP] ERROR: Display configuration failed (%d)\n", ret);
    return ret;
  }

  ret = cdc->Start();
  if (ret != ARM_DRIVER_OK) {
    printf("  [DISP] ERROR: Start failed (%d)\n", ret);
    return ret;
  }

  printf("  [DISP] ILI9806E %dx%d RGB565\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
  return 0;
}

void display_draw_image(const uint8_t *rgb, int width, int height, int x, int y, int scale)
{
  uint16_t (*fb)[DISPLAY_WIDTH] = frame_buffer[back_buffer];

  for (int row = 0; row < height; row++) {
    /* Convert one image row to RGB565 and repeat each pixel scale times horizontally */
    uint16_t line[width * scale];
    const uint8_t *src = &rgb[row * width * 3];
    for (int col = 0; col < width; col++) {
      uint16_t pixel = DISPLAY_RGB565(src[0], src[1], src[2]);
      src += 3;
      for (int s = 0; s < scale; s++) {
        line[col * scale + s] = pixel;
      }
    }

    /* Repeat the row scale times vertically, clipped to the display */
    int line_x = x < 0 ? 0 : x;
    int line_end = x + width * scale > DISPLAY_WIDTH ? DISPLAY_WIDTH : x + width * scale;
    if (line_end <= line_x) {
      return;
    }
    for (int s = 0; s < scale; s++) {
      int fb_y = y + row * scale + s;
      if (fb_y >= 0 && fb_y < DISPLAY_HEIGHT) {
        memcpy(&fb[fb_y][line_x], &line[line_x - x], (line_end - line_x) * sizeof(uint16_t));
      }
    }
  }
}

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
  uint16_t (*fb)[DISPLAY_WIDTH] = frame_buffer[back_buffer];

  /* Clip to the display; x1, y1 are exclusive */
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > DISPLAY_WIDTH) x1 = DISPLAY_WIDTH;
  if (y1 > DISPLAY_HEIGHT) y1 = DISPLAY_HEIGHT;

  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      fb[y][x] = color;
    }
  }
}

void display_draw_box(int x, int y, int w, int h, int thickness, uint16_t color)
{
  fill_rect(x, y, x + w, y + thickness, color);                  /* top */
  fill_rect(x, y + h - thickness, x + w, y + h, color);          /* bottom */
  fill_rect(x, y, x + thickness, y + h, color);                  /* left */
  fill_rect(x + w - thickness, y, x + w, y + h, color);          /* right */
}

void display_update(void)
{
  /* The CDC200 reads the frame buffer with DMA: write the CPU cache content to memory first */
  RTSS_CleanDCache_by_Addr(frame_buffer[back_buffer], sizeof(frame_buffer[0]));

  cdc->Control(CDC200_FRAMEBUF_UPDATE_VSYNC, (uint32_t)frame_buffer[back_buffer]);
  back_buffer = 1 - back_buffer;
}
