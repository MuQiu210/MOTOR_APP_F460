#ifndef __FIRMWARE_STORAGE_H__
#define __FIRMWARE_STORAGE_H__

#include <stdbool.h>
#include <stdint.h>

#define FIRMWARE_STORAGE_MAGIC            (0x46494E46UL)
#define FIRMWARE_STORAGE_FORMAT           (1UL)
#define FIRMWARE_SLOT_A                   (0U)
#define FIRMWARE_SLOT_B                   (1U)
#define FIRMWARE_SLOT_STATE_INVALID       (0UL)
#define FIRMWARE_SLOT_STATE_STAGED        (0x5AA55AA5UL)
#define FIRMWARE_SLOT_STATE_VALID         (0xA55AA55AUL)
#define FIRMWARE_SAVE_BANK_INVALID        (0xFFU)

#pragma pack(push, 1)
struct firmware_slot_info
{
    uint32_t length;
    uint8_t md5[16];
    uint32_t state;
};

/* One logical INFO area contains two independently valid physical records. */
struct firmware_storage_info
{
    uint32_t magic;
    uint32_t format;
    uint32_t sequence;
    struct firmware_slot_info slot[2];
    uint32_t save_bank;
    uint32_t reserved[3];
    uint32_t crc32;
};
#pragma pack(pop)

_Static_assert(sizeof(struct firmware_storage_info) == 80U,
               "firmware storage info layout must be 80 bytes");

/// @brief Load the newest valid INFO record, or return a blank record.
/// @return `true` when a valid record was found.
bool firmware_info_load(struct firmware_storage_info *info);

/// @brief Append one INFO record to the inactive INFO sector.
/// @return `true` when the record was written and read back successfully.
bool firmware_info_store(struct firmware_storage_info *info);

/// @brief Read the metadata of the primary APP image in slot A.
void firmware_info_recv(uint32_t *length, uint8_t md5[16]);

/// @brief Read the INFO-selected shared parameter bank.
/// @return `0`, `1`, or `FIRMWARE_SAVE_BANK_INVALID`.
uint8_t firmware_info_save_bank_read(void);

/// @brief Mark one shared parameter bank valid after its data was verified.
/// @param bank_index Bank index, either `0` or `1`.
/// @return `true` when the INFO record was committed.
bool firmware_info_save_bank_write(uint8_t bank_index);

/// @brief Store a verified image descriptor as a pending backup image.
bool firmware_info_stage_backup(uint32_t length, const uint8_t md5[16]);

/// @brief Mark the primary image and backup image as the same valid release.
bool firmware_info_commit_primary(uint32_t length, const uint8_t md5[16]);

#endif /* __FIRMWARE_STORAGE_H__ */
