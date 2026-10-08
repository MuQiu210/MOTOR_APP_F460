/// @file  save.c
/// @brief NVStore persistence and firmware metadata implementation.

#include "save.h"
#include "bsp.h"

#include <string.h>

#include "critical.h"
#include "flash.h"
#include "firmware_storage.h"
#include "hc32f460_flash_layout.h"
#include "nvstore.h"

static uint8_t s_app_image_md5[16];
static uint32_t s_app_image_length;
static volatile bool s_save_pending;

static const nvstore_config_t s_app_nvstore_config = {
    .bank1_addr = NVSTORE_BANK0_ADDR,
    .bank2_addr = NVSTORE_BANK1_ADDR,
    .bank_size = NVSTORE_BANK0_SIZE,
    .cache = &g_save,
    .cache_size = sizeof(g_save),
    .flash_erase = flash_erase,
    .flash_write = flash_write,
    .active_bank_read = firmware_info_save_bank_read,
    .active_bank_write = firmware_info_save_bank_write,
};

/// @brief Persist pending save data once the control loop is idle.
static void save_on_step(void)
{
    uint32_t primask;

    if (!s_save_pending)
    {
        return;
    }

    /* Flash erase/program stalls code fetch, so only save when fast control IRQs are no longer required. */
    if (Model_Y.Enable)
    {
        return;
    }

    primask = critical_enter();
    if (nvstore_write(&g_nvstore_handler) == 0)
    {
        s_save_pending = false;
    }
    critical_exit(primask);
}

void save_init(void)
{
    nvstore_init(&g_nvstore_handler, &s_app_nvstore_config);
    nvstore_read(&g_nvstore_handler);
    firmware_info_recv(&s_app_image_length, s_app_image_md5);
}

void save_request(void)
{
    s_save_pending = true;
}

void save_cycle_register(void)
{
    task_cycle_register(&g_task_handler, &save_on_step, 1);
}
