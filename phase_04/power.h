/* Peripheral power and clocks through the Secure Enclave run profile */
#ifndef POWER_H
#define POWER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable peripheral clocks and PHY power in the SE run profile.
 *        The current run profile is read and only the given bits are added,
 *        so the rest of the system (NPU, CPU clock) stays as it is.
 *        Changing the run profile can clear SRAM0/SRAM1, so call this before
 *        anything is stored there (tensor arena, frame buffers).
 *        Requires se_services_port_init() to be called first.
 * @param ip_clock_gating  e.g. CAMERA_MASK | MIPI_CSI_MASK
 * @param phy_pwr_gating   e.g. LDO_PHY_MASK | MIPI_RX_DPHY_MASK
 * @return 0 on success, negative on failure
 */
int power_enable(uint32_t ip_clock_gating, uint32_t phy_pwr_gating);

#ifdef __cplusplus
}
#endif

#endif /* POWER_H */
