/// @file  can_slave.c
/// @brief APP DaMiao CAN slave adapter for the M4310 runtime.

#include "can_slave.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "Model.h"
#include "bsp.h"
#include "can.h"
#include "damiao.h"
#include "task.h"
#include "save.h"
#include "main.h"

#define CAN_SLAVE_TORQUE_CONSTANT (0.14F)
#define CAN_SLAVE_KP_SCALE (CAN_SLAVE_TORQUE_CONSTANT * 3.1415926F)
#define CAN_SLAVE_KD_SCALE (CAN_SLAVE_TORQUE_CONSTANT)
#define CAN_SLAVE_DEFAULT_CAN_ID 1U
#define CAN_SLAVE_DEFAULT_MASTER_ID 0U

/* DM register-access frame opcodes (DM-J4310P-2EC V1.0 manual). */
#define CAN_SLAVE_OP_READ  (0x33U)
#define CAN_SLAVE_OP_WRITE (0x55U)

/* OTA register RIDs — shared numbering with the bootloader can_slave.c so the
 * host uses one register map for both the APP-running and boot-running phases. */
#define CAN_SLAVE_REG_CONTROL  (0x00U)
#define CAN_SLAVE_REG_FW_SIZE  (0x01U)
#define CAN_SLAVE_REG_MD5_BASE (0x05U)
#define CAN_SLAVE_REG_MD5_NUM  (4U)
#define CAN_SLAVE_REG_MODEL_BASE   (0x0AU)
#define CAN_SLAVE_REG_VERSION_BASE (0x30U)

/* CONTROL register value reported while the application is running. Mirrors the
 * Modbus 0x0000 read semantics (modbus_ota.py APP_RUNNING_FLAG). */
#define CAN_SLAVE_APP_RUNNING_FLAG (7U)
/* CONTROL register value that resets the APP into the bootloader. Mirrors the
 * Modbus 0x0000 write=1 -> NVIC_SystemReset() path. */
#define CAN_SLAVE_CMD_ENTER_BOOT (1U)

static struct damiao s_damiao;
static bool s_damiao_enable;

static void can_slave_send(const struct damiao_frame *frame);
static void can_slave_reg_frame(const struct damiao_frame *frame);
static bool can_slave_reg_read(uint8_t rid, uint32_t *value);
static void can_slave_special_frame(uint8_t command);
static void can_slave_command_frame(const struct damiao_command *command);
static bool can_slave_feedback_frame(struct damiao_feedback *feedback);
static struct damiao_config can_slave_config(void);

/// @brief Translate M4310 fault bits to a DaMiao protocol error code.
static uint8_t can_slave_error_code(void)
{
    uint32_t fault = Model_Y.Info.Fault;

    if ((fault & (HardOverVoltage | SoftOverVoltage)) != 0U)
    {
        return 0x8U;
    }
    if ((fault & (HardUnderVoltage | SoftUnderVoltage)) != 0U)
    {
        return 0x9U;
    }
    if ((fault & (HardOverCurrent | SoftOverCurrent)) != 0U)
    {
        return 0xAU;
    }
    if ((fault & MosOverTemperature) != 0U)
    {
        return 0xBU;
    }
    if ((fault & MotorOverTemperature) != 0U)
    {
        return 0xCU;
    }
    if ((fault & MotorSensorLoss) != 0U)
    {
        return 0xDU;
    }
    if ((fault & MotorPhaseLoss) != 0U)
    {
        return 0xEU;
    }

    return s_damiao_enable ? 0x1U : 0x0U;
}

/// @brief Send one DaMiao protocol frame through CAN1.
static void can_slave_send(const struct damiao_frame *frame)
{
    if (frame != NULL)
    {
        can_msg_t msg;

        msg.id = frame->id;
        msg.ide = frame->ide;
        msg.rtr = frame->rtr;
        msg.dlc = frame->dlc;
        (void)memcpy(msg.data, frame->data, sizeof(msg.data));
        (void)can1_send(&msg);
    }
}

