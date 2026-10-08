/// @file  gpio.c
/// @brief APP GPIO initialization.

#include "gpio.h"

#include "main.h"

#define LED_R_PORT GPIO_PORT_C
#define LED_R_PIN  GPIO_PIN_14
#define LED_G_PORT GPIO_PORT_C
#define LED_G_PIN  GPIO_PIN_15

static void gpio_led_write(uint8_t port, uint32_t pin, bool on)
{
    if (on)
    {
        GPIO_ResetPins(port, pin);
    }
    else
    {
        GPIO_SetPins(port, pin);
    }
}

/// @brief Initialize the GPIO pins used by the application.
/// @param None
/// @retval None
void gpio_init(void)
{
    stc_gpio_init_t gpio_init_cfg;

    /* PH0/PH1: XTAL-IN/OUT — must be analog before clock_init() starts XTAL. */
    (void)GPIO_StructInit(&gpio_init_cfg);
    gpio_init_cfg.u16PinAttr = PIN_ATTR_ANALOG;
    (void)GPIO_Init(GPIO_PORT_H, GPIO_PIN_00, &gpio_init_cfg);
    (void)GPIO_Init(GPIO_PORT_H, GPIO_PIN_01, &gpio_init_cfg);

    /* F460 XHCode uses PB3 as CAN RX, PA15 as SPI1 SCK and PA12 as SPI1 MISO.
       Release the debug functions that overlap those alternate-function pins. */
    GPIO_SetDebugPort(GPIO_PIN_TDI | GPIO_PIN_SWO, DISABLE);

    /* All ADC pins used by the F460 XHCode configuration are analog inputs. */
    (void)GPIO_StructInit(&gpio_init_cfg);
    gpio_init_cfg.u16PinAttr = PIN_ATTR_ANALOG;
    (void)GPIO_Init(GPIO_PORT_A,
                    GPIO_PIN_01 | GPIO_PIN_02 | GPIO_PIN_03 | GPIO_PIN_04 |
                    GPIO_PIN_05 | GPIO_PIN_06 | GPIO_PIN_07,
                    &gpio_init_cfg);
    (void)GPIO_Init(GPIO_PORT_B, GPIO_PIN_00 | GPIO_PIN_01, &gpio_init_cfg);

    (void)GPIO_StructInit(&gpio_init_cfg);
    gpio_init_cfg.u16PinDir = PIN_DIR_OUT;
    gpio_init_cfg.u16PinAttr = PIN_ATTR_DIGITAL;
    (void)GPIO_Init(LED_R_PORT, LED_R_PIN, &gpio_init_cfg);
    (void)GPIO_Init(LED_G_PORT, LED_G_PIN, &gpio_init_cfg);
    (void)GPIO_Init(GPIO_PORT_B, GPIO_PIN_06 | GPIO_PIN_07, &gpio_init_cfg);
    gpio_status_led_set_stop(true);
    GPIO_SetPins(GPIO_PORT_B, GPIO_PIN_06);
    GPIO_ResetPins(GPIO_PORT_B, GPIO_PIN_07);
}

void gpio_status_led_set_stop(bool stop_mode)
{
    gpio_led_write(LED_R_PORT, LED_R_PIN, stop_mode);
    gpio_led_write(LED_G_PORT, LED_G_PIN, !stop_mode);
}
