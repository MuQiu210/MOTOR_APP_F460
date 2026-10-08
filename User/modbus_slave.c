/// @file  modbus_slave.c
/// @brief APP Modbus slave for MD5 read and bootloader-enter command.

#include "modbus_slave.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp.h"

#include "task.h"
#include "flash.h"
#include "uart3.h"

#include "modbus.h"
#include "base.h"
#include "motor.h"
#include "Model.h"
#include "save.h"

#include "main.h"

#define APP_MODBUS_SLAVE_ID (1U)
#define APP_MODBUS_FRAME_GAP_MS (2U)
#define APP_MODBUS_RAW_PWM_WRITE_ENABLE (0)
#define APP_MODBUS_INSIDE_ENCODER_MAP_BASE (0x0500U)
#define APP_MODBUS_OUTSIDE_ENCODER_MAP_BASE (0x0700U)
#define APP_MODBUS_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define APP_MODBUS_INSIDE_ENCODER_MAP_COUNT ((uint16_t)APP_MODBUS_ARRAY_COUNT(Save->InsideEncoderMap))
#define APP_MODBUS_OUTSIDE_ENCODER_MAP_COUNT ((uint16_t)APP_MODBUS_ARRAY_COUNT(Save->OutsideEncoderMap))

static void modbus_read_0x0000_0x00FF(uint16_t addr, uint16_t *data);
static void modbus_read_0x0100_0x01FF(uint16_t addr, uint16_t *data);
static void modbus_read_0x0200_0x02FF(uint16_t addr, uint16_t *data);
static void modbus_read_0x0300_0x03FF(uint16_t addr, uint16_t *data);
static void modbus_read_0x0400_0x04FF(uint16_t addr, uint16_t *data);
static void modbus_read_0x0500_0x06FF(uint16_t addr, uint16_t *data);
static void modbus_read_0x0700_0x08FF(uint16_t addr, uint16_t *data);

static void modbus_write_0x0000_0x00FF(uint16_t addr, uint16_t data);
#if APP_MODBUS_RAW_PWM_WRITE_ENABLE
static void modbus_write_0x0100_0x01FF(uint16_t addr, uint16_t data);
#endif
static void modbus_write_0x0200_0x02FF(uint16_t addr, uint16_t data);
static void modbus_write_0x0400_0x04FF(uint16_t addr, uint16_t data);
static void modbus_write_0x0500_0x06FF(uint16_t addr, uint16_t data);
static void modbus_write_0x0700_0x08FF(uint16_t addr, uint16_t data);

static int16_t *modbus_map_compensation_table(uint16_t addr);

static error_t modbus_read_hook(modbus_read *recv);
static error_t modbus_write_hook(modbus_write *write);

static uint16_t modbus_identity_word(const uint8_t *bytes,
                                     uint16_t base,
                                     uint16_t addr)
{
    uint16_t index = (uint16_t)(addr - base);

    return (uint16_t)(((uint16_t)bytes[index * 2U] << 8U) |
                      (uint16_t)bytes[index * 2U + 1U]);
}

/// @brief Map a compensation-table register address to the backing element.
/// @param addr Target register address.
/// @return Pointer to the mapped table element, or `NULL` when ADDR is not in a table range.
static int16_t *modbus_map_compensation_table(uint16_t addr)
{
    uint16_t offset;

    if (addr >= APP_MODBUS_INSIDE_ENCODER_MAP_BASE)
    {
        offset = (uint16_t)(addr - APP_MODBUS_INSIDE_ENCODER_MAP_BASE);
        if (offset < APP_MODBUS_INSIDE_ENCODER_MAP_COUNT)
        {
            return &Save->InsideEncoderMap[offset];
        }
    }

    if (addr >= APP_MODBUS_OUTSIDE_ENCODER_MAP_BASE)
    {
        offset = (uint16_t)(addr - APP_MODBUS_OUTSIDE_ENCODER_MAP_BASE);
        if (offset < APP_MODBUS_OUTSIDE_ENCODER_MAP_COUNT)
        {
            return &Save->OutsideEncoderMap[offset];
        }
    }

    return NULL;
}

