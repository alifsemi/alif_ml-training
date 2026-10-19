/*
 * Copyright (c) 2025 Arm Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ethos-U driver callbacks for Alif Ensemble:
 *   - D-cache flush/invalidate
 *   - Address remap (local TCM -> global address for NPU bus master)
 */

#include "RTE_Components.h"
#include CMSIS_device_header

#include "ethosu_driver.h"
#include "sys_utils.h"        /* LocalToGlobal() */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------- D-cache callbacks ---------- */
/*
 * The driver calls these once per inference with every base address region
 * (weights, arena, ...). Per-address maintenance over those sizes is slower
 * than a whole-cache operation, so the whole cache is cleaned/invalidated.
 */

static bool dcache_enabled(void)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    return (SCB->CCR & SCB_CCR_DC_Msk) != 0U;
#else
    return false;
#endif
}

void ethosu_flush_dcache(const uint64_t *base_addr, const size_t *base_addr_size, int num_base_addr)
{
    (void)base_addr;
    (void)base_addr_size;

    if (num_base_addr > 0 && dcache_enabled()) {
        SCB_CleanDCache();
    } else {
        __DSB();
    }
}

void ethosu_invalidate_dcache(const uint64_t *base_addr, const size_t *base_addr_size, int num_base_addr)
{
    (void)base_addr;
    (void)base_addr_size;

    if (num_base_addr > 0 && dcache_enabled()) {
        /* Clean as well: a plain invalidate would drop dirty write-back lines. */
        SCB_CleanInvalidateDCache();
    } else {
        __DSB();
    }
}

/* ---------- Address remap ---------- */
/*
 * The NPU is a bus master and cannot see local TCM aliases (0x0000_xxxx
 * for ITCM, 0x2000_xxxx for DTCM).  Convert them to global addresses
 * that the NPU can reach via the system bus.
 * Non-TCM addresses (SRAM, MRAM, OSPI) pass through unchanged.
 */
uint64_t ethosu_address_remap(uint64_t address, int index)
{
    (void)index;
    return LocalToGlobal((void *)(uint32_t)address);
}

#if defined(ETHOSU85)
/* ---------- AXI port selection ----------
Accessible regions to U85 are main SRAM (0), OSPI (2,A,B,C,D), MRAM (8) and TCMs (5)
SRAM and TCMs need to use SRAM ports, MRAM and OSPI need to use EXT port
Quick-and-dirty check that gives the right answer for valid addresses:
 */
unsigned int ethosu_config_select(uint64_t address, int index)
{
    (void)index;
    return (address & 0xA0000000U) ? 2U : 0U;
}
#endif
