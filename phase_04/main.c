/* Copyright (C) 2026 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */

 /**
  * @file main.c
  * @brief phase_04 of the Alif Semiconductor AI/ML training.
  *        Target is to add camera and display support with minimalistic UI
  *        Steps:
  *          - Add camera driver and ISP driver.
  *          - configure ISP to output model input tensor sized image to save memory and resize processing time
  *          - Add display driver and draw camera image on display with detection bounding boxes
  */
#include "RTE_Components.h"
#include <stdio.h>
#include CMSIS_device_header

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
#include "retarget_stdout.h"
#include <retarget_init.h>
#endif

#include "board_config.h"
#include "se_services_port.h"
#include "aipl_color_conversion.h"

#include "ethosu_init.h"
#include "model.h"
#include "tflm_inference.h"
#include "object_detection.h"
#include "camera.h"
#include "display.h"

#define MAX_DETECTIONS 10

/* Camera image on the display: scaled up by an integer factor and centered */
#define IMAGE_SCALE    (DISPLAY_WIDTH / CAMERA_WIDTH)
#define IMAGE_X        ((DISPLAY_WIDTH - CAMERA_WIDTH * IMAGE_SCALE) / 2)
#define IMAGE_Y        ((DISPLAY_HEIGHT - CAMERA_HEIGHT * IMAGE_SCALE) / 2)
#define BOX_THICKNESS  3

/* Camera frame converted to RGB888 packed (R, G, B per pixel), the format od_pre_process expects */
static uint8_t camera_rgb[CAMERA_WIDTH * CAMERA_HEIGHT * 3] __attribute__((aligned(32), section(".bss.camera_frame_buf")));

static float cycles_to_ms(uint32_t cycles)
{
    return cycles * 1000.0f / SystemCoreClock;
}

