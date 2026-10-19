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
  * @brief phase_00 of the Alif Semiconductor AI/ML training.
  *        Target is to introduce the basic environment setup and initialization of the board.
  *        
  *        - Choose the board using CMSIS solution (sprocket icon)
  *        - Build the project using CMSIS solution (hammer icon)
  *        - Program the binary with SE toolkit (F1 --> Tasks:Run Task --> Program with Security Toolkit)
  *        - Check the log output using UART terminal
  *        - Try connecting the debugger.
  *          -  Remember to program the debug stubs using SE toolkit. (F1 --> Tasks:Run Task --> Alif: Install debug stubs)
  *          -  Press "run and debug" in left side panel. And "play" to start debugging
  */
#include "RTE_Components.h"
#include <stdio.h>
#include CMSIS_device_header

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
#include "retarget_stdout.h"
#include <retarget_init.h>
#endif
int main(void)
{

#if defined(RTE_CMSIS_Compiler_STDOUT_Custom)
    if (stdout_init() != 0) {
        while (1) {
        }
    }

    printf("Welcome to Alif Semiconductor AI/ML training! (phase 00 - Hello)\n");

    while (1) {
        __WFE();
    }
#endif
}
