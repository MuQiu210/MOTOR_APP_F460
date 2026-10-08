/// @file  flash.h
/// @brief APP flash erase/program interface.

#ifndef __APP_HARDWARE_FLASH_H__
#define __APP_HARDWARE_FLASH_H__

#include <stdint.h>

/// @brief Initialize flash access state.
/// @details
/// This API keeps EFM control registers in the lock state by default,
/// reducing the risk of accidental erase/program operations.
void flash_init(void);

/// @brief Erase flash sectors covered by [u32_addr, u32_addr + u32_len).
/// @param u32_addr Erase start address.
/// @param u32_len  Erase length in bytes.
/// @retval int32_t Generic error code:
/// - LL_OK: erase success
/// - LL_ERR_INVD_PARAM: address invalid
/// - LL_ERR: erase failed by hardware status
int32_t flash_erase(uint32_t u32_addr, uint32_t u32_len);

/// @brief Program bytes into flash.
/// @param u32_addr Start address in flash.
/// @param pu8_data Source data buffer.
/// @param u32_len  Data length in bytes.
/// @retval int32_t Generic error code:
/// - LL_OK: program success
/// - LL_ERR_INVD_PARAM: input pointer/address invalid
/// - LL_ERR_ADDR_ALIGN: start address not aligned
/// - LL_ERR: program failed by hardware status
int32_t flash_write(uint32_t u32_addr, const uint8_t *pu8_data, uint32_t u32_len);

/// @brief Read flash bytes from target address.
/// @param u32_addr Start address in flash.
/// @param pu8_data Destination buffer.
/// @param u32_len  Data length in bytes.
/// @retval int32_t @ref Generic_Error_Codes.
int32_t flash_read(uint32_t u32_addr, uint8_t *pu8_data, uint32_t u32_len);

#endif /* __APP_HARDWARE_FLASH_H__ */

