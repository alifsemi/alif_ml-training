#include <stdio.h>

#include "se_services_port.h"
#include "services_lib_api.h"

#include "power.h"

int power_enable(uint32_t ip_clock_gating, uint32_t phy_pwr_gating)
{
  run_profile_t profile = {0};
  uint32_t error_code = 0;

  uint32_t ret = SERVICES_get_run_cfg(se_services_s_handle, &profile, &error_code);
  if (ret != SERVICES_REQ_SUCCESS || error_code != 0) {
    printf("  [PWR] ERROR: SERVICES_get_run_cfg failed (%lu, %lu)\n",
           (unsigned long)ret, (unsigned long)error_code);
    return -1;
  }

  /* SRAM0/SRAM1 must stay powered, otherwise their content is lost */
  profile.memory_blocks   |= SRAM0_MASK | SRAM1_MASK;
  profile.ip_clock_gating |= ip_clock_gating;
  profile.phy_pwr_gating  |= phy_pwr_gating;

  ret = SERVICES_set_run_cfg(se_services_s_handle, &profile, &error_code);
  if (ret != SERVICES_REQ_SUCCESS || error_code != 0) {
    printf("  [PWR] ERROR: SERVICES_set_run_cfg failed (%lu, %lu)\n",
           (unsigned long)ret, (unsigned long)error_code);
    return -1;
  }
  return 0;
}
