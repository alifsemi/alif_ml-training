/* Ethos-U NPU PMU (performance monitoring unit) counters */
#ifndef NPU_PMU_H
#define NPU_PMU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the Ethos-U PMU event counters and the cycle counter.
 *        Call after npu_init(). Counters run only while the NPU executes
 *        a command stream (ethosu_inference_begin/end callbacks), so they
 *        accumulate over all Ethos-U operators of one Invoke().
 */
void npu_pmu_init(void);

/**
 * @brief Reset the cycle counter and all event counters to zero.
 */
void npu_pmu_reset(void);

/**
 * @brief Print the cycle counter, event counters and NPU idle cycles.
 */
void npu_pmu_print(void);

#ifdef __cplusplus
}
#endif

#endif /* NPU_PMU_H */
