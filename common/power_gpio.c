/*
 * GPIO marker for external power measurement, based on board_utils.c of the
 * Alif ML embedded evaluation kit. The pin is HIGH while the measured
 * section (inference) runs, so a power analyzer can trigger on it.
 */

#include "Driver_IO.h"
#include "pinconf.h"
#include "board_config.h"

#include "power_gpio.h"

#if !defined(POWER_GPIO_PORT)
#define POWER_GPIO_PORT               BOARD_LEDRGB0_B_GPIO_PORT
#define POWER_GPIO_PIN                BOARD_LEDRGB0_B_GPIO_PIN
#define POWER_GPIO_ALTERNATE_FUNCTION BOARD_LEDRGB0_B_ALTERNATE_FUNCTION
#endif

extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(POWER_GPIO_PORT);
static ARM_DRIVER_GPIO *const gpio_drv = &ARM_Driver_GPIO_(POWER_GPIO_PORT);

int32_t power_gpio_init(void)
{
    /* board_pins_config() is not called by the application, so set the pinmux here. */
    if (pinconf_set(PORT_(POWER_GPIO_PORT), POWER_GPIO_PIN, POWER_GPIO_ALTERNATE_FUNCTION,
                    PADCTRL_OUTPUT_DRIVE_STRENGTH_4MA) != 0) {
        return -1;
    }

    if (gpio_drv->Initialize(POWER_GPIO_PIN, NULL) != ARM_DRIVER_OK ||
        gpio_drv->PowerControl(POWER_GPIO_PIN, ARM_POWER_FULL) != ARM_DRIVER_OK ||
        gpio_drv->SetValue(POWER_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW) != ARM_DRIVER_OK ||
        gpio_drv->SetDirection(POWER_GPIO_PIN, GPIO_PIN_DIRECTION_OUTPUT) != ARM_DRIVER_OK) {
        return -1;
    }

    return 0;
}

void power_gpio_high(void)
{
    gpio_drv->SetValue(POWER_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
}

void power_gpio_low(void)
{
    gpio_drv->SetValue(POWER_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
}
