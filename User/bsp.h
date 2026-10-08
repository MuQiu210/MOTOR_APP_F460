/// @file  bsp.h
/// @brief App board support package interface.

#ifndef __APP_USER_BSP_H__
#define __APP_USER_BSP_H__

#include <stdbool.h>
#include <stdint.h>

#include "Model.h"
#include "firmware_identity.h"
#include "nvstore.h"
#include "task.h"
#include "modbus.h"

struct save_info
{
    uint16_t can_id;
    uint16_t master_id;
    SaveInfo model_save;
    MotorMode model_mode;
    float position_maximum;
    float velocity_maximum;
    float torque_maximum;
};

extern const uint64_t g_app_build_version;
extern const struct firmware_identity g_app_identity;
extern struct save_info g_save;
extern struct nvstore g_nvstore_handler;
extern struct task g_task_handler;
extern struct modbus_slave g_modbus_node;
extern struct modbus_rtu_slave g_modbus_rtu_slave;

/// @brief Initialize the application BSP and runtime services.
void bsp_init(void);

#endif /* __APP_USER_BSP_H__ */