int main(void)
{
    tflm_tensor_t input;
    tflm_tensor_t output[2];
    od_output_t yolo_outputs[2];
    od_box_t boxes[MAX_DETECTIONS];

    /* Pin multiplexing and GPIOs (camera I2C, camera power/reset) from the board pins.h / gpios.h */
    if (board_pins_config() != 0 || board_gpios_config() != 0) {
        goto bail;
    }

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (stdout_init() != 0) {
        goto bail;
    }
#endif

    printf("Welcome to Alif Semiconductor AI/ML training! (phase 04 - camera and display)\n");

    /* Secure Enclave services are needed for clocks and power (camera, MIPI) */
    se_services_port_init();
    if (board_clocks_config(CLKEN_HFOSC_MASK | CLKEN_CLK_100M_MASK) != 0) {
        printf("Failed to enable clocks\n");
        goto bail;
    }

    /* Camera first: */
    if (camera_init() != 0) {
        printf("Failed to initialize camera\n");
        goto bail;
    }
    printf("Camera initialized successfully\n");

    if (display_init() != 0) {
        printf("Failed to initialize display\n");
        goto bail;
    }
    printf("Display initialized successfully\n");

    if (npu_init() != 0) {
        printf("Failed to initialize Ethos-U NPU\n");
        goto bail;
    }
    printf("Ethos-U NPU initialized successfully\n");

    if (tflm_init(get_model_tflite()) != 0) {
        printf("Failed to initialize TFLM\n");
        goto bail;
    }
    printf("TFLM initialized successfully\n");

    if (tflm_get_input(0, &input) != 0 ||
        tflm_get_output(0, &output[0]) != 0 ||
        tflm_get_output(1, &output[1]) != 0) {
        printf("Failed to get model tensors\n");
        goto bail;
    }
    if (input.width != CAMERA_WIDTH || input.height != CAMERA_HEIGHT || input.channels != 1) {
        printf("Model input %dx%dx%d does not match the camera output %dx%d\n",
               input.width, input.height, input.channels, CAMERA_WIDTH, CAMERA_HEIGHT);
        goto bail;
    }
    for (int i = 0; i < 2; i++) {
        yolo_outputs[i].data = (const int8_t *)output[i].data;
        yolo_outputs[i].grid_size = output[i].width;
        yolo_outputs[i].scale = output[i].scale;
        yolo_outputs[i].zero_point = output[i].zero_point;
    }

    for (uint32_t frame = 0; ; frame++) {
        uint32_t t_capture = ARM_PMU_Get_CCNTR();
        const uint8_t *rgb_planar = camera_capture();
        t_capture = ARM_PMU_Get_CCNTR() - t_capture;
        if (rgb_planar == NULL) {
            printf("Camera capture failed\n");
            goto bail;
        }

        /* AIPL: ISP output RGB888 planar (RRR..GGG..BBB..) -> RGB888 packed (RGBRGB..) */
        uint32_t t_convert = ARM_PMU_Get_CCNTR();
        aipl_error_t aipl_ret = aipl_color_convert_rgb888p_to_rgb888(rgb_planar, camera_rgb,
                                                                     CAMERA_WIDTH, CAMERA_WIDTH, CAMERA_HEIGHT);
        t_convert = ARM_PMU_Get_CCNTR() - t_convert;
        if (aipl_ret != AIPL_ERR_OK) {
            printf("AIPL color conversion failed: %s\n", aipl_error_str(aipl_ret));
            goto bail;
        }

        /* Pre processing: RGB888 image -> grayscale INT8 directly into the input tensor */
        uint32_t t_pre = ARM_PMU_Get_CCNTR();
        od_pre_process(camera_rgb, (int8_t *)input.data, CAMERA_WIDTH * CAMERA_HEIGHT);
        t_pre = ARM_PMU_Get_CCNTR() - t_pre;

        /* Inference: input is already in the tensor, outputs are read in place */
        uint32_t t_inference = ARM_PMU_Get_CCNTR();
        if (tflm_run_inference(NULL, 0, NULL, 0) != 0) {
            printf("Failed to run inference\n");
            goto bail;
        }
        t_inference = ARM_PMU_Get_CCNTR() - t_inference;

        /* Post processing: YOLO output tensors -> bounding boxes */
        uint32_t t_post = ARM_PMU_Get_CCNTR();
        int num_boxes = od_post_process(yolo_outputs, boxes, MAX_DETECTIONS);
        t_post = ARM_PMU_Get_CCNTR() - t_post;

        /* Display: camera image and detection boxes, both scaled from model input coordinates */
        uint32_t t_display = ARM_PMU_Get_CCNTR();
        display_draw_image(camera_rgb, CAMERA_WIDTH, CAMERA_HEIGHT, IMAGE_X, IMAGE_Y, IMAGE_SCALE);
        for (int i = 0; i < num_boxes; i++) {
            display_draw_box(IMAGE_X + boxes[i].x * IMAGE_SCALE, IMAGE_Y + boxes[i].y * IMAGE_SCALE,
                             boxes[i].w * IMAGE_SCALE, boxes[i].h * IMAGE_SCALE,
                             BOX_THICKNESS, DISPLAY_COLOR_GREEN);
        }
        display_update();
        t_display = ARM_PMU_Get_CCNTR() - t_display;

        printf("Frame %lu: capture %.1fms, convert %.1fms, pre %.1fms, inference %.1fms, post %.1fms, display %.1fms, faces %d\n",
               (unsigned long)frame, cycles_to_ms(t_capture), cycles_to_ms(t_convert), cycles_to_ms(t_pre),
               cycles_to_ms(t_inference), cycles_to_ms(t_post), cycles_to_ms(t_display), num_boxes);
        for (int i = 0; i < num_boxes; i++) {
            printf("  [%d] x=%3d y=%3d w=%3d h=%3d score=%.2f\n",
                   i, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, (double)boxes[i].score);
        }
    }

bail:
    while (1) {
        __WFE();
    }

}
