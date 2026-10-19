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
  * @brief phase_03 of the Alif Semiconductor AI/ML training.
  *        Target is to add pre and post processing to the YOLO model inference.
  *        Steps:
  *          - Pre processing is the conversion of the input image to the format expected by the model input tensor.
  *            (e.g. resize, normalize, etc.)
  *          - Post processing is the conversion of the model output tensor to the format expected by the application or user.
  *            (e.g. bounding boxes, classes, scores, etc.)
  */
#include "RTE_Components.h"
#include <stdio.h>
#include CMSIS_device_header

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
#include "retarget_stdout.h"
#include <retarget_init.h>
#endif

#include "ethosu_init.h"
#include "model.h"
#include "tflm_inference.h"
#include "object_detection.h"
#include "sample_image.h"

#define MAX_DETECTIONS 10

static float cycles_to_ms(uint32_t cycles)
{
    return cycles * 1000.0f / SystemCoreClock;
}

int main(void)
{
    tflm_tensor_t input;
    tflm_tensor_t output[2];
    od_box_t boxes[MAX_DETECTIONS];
    uint32_t cycles;

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (stdout_init() != 0) {
        goto bail;
    }
#endif

    printf("Welcome to Alif Semiconductor AI/ML training! (phase 03 - pre and post processing)\n");

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
    if (input.width != SAMPLE_IMAGE_WIDTH || input.height != SAMPLE_IMAGE_HEIGHT || input.channels != 1) {
        printf("Model input %dx%dx%d does not match the sample image\n",
               input.width, input.height, input.channels);
        goto bail;
    }

    /* Pre processing: RGB888 image -> grayscale INT8 directly into the input tensor */
    printf("Pre processing...\n");
    cycles = ARM_PMU_Get_CCNTR();
    od_pre_process(sample_image, (int8_t *)input.data, SAMPLE_IMAGE_WIDTH * SAMPLE_IMAGE_HEIGHT);
    cycles = ARM_PMU_Get_CCNTR() - cycles;
    printf("Pre processing time: %.3fms\n", cycles_to_ms(cycles));

    /* Inference: input is already in the tensor, outputs are read in place */
    printf("Running inference...\n");
    if (tflm_run_inference(NULL, 0, NULL, 0) != 0) {
        printf("Failed to run inference\n");
        goto bail;
    }
    printf("Inference completed successfully\n");

    /* Post processing: YOLO output tensors -> bounding boxes */
    printf("Post processing...\n");
    od_output_t yolo_outputs[2];
    for (int i = 0; i < 2; i++) {
        yolo_outputs[i].data = (const int8_t *)output[i].data;
        yolo_outputs[i].grid_size = output[i].width;
        yolo_outputs[i].scale = output[i].scale;
        yolo_outputs[i].zero_point = output[i].zero_point;
    }
    cycles = ARM_PMU_Get_CCNTR();
    int num_boxes = od_post_process(yolo_outputs, boxes, MAX_DETECTIONS);
    cycles = ARM_PMU_Get_CCNTR() - cycles;
    printf("Post processing time: %.3fms\n", cycles_to_ms(cycles));

    printf("Detected %d face(s):\n", num_boxes);
    for (int i = 0; i < num_boxes; i++) {
        printf("  [%d] x=%3d y=%3d w=%3d h=%3d score=%.2f\n",
               i, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, (double)boxes[i].score);
    }

bail:
    while (1) {
        __WFE();
    }

}
