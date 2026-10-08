/// @file  bsp.c
/// @brief App board support package implementation.

#include "bsp.h"
#include "main.h"

#include "adc.h"
#include "can.h"
#include "gpio.h"
#include "flash.h"
#include "uart3.h"
#include "clock.h"
#include "timer4.h"
#include "encoder.h"

#include "base.h"
#include "modbus.h"
#include "critical.h"

#include "motor.h"
#include "can_slave.h"
#include "modbus_slave.h"
#include "save.h"

const struct firmware_identity g_app_identity
    __attribute__((used, aligned(8), section(".app_version"))) = {
    .build_time = FW_BUILD_VERSION,
    .magic = {'F', 'I', 'D', '1'},
    .product_id = FIRMWARE_PRODUCT_ID,
    .version = APP_FIRMWARE_VERSION,
    .reserved = {0U},
};
const uint64_t g_app_build_version __attribute__((used)) = FW_BUILD_VERSION;
struct save_info g_save;
struct nvstore g_nvstore_handler;
struct task g_task_handler;
struct modbus_slave g_modbus_node;
struct modbus_rtu_slave g_modbus_rtu_slave;

/// @brief Handle the SysTick interrupt and advance the task scheduler tick.
void SysTick_Handler(void)
{
    (void)task_on_timer(&g_task_handler, 1);
}

/// @brief Initialize the application BSP and all runtime services.
void bsp_init(void)
{
    LL_PERIPH_WE(LL_PERIPH_ALL);

    task_init(&g_task_handler);

    clock_init();
    flash_init();
    gpio_init();
    can1_init();
    timer4_init();
    adc_init();
    encoder_init();
    uart3_init(921600UL);
    SysTick_Init(1000UL);

    save_init();

    motor_init();
    can_slave_init();
    modbus_slave_init();

    save_cycle_register();

    /* Restore peripheral write protection before enabling interrupts. */
    LL_PERIPH_WP(LL_PERIPH_ALL);
    critical_exit(0UL);
}
