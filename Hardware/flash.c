/// @file  flash.c
/// @brief APP flash operation implementation.

#include "flash.h"

#include "main.h"

/// @brief Check whether address range is in main flash.
/// @param u32_addr Start address.
/// @param u32_len  Data length.
/// @retval int32_t @ref Generic_Error_Codes.
static int32_t flash_check_main_range(uint32_t u32_addr, uint32_t u32_len)
{
    /* Check condition and branch. */
    if (0UL == u32_len)
    {
        return LL_OK;
    }

    /* Check condition and branch. */
    if (u32_addr > EFM_END_ADDR)
    {
        return LL_ERR_INVD_PARAM;
    }

    /* Check condition and branch. */
    if ((u32_len - 1UL) > (EFM_END_ADDR - u32_addr))
    {
        return LL_ERR_INVD_PARAM;
    }

    return LL_OK;
}

/// @brief Convert any address in a sector to sector start address.
/// @param u32_addr Address inside the target sector.
/// @return Sector start address.
static uint32_t flash_get_sector_start_addr(uint32_t u32_addr)
{
    return ((u32_addr / EFM_SECTOR_SIZE) * EFM_SECTOR_SIZE);
}

/// @brief Unlock EFM control registers before erase/program.
/// @param None
/// @retval None
static void flash_op_unlock(void)
{
    EFM_REG_Unlock();
    EFM_FWMC_Cmd(ENABLE);
}

/// @brief Re-lock EFM control registers after erase/program.
/// @param None
/// @retval None
static void flash_op_lock(void)
{
    EFM_FWMC_Cmd(DISABLE);
    EFM_REG_Lock();
}

/// @brief Initialize the flash access state used by the application.
/// @param None
/// @retval None
void flash_init(void)
{
    flash_op_lock();
}

/// @brief Erase flash sectors that cover the specified address range.
/// @param u32_addr Erase start address.
/// @param u32_len Erase length in bytes.
/// @return Erase status code.
int32_t flash_erase(uint32_t u32_addr, uint32_t u32_len)
{
    int32_t i32_ret;
    uint32_t u32_start_addr;
    uint32_t u32_end_addr;
    uint32_t u32_erase_addr;

    i32_ret = flash_check_main_range(u32_addr, u32_len);
    /* Check condition and branch. */
    if (LL_OK != i32_ret)
    {
        return i32_ret;
    }
    /* Check condition and branch. */
    if (0UL == u32_len)
    {
        return LL_OK;
    }

    u32_start_addr = flash_get_sector_start_addr(u32_addr);
    u32_end_addr = flash_get_sector_start_addr(u32_addr + u32_len - 1UL);

    flash_op_unlock();
    i32_ret = LL_OK;
    /* Loop through current range. */
    for (u32_erase_addr = u32_start_addr; u32_erase_addr <= u32_end_addr; u32_erase_addr += EFM_SECTOR_SIZE)
    {
        i32_ret = EFM_SectorErase(u32_erase_addr);
        /* Check condition and branch. */
        if (LL_OK != i32_ret)
        {
            break;
        }
    }
    flash_op_lock();

    return i32_ret;
}

/// @brief Program data to flash memory.
/// @param u32_addr Write start address.
/// @param pu8_data Source buffer pointer.
/// @param u32_len Number of bytes to write.
/// @return Program status code.
int32_t flash_write(uint32_t u32_addr, const uint8_t *pu8_data, uint32_t u32_len)
{
    int32_t i32_ret;

    /* Check condition and branch. */
    if ((NULL == pu8_data) && (u32_len > 0UL))
    {
        return LL_ERR_INVD_PARAM;
    }

    /* Check condition and branch. */
    if (0UL == u32_len)
    {
        return LL_OK;
    }

    i32_ret = flash_check_main_range(u32_addr, u32_len);
    /* Check condition and branch. */
    if (LL_OK != i32_ret)
    {
        return i32_ret;
    }

    /* Check condition and branch. */
    if ((u32_addr % sizeof(uint32_t)) != 0UL)
    {
        return LL_ERR_ADDR_ALIGN;
    }

    flash_op_unlock();
    i32_ret = EFM_Program(u32_addr, pu8_data, u32_len);
    flash_op_lock();

    return i32_ret;
}

/// @brief Read data from flash memory.
/// @param u32_addr Read start address.
/// @param pu8_data Destination buffer pointer.
/// @param u32_len Number of bytes to read.
/// @return Read status code.
int32_t flash_read(uint32_t u32_addr, uint8_t *pu8_data, uint32_t u32_len)
{
    int32_t i32_ret;

    /* Check condition and branch. */
    if ((NULL == pu8_data) && (u32_len > 0UL))
    {
        return LL_ERR_INVD_PARAM;
    }
    /* Check condition and branch. */
    if (0UL == u32_len)
    {
        return LL_OK;
    }

    i32_ret = flash_check_main_range(u32_addr, u32_len);
    /* Check condition and branch. */
    if (LL_OK != i32_ret)
    {
        return i32_ret;
    }

    return EFM_ReadByte(u32_addr, pu8_data, u32_len);
}
