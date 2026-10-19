/* Ethos-U NPU driver initialization */
#ifndef ETHOSU_INIT_H
#define ETHOSU_INIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Arm Ethos-U driver and enable its IRQ.
 *        U85 (ETHOSU85) uses the shared NPU at NPU_HG_BASE.
 *        U55 uses the per-core local NPU at NPULOCAL_BASE.
 * @return 0 on success, -1 on failure
 */
int32_t npu_init(void);

struct ethosu_driver;

/**
 * @brief Get the Ethos-U driver instance initialized by npu_init().
 */
struct ethosu_driver *npu_get_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* ETHOSU_INIT_H */