/// @brief Read the system information and firmware identity registers.
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0000_0x00FF(uint16_t addr, uint16_t *data)
{
    uint64_t version_value;
    uint32_t image_length = 0UL;
    uint8_t image_md5[16];

    if (data == NULL)
    {
        return;
    }

    version_value = g_app_identity.build_time;
    if ((addr >= 0x0005U) && (addr <= 0x000EU))
    {
        firmware_info_recv(&image_length, image_md5);
    }

    switch (addr)
    {
    case 0x0000U:
        *data = 7U;
        break;
    case 0x0001U:
    case 0x0002U:
    case 0x0003U:
    case 0x0004U:
    {
        uint32_t shift = 16U * (0x0004U - addr);
        *data = (uint16_t)((version_value >> shift) & 0xFFFFU);
        break;
    }
    case 0x0005U:
    case 0x0006U:
    case 0x0007U:
    case 0x0008U:
    case 0x0009U:
    case 0x000AU:
    case 0x000BU:
    case 0x000CU:
    {
        uint16_t index = (uint16_t)(addr - 0x0005U);
        *data = (uint16_t)(((uint16_t)image_md5[index * 2U] << 8U) |
                           (uint16_t)image_md5[index * 2U + 1U]);
        break;
    }
    case 0x000DU:
    case 0x000EU:
    {
        uint32_t shift = 16U * (0x000EU - addr);
        *data = (uint16_t)((image_length >> shift) & 0xFFFFU);
        break;
    }
    case 0x00F0U:
    case 0x00F1U:
    case 0x00F2U:
    case 0x00F3U:
    case 0x00F4U:
    case 0x00F5U:
    case 0x00F6U:
    case 0x00F7U:
        *data = modbus_identity_word(g_app_identity.product_id, 0x00F0U, addr);
        break;
    case 0x00F8U:
    case 0x00F9U:
    case 0x00FAU:
    case 0x00FBU:
    case 0x00FCU:
    case 0x00FDU:
    case 0x00FEU:
    case 0x00FFU:
        *data = modbus_identity_word(g_app_identity.version, 0x00F8U, addr);
        break;
    default:
        *data = 0U;
        break;
    }
}

/// @brief Read the 0x0100-0x010D runtime monitor registers.
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0100_0x01FF(uint16_t addr, uint16_t *data)
{
    if (data == NULL)
    {
        return;
    }

    switch (addr)
    {
    case 0x0100U:
        *data = IU_ADC;
        break;
    case 0x0101U:
        *data = IV_ADC;
        break;
    case 0x0102U:
        *data = IW_ADC;
        break;
    case 0x0103U:
        *data = HALL2_ADC;
        break;
    case 0x0104U:
        *data = HALL1_ADC;
        break;
    case 0x0105U:
        *data = PWR_24V_DET;
        break;
    case 0x0106U:
        *data = PWR_5V_DET;
        break;
    case 0x0107U:
        *data = MOTOR_TEMP;
        break;
    case 0x0108U:
        *data = MOS_TEMP;
        break;
    case 0x0109U:
    {
        if (PWMOUT_IS_ENABLED())
            *data = 1U;
        else
            *data = 0U;
        break;
    }
    case 0x010AU:
        *data = (uint16_t)CM_TMR4_1->OCCRUL;
        break;
    case 0x010BU:
        *data = (uint16_t)CM_TMR4_1->OCCRVL;
        break;
    case 0x010CU:
        *data = (uint16_t)CM_TMR4_1->OCCRWL;
        break;
    case 0x010DU:
        *data = (uint16_t)(((uint32_t)Model_U.Motor.Angle) >> 16);
        break;
    default:
        *data = 0U;
        break;
    }
}

