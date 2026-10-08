#ifndef __FIRMWARE_IDENTITY_H__
#define __FIRMWARE_IDENTITY_H__

#include <stdint.h>

#define FIRMWARE_IDENTITY_STRING_SIZE (16U)
#define FIRMWARE_IDENTITY_MAGIC_SIZE  (4U)
#define FIRMWARE_IDENTITY_TOTAL_SIZE  (48U)

/* Keep these values synchronized with MOTOR_BOOT for one product variant. */
#define FIRMWARE_IDENTITY_MAGIC  "FID1"
#define FIRMWARE_PRODUCT_ID      "NGE-4310p"
#define APP_FIRMWARE_VERSION     "APP-V3.0.0"

#pragma pack(push, 1)
struct firmware_identity
{
    uint64_t build_time;
    uint8_t magic[FIRMWARE_IDENTITY_MAGIC_SIZE];
    uint8_t product_id[FIRMWARE_IDENTITY_STRING_SIZE];
    uint8_t version[FIRMWARE_IDENTITY_STRING_SIZE];
    uint8_t reserved[4];
};
#pragma pack(pop)

_Static_assert(sizeof(struct firmware_identity) == FIRMWARE_IDENTITY_TOTAL_SIZE,
               "firmware identity layout must be 48 bytes");

#endif /* __FIRMWARE_IDENTITY_H__ */
