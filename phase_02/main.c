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
  * @brief phase_02 of the Alif Semiconductor AI/ML training.
  *        Target is to integrate Ethos-U NPU with TFLM.
  *        Steps:
  *          - NPU driver configuration and callbacks
  *          - Add Ethos-U PMU counters for performance monitoring.
  *          - Initialize Ethos-U NPU.
  *          - Run inference using TFLM on Ethos-U.
  *          - Compare inference time with and without NPU acceleration.
  *          - Compare inference time U55 vs U85 NPU (change the NPU driver and model in the cproject.yml)
  *          - Compare SRAM copy of the model for U85 NPU
  *          - Enable NPU PMU counters and inspect the results.
  *          - Enable power measurement GPIO instrumentation and inspect the results.
  *        
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

int main(void)
{

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (stdout_init() != 0) {
        goto bail;
    }
#endif

    printf("Welcome to Alif Semiconductor AI/ML training! (phase 02 - Ethos-U NPU integration and measurements)\n");

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

    printf("Running inference...\n");
    if (tflm_run_inference(0,0,0,0) != 0) {
        printf("Failed to run inference\n");
        goto bail;
    }
    printf("Inference completed successfully\n");

bail:
    while (1) {
        __WFE();
    }

}
