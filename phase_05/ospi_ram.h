/* OSPI RAM (external PSRAM) initialization */
#ifndef OSPI_RAM_H
#define OSPI_RAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the OSPI controller and the APS512XXN PSRAM and enable
 *        XIP mode. After a successful return the PSRAM is memory mapped at
 *        the OSPI0 XIP base (0xA0000000) and can be read/written directly.
 *        Pins must be configured (board_pins_config()) before calling.
 * @return 0 on success, -1 on failure
 */
int32_t ospi_ram_init(void);

#ifdef __cplusplus
}
#endif

#endif /* OSPI_RAM_H */
