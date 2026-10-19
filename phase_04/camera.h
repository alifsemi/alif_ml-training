/* OV5675 camera + ISP capture
 *
 * Simplified from the Alif viewfinder demo:
 * https://github.com/alifsemi/alif_M55-viewfinder
 */
#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

#include "RTE_Device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ISP scaler output, configured in RTE_Device.h (RTE_ISP_OUTPUT_WIDTH/HEIGHT) */
#define CAMERA_WIDTH   RTE_ISP_OUTPUT_WIDTH
#define CAMERA_HEIGHT  RTE_ISP_OUTPUT_HEIGHT

#if RTE_ISP_OUTPUT_FORMAT != 39
#error "camera.c expects RGB888 planar ISP output (RTE_ISP_OUTPUT_FORMAT 39)"
#endif

/**
 * @brief Power up the camera related hardware and initialize the camera.
 *        - SE services: enable MIPI CSI/DPHY power and camera clocks
 *        - CPI + MIPI CSI2 receiver, OV5675 sensor over I2C
 *        - ISP: square crop of the sensor image, scaled to CAMERA_WIDTH x CAMERA_HEIGHT
 *        Requires board_pins_config() and se_services_port_init() to be called first.
 * @return 0 on success, negative on failure
 */
int camera_init(void);

/**
 * @brief Capture one frame. Blocks until the ISP has written the frame to memory.
 * @return RGB888 planar image (R plane, G plane, B plane, each CAMERA_WIDTH x CAMERA_HEIGHT),
 *         or NULL on failure. Valid until the next camera_capture call.
 */
const uint8_t *camera_capture(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_H */