/// @brief Read the 0x0200-0x0300 Command registers (read/write).
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0200_0x02FF(uint16_t addr, uint16_t *data)
{
    if (data == NULL)
    {
        return;
    }

    switch (addr)
    {
    case 0x0200U:
        *data = (uint16_t)(int16_t)Model_U.Command.Mode;
        break;
    case 0x0201U:
    case 0x0202U:
    {
        uint32_t shift = 16U * (0x0202U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Uq * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0203U:
    case 0x0204U:
    {
        uint32_t shift = 16U * (0x0204U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Iq * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0205U:
    case 0x0206U:
    {
        uint32_t shift = 16U * (0x0206U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Velocity * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0207U:
    case 0x0208U:
    case 0x0209U:
    case 0x020AU:
    {
        uint32_t shift = 16U * (0x020AU - addr);
        *data = (uint16_t)((uint64_t)Model_U.Command.Position >> shift);
        break;
    }
    case 0x020BU:
    case 0x020CU:
    {
        uint32_t shift = 16U * (0x020CU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Kp * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x020DU:
    case 0x020EU:
    {
        uint32_t shift = 16U * (0x020EU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Kd * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x020FU:
    case 0x0210U:
    {
        uint32_t shift = 16U * (0x0210U - addr);
        *data = (uint16_t)((uint32_t)Model_U.Command.ResetFault >> shift);
        break;
    }
    default:
        *data = 0U;
        break;
    }
}

/// @brief Read the 0x0300-0x0400 Info registers (read-only).
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0300_0x03FF(uint16_t addr, uint16_t *data)
{
    if (data == NULL)
    {
        return;
    }

    switch (addr)
    {
    case 0x0300U:
        *data = Model_Y.Enable ? 1U : 0U;
        break;
    case 0x0301U:
    case 0x0302U:
    {
        uint32_t shift = 16U * (0x0302U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Duty[0] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0303U:
    case 0x0304U:
    {
        uint32_t shift = 16U * (0x0304U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Duty[1] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0305U:
    case 0x0306U:
    {
        uint32_t shift = 16U * (0x0306U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Duty[2] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0307U:
    case 0x0308U:
    {
        uint32_t shift = 16U * (0x0308U - addr);
        *data = (uint16_t)((uint32_t)Model_Y.Info.HallAngle >> shift);
        break;
    }
    case 0x0309U:
        *data = (uint16_t)(int16_t)Model_Y.Info.Mode;
        break;
    case 0x030AU:
    case 0x030BU:
    {
        uint32_t shift = 16U * (0x030BU - addr);
        *data = (uint16_t)((uint32_t)Model_Y.Info.Fault >> shift);
        break;
    }
    case 0x030CU:
    case 0x030DU:
    {
        uint32_t shift = 16U * (0x030DU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.MosTemp * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x030EU:
    case 0x030FU:
    {
        uint32_t shift = 16U * (0x030FU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.MotorTemp * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0310U:
    case 0x0311U:
    {
        uint32_t shift = 16U * (0x0311U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Power * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0312U:
    case 0x0313U:
    {
        uint32_t shift = 16U * (0x0313U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Udc * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0314U:
    case 0x0315U:
    {
        uint32_t shift = 16U * (0x0315U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Idc * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0316U:
    case 0x0317U:
    {
        uint32_t shift = 16U * (0x0317U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Udq[0] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0318U:
    case 0x0319U:
    {
        uint32_t shift = 16U * (0x0319U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Udq[1] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x031AU:
    case 0x031BU:
    {
        uint32_t shift = 16U * (0x031BU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Idq[0] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x031CU:
    case 0x031DU:
    {
        uint32_t shift = 16U * (0x031DU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Idq[1] * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x031EU:
    case 0x031FU:
    {
        uint32_t shift = 16U * (0x031FU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_Y.Info.Velocity * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0320U:
    case 0x0321U:
    case 0x0322U:
    case 0x0323U:
    {
        uint32_t shift = 16U * (0x0323U - addr);
        *data = (uint16_t)((uint64_t)Model_Y.Info.Position >> shift);
        break;
    }
    case 0x0324U:
        *data = Model_Y.SaveRequest ? 1U : 0U;
        break;
    default:
        *data = 0U;
        break;
    }
}

/// @brief Read the 0x0400-0x0500 Save registers (read/write).
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0400_0x04FF(uint16_t addr, uint16_t *data)
{
    if (data == NULL)
    {
        return;
    }

    switch (addr)
    {
    case 0x0400U:
        *data = g_save.can_id;
        break;
    case 0x0401U:
        *data = g_save.master_id;
        break;
    case 0x0402U:
        *data = Save->Magic;
        break;
    case 0x0403U:
    case 0x0404U:
    {
        uint32_t shift = 16U * (0x0404U - addr);
        *data = (uint16_t)((uint32_t)Save->ValidBits >> shift);
        break;
    }
    case 0x0405U:
    case 0x0406U:
    {
        uint32_t shift = 16U * (0x0406U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->Rs * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0407U:
    case 0x0408U:
    {
        uint32_t shift = 16U * (0x0408U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->Ldq * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0409U:
        *data = (uint16_t)(int16_t)Save->Pole;
        break;
    case 0x040AU:
    case 0x040BU:
    {
        uint32_t shift = 16U * (0x040BU - addr);
        *data = (uint16_t)((uint32_t)Save->AngleOffset >> shift);
        break;
    }
    case 0x040CU:
    case 0x040DU:
    {
        uint32_t index = addr - 0x040CU;
        *data = Save->HallRangeValue[index];
        break;
    }
    case 0x040EU:
    case 0x040FU:
    {
        uint32_t index = addr - 0x040EU;
        *data = Save->HallMiddleValue[index];
        break;
    }
    case 0x0410U:
    case 0x0411U:
    case 0x0412U:
    {
        uint32_t index = addr - 0x0410U;
        *data = Save->CurrentAdcZeroPoint[index];
        break;
    }
    case 0x0413U:
    case 0x0414U:
    {
        uint32_t shift = 16U * (0x0414U - addr);
        *data = (uint16_t)((uint32_t)Save->PositionZeroPoint >> shift);
        break;
    }
    case 0x0415U:
    case 0x0416U:
    {
        uint32_t shift = 16U * (0x0416U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Kp * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0417U:
    case 0x0418U:
    {
        uint32_t shift = 16U * (0x0418U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Ki * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0419U:
    case 0x041AU:
    {
        uint32_t shift = 16U * (0x041AU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Torque * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x041BU:
    case 0x041CU:
    {
        uint32_t shift = 16U * (0x041CU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Acceleration * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x041DU:
    case 0x041EU:
    {
        uint32_t shift = 16U * (0x041EU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->PositionConfig.Kp * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x041FU:
    case 0x0420U:
    {
        uint32_t shift = 16U * (0x0420U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->PositionConfig.Ki * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0421U:
        *data = (uint16_t)(int16_t)g_save.model_mode;
        break;
    case 0x0422U:
    case 0x0423U:
    {
        uint32_t shift = 16U * (0x0423U - addr);
        uint32_t raw = (uint32_t)(int32_t)(g_save.position_maximum * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0424U:
    case 0x0425U:
    {
        uint32_t shift = 16U * (0x0425U - addr);
        uint32_t raw = (uint32_t)(int32_t)(g_save.velocity_maximum * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    case 0x0426U:
    case 0x0427U:
    {
        uint32_t shift = 16U * (0x0427U - addr);
        uint32_t raw = (uint32_t)(int32_t)(g_save.torque_maximum * 1e6f);
        *data = (uint16_t)(raw >> shift);
        break;
    }
    default:
        *data = 0U;
        break;
    }
}

/// @brief Read the 0x0500-0x0700 Inside Encoder Map registers (read/write).
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0500_0x06FF(uint16_t addr, uint16_t *data)
{
    int16_t *table_entry;

    if (data == NULL)
    {
        return;
    }

    table_entry = modbus_map_compensation_table(addr);
    if (table_entry != NULL)
    {
        *data = (uint16_t)(*table_entry);
    }
    else
    {
        *data = 0U;
    }
}

/// @brief Read the 0x0700-0x0900 Outside Encoder Map registers (read/write).
/// @param addr Target register address.
/// @param data Output register pointer that may be `NULL`.
/// @return None.
static void modbus_read_0x0700_0x08FF(uint16_t addr, uint16_t *data)
{
    int16_t *table_entry;

    if (data == NULL)
    {
        return;
    }

    table_entry = modbus_map_compensation_table(addr);
    if (table_entry != NULL)
    {
        *data = (uint16_t)(*table_entry);
    }
    else
    {
        *data = 0U;
    }
}

/// @brief Write the 0x0000-0x000F system control registers.
/// @param addr Target register address.
/// @param data Register value to write.
/// @return None.
static void modbus_write_0x0000_0x00FF(uint16_t addr, uint16_t data)
{
    switch (addr)
    {
    case 0x0000U:
        if (data == 1U)
        {
            NVIC_SystemReset();
        }
        break;
    default:
        break;
    }
}

/// @brief Write the 0x0100-0x010D PWM control registers.
/// @param addr Target register address.
/// @param data Register value to write.
/// @return None.
#if APP_MODBUS_RAW_PWM_WRITE_ENABLE
static void modbus_write_0x0100_0x01FF(uint16_t addr, uint16_t data)
{
    switch (addr)
    {
    case 0x0109U:
        if (data == 0U)
        {
            PWMOUT_DISABLE();
        }
        else
        {
            PWMOUT_ENABLE();
        }
        break;
    case 0x010AU:
        CM_TMR4_1->OCCRUL = data;
        break;
    case 0x010BU:
        CM_TMR4_1->OCCRVL = data;
        break;
    case 0x010CU:
        CM_TMR4_1->OCCRWL = data;
        break;
    default:
        break;
    }
}
#endif

/// @brief Write the 0x0200-0x0300 Command registers (read/write).
/// @param addr Target register address.
/// @param data Register value to write.
/// @return None.
static void modbus_write_0x0200_0x02FF(uint16_t addr, uint16_t data)
{
    switch (addr)
    {
    case 0x0200U:
        Model_U.Command.Mode = (MotorMode)(int16_t)data;
        break;
    case 0x0201U:
    case 0x0202U:
    {
        uint32_t shift = 16U * (0x0202U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Uq * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Model_U.Command.Uq = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0203U:
    case 0x0204U:
    {
        uint32_t shift = 16U * (0x0204U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Iq * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Model_U.Command.Iq = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0205U:
    case 0x0206U:
    {
        uint32_t shift = 16U * (0x0206U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Velocity * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Model_U.Command.Velocity = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0207U:
    case 0x0208U:
    case 0x0209U:
    case 0x020AU:
    {
        uint32_t shift = 16U * (0x020AU - addr);
        uint64_t raw = (uint64_t)Model_U.Command.Position;
        raw = (raw & ~(0xFFFFULL << shift)) | ((uint64_t)data << shift);
        Model_U.Command.Position = (int64_t)raw;
        break;
    }
    case 0x020BU:
    case 0x020CU:
    {
        uint32_t shift = 16U * (0x020CU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Kp * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Model_U.Command.Kp = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x020DU:
    case 0x020EU:
    {
        uint32_t shift = 16U * (0x020EU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Model_U.Command.Kd * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Model_U.Command.Kd = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x020FU:
    case 0x0210U:
    {
        uint32_t shift = 16U * (0x0210U - addr);
        uint32_t raw = Model_U.Command.ResetFault;
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Model_U.Command.ResetFault = raw;
        break;
    }
    default:
        break;
    }
}

/// @brief Write the 0x0400-0x0500 Save registers (read/write).
/// @param addr Target register address.
/// @param data Register value to write.
/// @return None.
static void modbus_write_0x0400_0x04FF(uint16_t addr, uint16_t data)
{
    switch (addr)
    {
    case 0x0400U:
        g_save.can_id = data;
        break;
    case 0x0401U:
        g_save.master_id = data;
        break;
    case 0x0402U:
        Save->Magic = (uint8_t)data;
        break;
    case 0x0403U:
    case 0x0404U:
    {
        uint32_t shift = 16U * (0x0404U - addr);
        uint32_t raw = Save->ValidBits;
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->ValidBits = raw;
        break;
    }
    case 0x0405U:
    case 0x0406U:
    {
        uint32_t shift = 16U * (0x0406U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->Rs * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->Rs = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0407U:
    case 0x0408U:
    {
        uint32_t shift = 16U * (0x0408U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->Ldq * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->Ldq = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0409U:
        Save->Pole = (int16_t)data;
        break;
    case 0x040AU:
    case 0x040BU:
    {
        uint32_t shift = 16U * (0x040BU - addr);
        uint32_t raw = (uint32_t)Save->AngleOffset;
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->AngleOffset = (int32_t)raw;
        break;
    }
    case 0x040CU:
    case 0x040DU:
    {
        uint32_t index = addr - 0x040CU;
        Save->HallRangeValue[index] = data;
        break;
    }
    case 0x040EU:
    case 0x040FU:
    {
        uint32_t index = addr - 0x040EU;
        Save->HallMiddleValue[index] = data;
        break;
    }
    case 0x0410U:
    case 0x0411U:
    case 0x0412U:
    {
        uint32_t index = addr - 0x0410U;
        Save->CurrentAdcZeroPoint[index] = data;
        break;
    }
    case 0x0413U:
    case 0x0414U:
    {
        uint32_t shift = 16U * (0x0414U - addr);
        uint32_t raw = (uint32_t)Save->PositionZeroPoint;
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->PositionZeroPoint = (int32_t)raw;
        break;
    }
    case 0x0415U:
    case 0x0416U:
    {
        uint32_t shift = 16U * (0x0416U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Kp * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->VelocityConfig.Kp = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0417U:
    case 0x0418U:
    {
        uint32_t shift = 16U * (0x0418U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Ki * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->VelocityConfig.Ki = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0419U:
    case 0x041AU:
    {
        uint32_t shift = 16U * (0x041AU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Torque * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->VelocityConfig.Torque = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x041BU:
    case 0x041CU:
    {
        uint32_t shift = 16U * (0x041CU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->VelocityConfig.Acceleration * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->VelocityConfig.Acceleration = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x041DU:
    case 0x041EU:
    {
        uint32_t shift = 16U * (0x041EU - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->PositionConfig.Kp * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->PositionConfig.Kp = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x041FU:
    case 0x0420U:
    {
        uint32_t shift = 16U * (0x0420U - addr);
        uint32_t raw = (uint32_t)(int32_t)(Save->PositionConfig.Ki * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        Save->PositionConfig.Ki = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0421U:
        g_save.model_mode = (MotorMode)(int16_t)data;
        break;
    case 0x0422U:
    case 0x0423U:
    {
        uint32_t shift = 16U * (0x0423U - addr);
        uint32_t raw = (uint32_t)(int32_t)(g_save.position_maximum * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        g_save.position_maximum = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0424U:
    case 0x0425U:
    {
        uint32_t shift = 16U * (0x0425U - addr);
        uint32_t raw = (uint32_t)(int32_t)(g_save.velocity_maximum * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        g_save.velocity_maximum = (float)(int32_t)raw / 1e6f;
        break;
    }
    case 0x0426U:
    case 0x0427U:
    {
        uint32_t shift = 16U * (0x0427U - addr);
        uint32_t raw = (uint32_t)(int32_t)(g_save.torque_maximum * 1e6f);
        raw = (raw & ~(0xFFFFUL << shift)) | ((uint32_t)data << shift);
        g_save.torque_maximum = (float)(int32_t)raw / 1e6f;
        break;
    }
    default:
        break;
    }
}

/// @brief Write the 0x0500-0x0700 Inside Encoder Map registers (read/write).
/// @param addr Target register address.
/// @param data Register value to write.
/// @return None.
static void modbus_write_0x0500_0x06FF(uint16_t addr, uint16_t data)
{
    int16_t *table_entry;

    table_entry = modbus_map_compensation_table(addr);
    if (table_entry != NULL)
    {
        *table_entry = (int16_t)data;
    }
}

/// @brief Write the 0x0700-0x0900 Outside Encoder Map registers (read/write).
/// @param addr Target register address.
/// @param data Register value to write.
/// @return None.
static void modbus_write_0x0700_0x08FF(uint16_t addr, uint16_t data)
{
    int16_t *table_entry;

    table_entry = modbus_map_compensation_table(addr);
    if (table_entry != NULL)
    {
        *table_entry = (int16_t)data;
    }
}


/// @brief Handle Modbus register read requests.
/// @param recv Read-request descriptor containing the target register.
/// @return `0` on success, otherwise an errno value converted to a Modbus exception.
static error_t modbus_read_hook(modbus_read *recv)
{
    if (recv == NULL)
    {
        return EINVAL;
    }

    if ((recv->cmd != MODBUS_READ_REGS) && (recv->cmd != MODBUS_READ_INPUT_REGS))
    {
        return ENOTSUP;
    }

    recv->data = 0U;
    if ((recv->addr <= 0x000EU) ||
        ((recv->addr >= 0x00F0U) && (recv->addr <= 0x00FFU)))
    {
        modbus_read_0x0000_0x00FF(recv->addr, &recv->data);
    }
    else if ((recv->addr >= 0x0100U) && (recv->addr <= 0x010DU))
    {
        modbus_read_0x0100_0x01FF(recv->addr, &recv->data);
    }
    else if ((recv->addr >= 0x0200U) && (recv->addr <= 0x0210U))
    {
        modbus_read_0x0200_0x02FF(recv->addr, &recv->data);
    }
    else if ((recv->addr >= 0x0300U) && (recv->addr <= 0x0324U))
    {
        modbus_read_0x0300_0x03FF(recv->addr, &recv->data);
    }
    else if ((recv->addr >= 0x0400U) && (recv->addr <= 0x0427U))
    {
        modbus_read_0x0400_0x04FF(recv->addr, &recv->data);
    }
    else if ((recv->addr >= 0x0500U) && (recv->addr <= 0x053fU))
    {
        modbus_read_0x0500_0x06FF(recv->addr, &recv->data);
    }
    else if ((recv->addr >= 0x0700U) && (recv->addr <= 0x073FU))
    {
        modbus_read_0x0700_0x08FF(recv->addr, &recv->data);
    }
    else
    {
        return EFAULT;
    }

    return 0;
}

/// @brief Handle Modbus register write requests.
/// @param write Write-request descriptor containing the target register and value.
/// @return `0` on success, otherwise an errno value converted to a Modbus exception.
static error_t modbus_write_hook(modbus_write *write)
{
    if (write == NULL)
    {
        return EINVAL;
    }

    if ((write->cmd != MODBUS_WRITE_SINGLE_REG) && (write->cmd != MODBUS_WRITE_MUL_REGS))
    {
        return ENOTSUP;
    }

    if (write->addr == 0x0000U)
    {
        modbus_write_0x0000_0x00FF(write->addr, write->data);
    }
#if APP_MODBUS_RAW_PWM_WRITE_ENABLE
    else if ((write->addr >= 0x0109U) && (write->addr <= 0x010CU))
    {
        modbus_write_0x0100_0x01FF(write->addr, write->data);
    }
#endif
    else if ((write->addr >= 0x0200U) && (write->addr <= 0x0210U))
    {
        modbus_write_0x0200_0x02FF(write->addr, write->data);
    }
    else if ((write->addr >= 0x0400U) && (write->addr <= 0x0427U))
    {
        modbus_write_0x0400_0x04FF(write->addr, write->data);
    }
    else if ((write->addr >= 0x0500U) && (write->addr <= 0x053FU))
    {
        modbus_write_0x0500_0x06FF(write->addr, write->data);
    }
    else if ((write->addr >= 0x0700U) && (write->addr <= 0x073FU))
    {
        modbus_write_0x0700_0x08FF(write->addr, write->data);
    }
    else
    {
        return EFAULT;
    }

    return 0;
}

/// @brief Run one periodic Modbus slave receive/step cycle.
/// @details Read up to 8 UART chunks of 32 bytes per call to bound CPU time.
/// @return None.
static void modbus_slave_on_step(void)
{
    uint8_t buffer[32];
    uint32_t length;

    for (uint32_t loop = 0UL; loop < 8UL; loop++)
    {
        length = uart3_recv(buffer, sizeof(buffer));
        if (length == 0UL)
        {
            break;
        }

        (void)modbus_rtu_slave_on_recv(&g_modbus_rtu_slave, buffer, length);
    }
    (void)modbus_rtu_slave_on_step(&g_modbus_rtu_slave, 1UL);
}

/// @brief Initialize the application Modbus slave node.
/// @return None.
void modbus_slave_init(void)
{
    const struct modbus_rtu_slave_config config = {
        .interval = APP_MODBUS_FRAME_GAP_MS,
        .on_send = uart3_send,
        .on_read = NULL,
        .on_write = NULL,
    };

    (void)modbus_rtu_slave_init(&g_modbus_rtu_slave, &config);
    (void)modbus_slave_node_init(&g_modbus_node, APP_MODBUS_SLAVE_ID, modbus_read_hook, modbus_write_hook);
    (void)modbus_rtu_slave_add_node(&g_modbus_rtu_slave, &g_modbus_node);
    (void)task_cycle_register(&g_task_handler, &modbus_slave_on_step, 1);
}