/// @brief Handle one broadcast register-access CAN frame addressed to `0x7FF`.
/// @details The APP exposes a minimal register surface over the DM frame format:
///          - read  CONTROL (RID 0x00) -> APP_RUNNING_FLAG (7), so the host can
///            distinguish "APP running" from "bootloader waiting";
///          - read  FW_SIZE (RID 0x01) -> stored firmware length (firmware_info_recv);
///          - read  MD5_0..3 (RID 0x05..0x08) -> stored firmware MD5;
///          - read  MODEL_0..3 (RID 0x0A..0x0D) and VERSION_0..3
///            (RID 0x30..0x33) -> fixed firmware identity strings;
///          - write CONTROL = ENTER_BOOT (1) -> NVIC_SystemReset(), entering the
///            bootloader. Mirrors the Modbus 0x0000 write=1 reset path so the
///            host can jump APP->boot over the same CAN channel it uses to upgrade.
static void can_slave_reg_frame(const struct damiao_frame *frame)
{
    uint8_t lo;
    uint8_t hi;
    uint8_t opcode;
    uint8_t rid;

    if ((frame == NULL) || (frame->dlc < 4U))
    {
        return;
    }

    lo = frame->data[0];
    hi = frame->data[1];
    /* DM addresses a slave by its 11-bit CAN ID encoded in D[0..1]. */
    if ((((uint16_t)hi << 8U) | (uint16_t)lo) != (uint16_t)s_damiao.config.id)
    {
        return;
    }

    opcode = frame->data[2];
    rid = frame->data[3];

    switch (opcode)
    {
    case CAN_SLAVE_OP_READ:
    {
        uint32_t value = 0U;
        if (can_slave_reg_read(rid, &value))
        {
            struct damiao_frame resp;

            resp.id = s_damiao.config.master_id;
            resp.ide = 0U;
            resp.rtr = 0U;
            resp.dlc = 8U;
            resp.data[0] = lo;
            resp.data[1] = hi;
            resp.data[2] = CAN_SLAVE_OP_READ;
            resp.data[3] = rid;
            resp.data[4] = (uint8_t)(value & 0xFFU);
            resp.data[5] = (uint8_t)((value >> 8U) & 0xFFU);
            resp.data[6] = (uint8_t)((value >> 16U) & 0xFFU);
            resp.data[7] = (uint8_t)((value >> 24U) & 0xFFU);
            can_slave_send(&resp);
        }
        break;
    }

    case CAN_SLAVE_OP_WRITE:
    {
        uint32_t value;
        if (frame->dlc < 8U)
        {
            return;
        }
        value = ((uint32_t)frame->data[4]) |
                ((uint32_t)frame->data[5] << 8U) |
                ((uint32_t)frame->data[6] << 16U) |
                ((uint32_t)frame->data[7] << 24U);
        if ((rid == CAN_SLAVE_REG_CONTROL) && (value == CAN_SLAVE_CMD_ENTER_BOOT))
        {
            /* Reset into the bootloader. ACK is best-effort: the frame may be
             * queued in the TX FIFO before the reset takes effect. */
            struct damiao_frame resp;
            resp.id = s_damiao.config.master_id;
            resp.ide = 0U;
            resp.rtr = 0U;
            resp.dlc = 4U;
            resp.data[0] = lo;
            resp.data[1] = hi;
            resp.data[2] = CAN_SLAVE_OP_WRITE;
            resp.data[3] = rid;
            (void)memset(&resp.data[4], 0, 4U);
            can_slave_send(&resp);
            NVIC_SystemReset();
        }
        break;
    }

    default:
        break;
    }
}

/// @brief Read one APP register exposed over the DM CAN register path.
/// @return `true` if the RID is readable; `value` is filled then.
static bool can_slave_reg_read(uint8_t rid, uint32_t *value)
{
    uint32_t image_length = 0UL;
    uint8_t image_md5[16];

    if (value == NULL)
    {
        return false;
    }

    switch (rid)
    {
    case CAN_SLAVE_REG_CONTROL:
        /* Report the running flag so the host sees "APP alive". */
        *value = CAN_SLAVE_APP_RUNNING_FLAG;
        return true;

    case CAN_SLAVE_REG_FW_SIZE:
        firmware_info_recv(&image_length, NULL);
        *value = image_length;
        return true;

    case CAN_SLAVE_REG_MD5_BASE:
    case (CAN_SLAVE_REG_MD5_BASE + 1U):
    case (CAN_SLAVE_REG_MD5_BASE + 2U):
    case (CAN_SLAVE_REG_MD5_BASE + 3U):
        firmware_info_recv(NULL, image_md5);
        (void)memcpy(value, &image_md5[(rid - CAN_SLAVE_REG_MD5_BASE) * 4U], sizeof(uint32_t));
        return true;

    case CAN_SLAVE_REG_MODEL_BASE:
    case (CAN_SLAVE_REG_MODEL_BASE + 1U):
    case (CAN_SLAVE_REG_MODEL_BASE + 2U):
    case (CAN_SLAVE_REG_MODEL_BASE + 3U):
        (void)memcpy(value,
                     &g_app_identity.product_id[(rid - CAN_SLAVE_REG_MODEL_BASE) * 4U],
                     sizeof(uint32_t));
        return true;

    case CAN_SLAVE_REG_VERSION_BASE:
    case (CAN_SLAVE_REG_VERSION_BASE + 1U):
    case (CAN_SLAVE_REG_VERSION_BASE + 2U):
    case (CAN_SLAVE_REG_VERSION_BASE + 3U):
        (void)memcpy(value,
                     &g_app_identity.version[(rid - CAN_SLAVE_REG_VERSION_BASE) * 4U],
                     sizeof(uint32_t));
        return true;

    default:
        return false;
    }
}

