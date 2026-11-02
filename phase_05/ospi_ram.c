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
 * @file ospi_ram.c
 * @brief OSPI PSRAM XIP initialization using the APS512XXN driver from the
 *        Ensemble DFP. Based on ospi_ram.c in alif_ml-embedded-evaluation-kit.
 */
#include <stdio.h>
#include <string.h>

#include "RTE_Device.h"
#include "RTE_Components.h"
#include CMSIS_device_header

#include "board_defs.h"
#include "ospi_psram_xip.h"
#include "APS512XXN_PSRAM.h"
#include "ospi_ram.h"

#if BOARD_APS512XXN_PSRAM_PRESENT && RTE_APS512XXN_PSRAM_DUAL_OCTAL_MODE_ENABLE && \
    (BOARD_PSRAM_OSPI_INSTANCE == 0) && (RTE_OSPI0_SPI_FRAME_FORMAT != 4)
#error "APS512XXN dual octal mode requires RTE_OSPI0_SPI_FRAME_FORMAT 4 (Dual Octal SPI FRF) in RTE_Device.h"
#endif

static ospi_psram_xip_config psram_config;

static int32_t init_psram_config(OSPI_INSTANCE instance, ospi_psram_xip_config *config)
{
    if (!config) {
        return -1;
    }

    memset(config, 0, sizeof(ospi_psram_xip_config));

    config->config_mode = CONFIG_MODE_USER_PARAMETERS;
    config->instance    = instance;

    if (config->instance == OSPI_INSTANCE_0) {
        config->spi_frf        = RTE_OSPI0_SPI_FRAME_FORMAT;
        config->bus_speed      = RTE_OSPI0_BUS_SPEED;
        config->ddr_drive_edge = RTE_OSPI0_DDR_DRIVE_EDGE;
        config->rxds_delay     = RTE_OSPI0_RXDS_DELAY;
        config->dfs            = RTE_OSPI0_DFS;
        config->slave_select   = RTE_OSPI0_CHIP_SELECTION_PIN;
        config->wait_cycles    = RTE_OSPI0_WAIT_CYCLES;
#if SOC_FEAT_AES_OSPI_SIGNALS_DELAY
        config->signal_delay   = RTE_OSPI0_SIGNAL_DELAY;
#endif
#ifdef RTE_OSPI1
    } else if (config->instance == OSPI_INSTANCE_1) {
        config->spi_frf        = RTE_OSPI1_SPI_FRAME_FORMAT;
        config->bus_speed      = RTE_OSPI1_BUS_SPEED;
        config->ddr_drive_edge = RTE_OSPI1_DDR_DRIVE_EDGE;
        config->rxds_delay     = RTE_OSPI1_RXDS_DELAY;
        config->dfs            = RTE_OSPI1_DFS;
        config->slave_select   = RTE_OSPI1_CHIP_SELECTION_PIN;
        config->wait_cycles    = RTE_OSPI1_WAIT_CYCLES;
#if SOC_FEAT_AES_OSPI_SIGNALS_DELAY
        config->signal_delay   = RTE_OSPI1_SIGNAL_DELAY;
#endif
#endif
    } else {
        return -1;
    }

    return 0;
}

int32_t ospi_ram_init(void)
{
#if BOARD_APS512XXN_PSRAM_PRESENT
    if (init_psram_config(BOARD_PSRAM_OSPI_INSTANCE, &psram_config) != 0) {
        printf("PSRAM config error\n");
        return -1;
    }

    psram_config.ram_init    = aps512xxn_psram_init;
    psram_config.ram_type    = RAM_TYPE_PSRAM;
    /* XIP read/write latency must match the latency programmed into the APS512XXN mode registers. */
    psram_config.wait_cycles = RTE_APS512XXN_PSRAM_WAIT_CYCLES;

    if (ospi_psram_xip_init(&psram_config) == 0) {
        printf("APS512XXN RAM detected\n");
        return 0;
    }
#endif

    return -1;
}
