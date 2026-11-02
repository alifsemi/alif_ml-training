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
  * @brief phase_05 of the Alif Semiconductor AI/ML training.
  *        Target is to run inference with tflite model in OSPI RAM.
  *        Steps:
  *          - Initialize OSPI RAM
  *          - Initialize OSPI RAM as XIP (memory mapped)
  *          - Copy model to OSPI RAM
  *          - Run inference from OSPI RAM
  *          - Compare inference time with previous results in Phase_02
  *        
  */
#include "RTE_Components.h"
#include <stdio.h>
#include CMSIS_device_header

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
#include "retarget_stdout.h"
#include <retarget_init.h>
#endif

#include "board_config.h"

#include "ethosu_init.h"
#include "model.h"
#include "ospi_ram.h"
#include "tflm_inference.h"

int main(void)
{

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (stdout_init() != 0) {
        goto bail;
    }
#endif

    printf("Welcome to Alif Semiconductor AI/ML training! (phase 05 - OSPI RAM integration and measurements)\n");

    /* Pin multiplexing (OSPI0/OSPI1 PSRAM data, clock, RXDS and SS) from the board pins.h */
    if (board_pins_config() != 0) {
        printf("Failed to configure board pins\n");
        goto bail;
    }

    /* Must run before get_model_tflite(): the model is copied to OSPI RAM (.bss.model_psram). */
    if (ospi_ram_init() != 0) {
        printf("Failed to initialize OSPI RAM\n");
        goto bail;
    }
    printf("OSPI RAM initialized in XIP mode\n");

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