/// @brief Handle one DaMiao special command.
static void can_slave_special_frame(uint8_t command)
{
    switch (command)
    {
    case DAMIAO_CMD_ENABLE:
        s_damiao_enable = true;
        Model_U.Command.Mode = g_save.model_mode;
        break;
    case DAMIAO_CMD_DISABLE:
        s_damiao_enable = false;
        Model_U.Command.Mode = StopMode;
        break;
    case DAMIAO_CMD_SET_ZERO:
        Save->PositionZeroPoint += (int32_t)Model_Y.Info.Position;
        save_request();
        break;
    case DAMIAO_CMD_CLR_FAULT:
        Model_U.Command.ResetFault = AllFault;
        break;
    default:
        break;
    }
}

/// @brief Apply one decoded DaMiao command to the M4310 command inputs.
static void can_slave_command_frame(const struct damiao_command *command)
{
    float torque_norm;

    if ((command == NULL) || (!s_damiao_enable))
    {
        return;
    }

    switch (command->type)
    {
    case DAMIAO_COMMAND_MIT:
        if (g_save.model_mode != MitMode)
        {
            return;
        }
        Model_U.Command.Mode = MitMode;
        Model_U.Command.Position = command->position;
        Model_U.Command.Velocity = command->velocity;
        Model_U.Command.Kp = command->kp * CAN_SLAVE_KP_SCALE;
        Model_U.Command.Kd = command->kd * CAN_SLAVE_KD_SCALE;

        torque_norm = command->torque * CAN_SLAVE_TORQUE_CONSTANT;
        if (torque_norm >= 1.0F)
        {
            Model_U.Command.Iq = g_save.torque_maximum;
        }
        else if (torque_norm <= -1.0F)
        {
            Model_U.Command.Iq = -g_save.torque_maximum;
        }
        else
        {
            Model_U.Command.Iq = torque_norm * g_save.torque_maximum;
        }
        break;
    case DAMIAO_COMMAND_POSITION_VELOCITY:
        if (g_save.model_mode != PositionMode)
        {
            return;
        }
        Model_U.Command.Mode = PositionMode;
        Model_U.Command.Position = command->position;
        Model_U.Command.Velocity = command->velocity;
        Model_U.Command.Iq = g_save.torque_maximum;
        break;
    case DAMIAO_COMMAND_VELOCITY:
        if (g_save.model_mode != VelocityMode)
        {
            return;
        }
        Model_U.Command.Mode = VelocityMode;
        Model_U.Command.Iq = g_save.torque_maximum;
        Model_U.Command.Velocity = command->velocity;
        break;
    default:
        break;
    }
}

/// @brief Populate feedback values from the M4310 runtime state.
static bool can_slave_feedback_frame(struct damiao_feedback *feedback)
{
    if (feedback == NULL)
    {
        return false;
    }

    feedback->error_code = can_slave_error_code();
    feedback->position = Model_Y.Info.Position;
    feedback->velocity = Model_Y.Info.Velocity;
    feedback->torque = Model_Y.Info.Idq[1] / 0.9F;
    feedback->mos_temp = Model_Y.Info.MosTemp;
    feedback->motor_temp = Model_Y.Info.MotorTemp;

    return true;
}

/// @brief Build the current DaMiao protocol configuration.
static struct damiao_config can_slave_config(void)
{
    struct damiao_config config;

    config.id = (uint8_t)g_save.can_id;
    config.master_id = g_save.master_id;
    config.position_maximum = g_save.position_maximum;
    config.velocity_maximum = g_save.velocity_maximum;
    config.torque_maximum = g_save.torque_maximum;
    config.on_send = can_slave_send;
    config.on_reg = can_slave_reg_frame;
    config.on_special = can_slave_special_frame;
    config.on_command = can_slave_command_frame;
    config.on_feedback = can_slave_feedback_frame;

    return config;
}

/// @brief Run one periodic CAN slave receive/send cycle.
static void can_slave_on_step(void)
{
    can_msg_t msg;
    struct damiao_frame frame;

    s_damiao.config = can_slave_config();

    while (can1_recv(&msg))
    {
        frame.id = msg.id;
        frame.ide = msg.ide;
        frame.rtr = msg.rtr;
        frame.dlc = msg.dlc;
        (void)memcpy(frame.data, msg.data, sizeof(frame.data));
        (void)damiao_on_recv(&s_damiao, &frame);
    }
}

/// @brief Initialize the DaMiao CAN slave service.
void can_slave_init(void)
{
    if (g_save.can_id == 0U)
    {
        g_save.can_id = CAN_SLAVE_DEFAULT_CAN_ID;
        g_save.master_id = CAN_SLAVE_DEFAULT_MASTER_ID;
    }
    if (g_save.position_maximum == 0.0F)
    {
        g_save.position_maximum = 12.5F;
    }
    if (g_save.velocity_maximum == 0.0F)
    {
        g_save.velocity_maximum = 45.0F;
    }
    if (g_save.torque_maximum == 0.0F)
    {
        g_save.torque_maximum = 7.0F;
    }

    {
        const struct damiao_config config = can_slave_config();
        (void)damiao_init(&s_damiao, &config);
    }

    (void)task_cycle_register(&g_task_handler, &can_slave_on_step, 1);
}
