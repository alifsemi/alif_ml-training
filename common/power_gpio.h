/* GPIO marker for external power measurement */
#ifndef POWER_GPIO_H
#define POWER_GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the power measurement GPIO as an output driven LOW.
 *        Defaults to the blue LED of LEDRGB0 (BOARD_LEDRGB0_B_*);
 *        override with POWER_GPIO_PORT, POWER_GPIO_PIN and POWER_GPIO_ALTERNATE_FUNCTION.
 * @return 0 on success, -1 on failure
 */
int32_t power_gpio_init(void);

/**
 * @brief Drive the power measurement GPIO HIGH (start of measured section).
 */
void power_gpio_high(void);

/**
 * @brief Drive the power measurement GPIO LOW (end of measured section).
 */
void power_gpio_low(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_GPIO_H */
