#ifndef __HC32F448_FLASH_LAYOUT_H__
#define __HC32F448_FLASH_LAYOUT_H__

/*
 * HC32F448 on-chip flash layout used by this product line.
 *
 * Flash total : 256 KiB, 0x00000000 - 0x0003FFFF
 * Loader      : 0x00000000 - 0x00007FFF (32 KiB)
 * APP A       : 0x00008000 - 0x0001FFFF (96 KiB, primary image)
 * APP B       : 0x00020000 - 0x00037FFF (96 KiB, backup image)
 * SAVE A      : 0x00038000 - 0x00039FFF (8 KiB, shared parameter bank)
 * SAVE B      : 0x0003A000 - 0x0003BFFF (8 KiB, shared parameter bank)
 * INFO        : 0x0003C000 - 0x0003FFFF (16 KiB, two metadata sectors)
 */

#define HC32F448_FLASH_BASE_ADDR        (0x00000000UL)
#define HC32F448_FLASH_END_ADDR         (0x0003FFFFUL)
#define HC32F448_FLASH_SIZE             (HC32F448_FLASH_END_ADDR - HC32F448_FLASH_BASE_ADDR + 1UL)
#define HC32F448_FLASH_SECTOR_SIZE      (0x00002000UL)

#define LOADER_BASE_ADDR                (0x00000000UL)
#define LOADER_END_ADDR                 (0x00007FFFUL)
#define LOADER_SIZE                     (LOADER_END_ADDR - LOADER_BASE_ADDR + 1UL)

#define APP_A_BASE_ADDR                 (0x00008000UL)
#define APP_A_END_ADDR                  (0x0001FFFFUL)
#define APP_A_SIZE                      (APP_A_END_ADDR - APP_A_BASE_ADDR + 1UL)
#define APP_B_BASE_ADDR                 (0x00020000UL)
#define APP_B_END_ADDR                  (0x00037FFFUL)
#define APP_B_SIZE                      (APP_B_END_ADDR - APP_B_BASE_ADDR + 1UL)

/* APP is linked for A and is always the executable startup slot. */
#define APP_BASE_ADDR                   APP_A_BASE_ADDR
#define APP_END_ADDR                    APP_A_END_ADDR
#define APP_SIZE                        APP_A_SIZE
#define APP_IDENTITY_ADDR               (APP_A_BASE_ADDR + 0x1000UL)
#define APP_BUILD_VERSION_ADDR          APP_IDENTITY_ADDR
#define APP_IDENTITY_OFFSET             (APP_IDENTITY_ADDR - APP_A_BASE_ADDR)
#define APP_IDENTITY_SIZE               (48UL)

#define SAVE_A_ADDR                     (0x00038000UL)
#define SAVE_A_END_ADDR                 (0x00039FFFUL)
#define SAVE_A_SIZE                     (SAVE_A_END_ADDR - SAVE_A_ADDR + 1UL)

#define SAVE_B_ADDR                     (0x0003A000UL)
#define SAVE_B_END_ADDR                 (0x0003BFFFUL)
#define SAVE_B_SIZE                     (SAVE_B_END_ADDR - SAVE_B_ADDR + 1UL)

#define FIRMWARE_INFO_ADDR              (0x0003C000UL)
#define FIRMWARE_INFO_END_ADDR          (0x0003FFFFUL)
#define FIRMWARE_INFO_SIZE              (FIRMWARE_INFO_END_ADDR - FIRMWARE_INFO_ADDR + 1UL)

#define NVSTORE_BANK0_ADDR              SAVE_A_ADDR
#define NVSTORE_BANK0_END_ADDR          SAVE_A_END_ADDR
#define NVSTORE_BANK0_SIZE              SAVE_A_SIZE
#define NVSTORE_BANK1_ADDR              SAVE_B_ADDR
#define NVSTORE_BANK1_END_ADDR          SAVE_B_END_ADDR
#define NVSTORE_BANK1_SIZE              SAVE_B_SIZE

#endif /* __HC32F448_FLASH_LAYOUT_H__ */
