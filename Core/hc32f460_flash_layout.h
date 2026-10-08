#ifndef __HC32F460_FLASH_LAYOUT_H__
#define __HC32F460_FLASH_LAYOUT_H__

/*
 * F460JETA uses the complete 512 KiB flash and doubles every product region
 * from the original HC32F448JCUI layout: 64 KiB loader, two 192 KiB APP
 * slots, two 16 KiB save banks, and 32 KiB INFO.
 */
#define HC32F460_FLASH_BASE_ADDR        (0x00000000UL)
#define HC32F460_FLASH_END_ADDR         (0x0007FFFFUL)
#define HC32F460_FLASH_SIZE             (HC32F460_FLASH_END_ADDR - HC32F460_FLASH_BASE_ADDR + 1UL)
#define HC32F460_FLASH_SECTOR_SIZE      (0x00002000UL)

#define LOADER_BASE_ADDR                (0x00000000UL)
#define LOADER_END_ADDR                 (0x0000FFFFUL)
#define LOADER_SIZE                     (LOADER_END_ADDR - LOADER_BASE_ADDR + 1UL)

#define APP_A_BASE_ADDR                 (0x00010000UL)
#define APP_A_END_ADDR                  (0x0003FFFFUL)
#define APP_A_SIZE                      (APP_A_END_ADDR - APP_A_BASE_ADDR + 1UL)
#define APP_B_BASE_ADDR                 (0x00040000UL)
#define APP_B_END_ADDR                  (0x0006FFFFUL)
#define APP_B_SIZE                      (APP_B_END_ADDR - APP_B_BASE_ADDR + 1UL)

#define APP_BASE_ADDR                   APP_A_BASE_ADDR
#define APP_END_ADDR                    APP_A_END_ADDR
#define APP_SIZE                        APP_A_SIZE
#define APP_IDENTITY_ADDR               (APP_A_BASE_ADDR + 0x1000UL)
#define APP_BUILD_VERSION_ADDR          APP_IDENTITY_ADDR
#define APP_IDENTITY_OFFSET             (APP_IDENTITY_ADDR - APP_A_BASE_ADDR)
#define APP_IDENTITY_SIZE               (48UL)

#define SAVE_A_ADDR                     (0x00070000UL)
#define SAVE_A_END_ADDR                 (0x00073FFFUL)
#define SAVE_A_SIZE                     (SAVE_A_END_ADDR - SAVE_A_ADDR + 1UL)
#define SAVE_B_ADDR                     (0x00074000UL)
#define SAVE_B_END_ADDR                 (0x00077FFFUL)
#define SAVE_B_SIZE                     (SAVE_B_END_ADDR - SAVE_B_ADDR + 1UL)

#define FIRMWARE_INFO_ADDR              (0x00078000UL)
#define FIRMWARE_INFO_END_ADDR          (0x0007FFFFUL)
#define FIRMWARE_INFO_SIZE              (FIRMWARE_INFO_END_ADDR - FIRMWARE_INFO_ADDR + 1UL)

#define NVSTORE_BANK0_ADDR              SAVE_A_ADDR
#define NVSTORE_BANK0_END_ADDR          SAVE_A_END_ADDR
#define NVSTORE_BANK0_SIZE             SAVE_A_SIZE
#define NVSTORE_BANK1_ADDR              SAVE_B_ADDR
#define NVSTORE_BANK1_END_ADDR          SAVE_B_END_ADDR
#define NVSTORE_BANK1_SIZE              SAVE_B_SIZE

#endif /* __HC32F460_FLASH_LAYOUT_H__ */
