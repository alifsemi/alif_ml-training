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
  * @brief phase_01 of the Alif Semiconductor AI/ML training.
  *        Target is to integrate TFLM (TensorFlow Lite for Microcontrollers) runtime into the project.
  *        Steps: 
  *          - TFLM pack included in the project (cproject.yml)
  *          - Initialize the TFLM runtime.
  *          - Verify the integration by running a simple TFLM model.
  *        
  */
#include "RTE_Components.h"
#include <stdio.h>
#include CMSIS_device_header

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
#include "retarget_stdout.h"
#include <retarget_init.h>
#endif

#include "model.h"
#include "tflm_inference.h"

int main(void)
{

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (stdout_init() != 0) {
        goto bail;
    }
#endif

    printf("Welcome to Alif Semiconductor AI/ML training! (phase 01 - TFLM integration)\n");

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
