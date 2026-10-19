/*
 * Ethos-U NPU PMU counters, based on ethosu_profiler.c of the
 * Alif ML embedded evaluation kit.
 *
 * The driver soft-resets the NPU on the first power request of an inference,
 * which clears the PMU configuration. ETHOSU_PMU_Enable() holds a power
 * request, so the configuration done in npu_pmu_init() is preserved.
 */

#include <stdio.h>

#include "ethosu_driver.h"
#include "pmu_ethosu.h"

#include "ethosu_init.h"
#include "npu_pmu.h"

struct npu_pmu_event {
    enum ethosu_pmu_event_type type;
    const char *name;
    const char *unit;
};

static const struct npu_pmu_event pmu_events[] = {
#if defined(ETHOSU55) || defined(ETHOSU65)
    { ETHOSU_PMU_NPU_ACTIVE,                 "NPU_ACTIVE",                 "cycles" },
    { ETHOSU_PMU_AXI0_RD_DATA_BEAT_RECEIVED, "AXI0_RD_DATA_BEAT_RECEIVED", "beats"  },
    { ETHOSU_PMU_AXI0_WR_DATA_BEAT_WRITTEN,  "AXI0_WR_DATA_BEAT_WRITTEN",  "beats"  },
    { ETHOSU_PMU_AXI1_RD_DATA_BEAT_RECEIVED, "AXI1_RD_DATA_BEAT_RECEIVED", "beats"  },
#elif defined(ETHOSU85)
    { ETHOSU_PMU_NPU_ACTIVE,                 "NPU_ACTIVE",                 "cycles" },
    { ETHOSU_PMU_SRAM_RD_DATA_BEAT_RECEIVED, "SRAM_RD_DATA_BEAT_RECEIVED", "beats"  },
    { ETHOSU_PMU_SRAM_WR_DATA_BEAT_WRITTEN,  "SRAM_WR_DATA_BEAT_WRITTEN",  "beats"  },
    { ETHOSU_PMU_EXT_RD_DATA_BEAT_RECEIVED,  "EXT_RD_DATA_BEAT_RECEIVED",  "beats"  },
    { ETHOSU_PMU_EXT_WR_DATA_BEAT_WRITTEN,   "EXT_WR_DATA_BEAT_WRITTEN",   "beats"  },
#endif
};

#define NUM_PMU_EVENTS (sizeof(pmu_events) / sizeof(pmu_events[0]))

_Static_assert(NUM_PMU_EVENTS <= ETHOSU_PMU_NCOUNTERS, "Too many PMU events for this NPU");

static uint32_t pmu_cnt_mask;

void npu_pmu_init(void)
{
    struct ethosu_driver *drv = npu_get_driver();
    uint32_t mask = ETHOSU_PMU_CCNT_Msk;

    ETHOSU_PMU_Enable(drv);

    for (uint32_t i = 0; i < NUM_PMU_EVENTS; i++) {
        ETHOSU_PMU_Set_EVTYPER(drv, i, pmu_events[i].type);
        mask |= 1UL << i;
    }
    pmu_cnt_mask = mask;

    ETHOSU_PMU_Set_CNTR_OVS(drv, pmu_cnt_mask);
    ETHOSU_PMU_CNTR_Disable(drv, pmu_cnt_mask);
    npu_pmu_reset();
}

void npu_pmu_reset(void)
{
    struct ethosu_driver *drv = npu_get_driver();

    ETHOSU_PMU_CYCCNT_Reset(drv);
    ETHOSU_PMU_EVCNTR_ALL_Reset(drv);
    ETHOSU_PMU_Set_CNTR_OVS(drv, pmu_cnt_mask);
}

void npu_pmu_print(void)
{
    struct ethosu_driver *drv = npu_get_driver();
    const uint32_t ovs = ETHOSU_PMU_Get_CNTR_OVS(drv);
    const uint64_t total = ETHOSU_PMU_Get_CCNTR(drv);
    uint32_t active = 0;

    printf("Ethos-U PMU counters:\n");
    printf("  %-28s: %llu cycles\n", "NPU_TOTAL", (unsigned long long)total);

    for (uint32_t i = 0; i < NUM_PMU_EVENTS; i++) {
        const uint32_t value = ETHOSU_PMU_Get_EVCNTR(drv, i);
        if (pmu_events[i].type == ETHOSU_PMU_NPU_ACTIVE) {
            active = value;
        }
        printf("  %-28s: %lu %s%s\n", pmu_events[i].name, (unsigned long)value,
               pmu_events[i].unit, (ovs & (1UL << i)) ? " (OVERFLOW)" : "");
    }

    printf("  %-28s: %llu cycles\n", "NPU_IDLE", (unsigned long long)(total - active));
}

/* Driver callbacks: count only while the NPU runs a command stream. */
void ethosu_inference_begin(struct ethosu_driver *drv, void *user_arg)
{
    (void)user_arg;
    ETHOSU_PMU_CNTR_Disable(drv, pmu_cnt_mask);
    ETHOSU_PMU_CNTR_Enable(drv, pmu_cnt_mask);
}

void ethosu_inference_end(struct ethosu_driver *drv, void *user_arg)
{
    (void)user_arg;
    ETHOSU_PMU_CNTR_Disable(drv, pmu_cnt_mask);
}
