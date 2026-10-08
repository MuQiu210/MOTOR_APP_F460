/// @file  gpio.h
/// @brief APP GPIO initialization.

#ifndef __APP_HARDWARE_GPIO_H__
#define __APP_HARDWARE_GPIO_H__

#include <stdbool.h>

void gpio_init(void);
void gpio_status_led_set_stop(bool stop_mode);

#endif /* __APP_HARDWARE_GPIO_H__ */
