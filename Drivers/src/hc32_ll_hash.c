/**
 *******************************************************************************
 * @file  hc32_ll_hash.c
 * @brief This file provides firmware functions to manage the HASH.
 @verbatim
   Change Logs:
   Date             Author          Notes
   2023-05-31       CDT             First version
   2025-01-20       CDT             Optimize HASH_DoCalc function
   2025-06-06       CDT             Add HASH_InputData function
                                    Add HASH_GetMsgDigest function
 @endverbatim
 *******************************************************************************
 * Copyright (C) 2022-2025, Xiaohua Semiconductor Co., Ltd. All rights reserved.
 *
 * This software component is licensed by XHSC under BSD 3-Clause license
 * (the "License"); You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                    opensource.org/licenses/BSD-3-Clause
 *
 *******************************************************************************
 */

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "hc32_ll_hash.h"
#include "hc32_ll_utility.h"

/**
 * @addtogroup LL_Driver
 * @{
 */

/**
 * @defgroup LL_HASH HASH
 * @brief HASH Driver Library
 * @{
 */

#if (LL_HASH_ENABLE == DDL_ON)

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/**
 * @defgroup HASH_Local_Macros HASH Local Macros
 * @{
 */

#define HASH_FLAG_CLR_ALL               (0U)

/**
 * @defgroup HASH_Miscellaneous_Macros HASH Miscellaneous Macros
 * @{
 */
#define HASH_GROUP_SIZE                 (64U)
#define HASH_GROUP_SIZE_WORD            (HASH_GROUP_SIZE / 4U)
#define HASH_LAST_GROUP_SIZE_MAX        (56U)
#define HASH_TIMEOUT                    (6000U)
#define HASH_MSG_DIGEST_SIZE_WORD       (8U)

/**
 * @}
 */

/**
 * @defgroup HASH_Action HASH Action
 * @{
 */
#define HASH_ACTION_START               (HASH_CR_START)
/**
 * @}
 */

#define IS_HASH_PWC_UNLOCKED()          ((CM_PWC->FPRC & PWC_FPRC_FPRCB1) == PWC_FPRC_FPRCB1)

/* HASH reset timeout */
#define HASH_RMU_TIMEOUT                (100UL)

/**
 * @}
 */

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/
typedef struct {
    uint8_t FillBuffer[HASH_GROUP_SIZE];
    uint8_t Position;
    uint32_t Total_Len;
} stc_hash_fill_t;

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
static stc_hash_fill_t m_stcHashFill = {0U};
/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/**
 * @defgroup HASH_Local_Functions HASH Local Functions
 * @{
 */

/**
 * @brief  Writes the input buffer in data register.
 * @param  [in] pu8Data       The buffer for source data
 * @retval None
 */
static void HASH_WriteData(const uint8_t *pu8Data)
{
    uint8_t i;
    __IO uint32_t *regDR = &CM_HASH->DR15;
    const uint32_t *pu32Data = (const uint32_t *)((uint32_t)pu8Data);

    /* Loop through current range. */
    for (i = 0U; i < HASH_GROUP_SIZE_WORD; i++) {
        regDR[i] = __REV(pu32Data[i]);
    }
}

/**
 * @brief  Memory copy.
 * @param  [in] pu8Dest                 Pointer to a destination address.
 * @param  [in] pu8Src                  Pointer to a source address.
 * @param  [in] u32Size                 Data size.
 * @retval None
 */
static void HASH_MemCopy(uint8_t *pu8Dest, const uint8_t *pu8Src, uint32_t u32Size)
{
    uint32_t i = 0UL;
    /* Loop through current range. */
    while (i < u32Size) {
        pu8Dest[i] = pu8Src[i];
        i++;
    }
}

/**
 * @brief  Memory set.
 * @param  [in] pu8Mem                  Pointer to an address.
 * @param  [in] u8Value                 Data value.
 * @param  [in] u32Size                 Data size.
 * @retval None
 */
static void HASH_MemSet(uint8_t *pu8Mem, uint8_t u8Value, uint32_t u32Size)
{
    uint32_t i = 0UL;
    /* Loop through current range. */
    while (i < u32Size) {
        pu8Mem[i] = u8Value;
        i++;
    }
}

/**
 * @brief  Wait for the HASH to stop
 * @param  [in]  u32Action              HASH action. This parameter can be a value of @ref HASH_Action.
 * @retval int32_t:
 *           - LL_OK:                   No errors occurred
 *           - LL_ERR_TIMEOUT:          Works timeout
 */
static int32_t HASH_Wait(uint32_t u32Action)
{
    int32_t i32Ret = LL_OK;
    __IO uint32_t u32TimeCount = 0UL;

    /* Wait for the HASH to stop */
    while (READ_REG32_BIT(CM_HASH->CR, u32Action) != 0UL) {
        /* Check condition and branch. */
        if (u32TimeCount++ > HASH_TIMEOUT) {
            i32Ret = LL_ERR_TIMEOUT;
            break;
        }
    }

    return i32Ret;
}

/**
 * @brief  Hash Parameters reset.
 * @param  None
 * @retval None
 */
static inline void Hash_ParaReset(void)
{
    m_stcHashFill.Position = 0U;
    m_stcHashFill.Total_Len = 0UL;
    HASH_MemSet(m_stcHashFill.FillBuffer, 0U, HASH_GROUP_SIZE);
}

/**
 * @brief  HASH Padding update
 * @param  [in] pu8Data                 The source data buffer
 * @param  [in] u32DataSize             Length of the input buffer in bytes
 * @retval int32_t:
 *           - LL_OK:                   No errors occurred
 *           - LL_ERR_TIMEOUT:          Works timeout
 *           - LL_ERR_INVD_PARAM:       Parameter error
 */
int32_t HASH_InputData(const uint8_t *pu8Data, uint32_t u32DataSize)
{
    int32_t i32Ret = LL_ERR_INVD_PARAM;
    uint32_t i;
    /* Check condition and branch. */
    if ((pu8Data != NULL) && (u32DataSize != 0UL)) {
        /* Loop through current range. */
        for (i = 0U; i < u32DataSize; i++) {
            m_stcHashFill.FillBuffer[m_stcHashFill.Position] = pu8Data[i];
            m_stcHashFill.Position++;
            m_stcHashFill.Total_Len++;
            /* Check condition and branch. */
            if (m_stcHashFill.Position >= HASH_GROUP_SIZE) {
                m_stcHashFill.Position = 0U;
                HASH_WriteData(m_stcHashFill.FillBuffer);
                /* Set first group */
                if ((m_stcHashFill.Total_Len / HASH_GROUP_SIZE) == 1U) {
                    SET_REG32_BIT(CM_HASH->CR, HASH_CR_FST_GRP | HASH_FLAG_CLR_ALL);
                } else {
                    /* Set continuous group. */
                    WRITE_REG32(bCM_HASH->CR_b.FST_GRP, 0U);
                }
                /* Start hash calculating. */
                SET_REG32_BIT(CM_HASH->CR, HASH_CR_START | HASH_FLAG_CLR_ALL);
                i32Ret = HASH_Wait(HASH_ACTION_START);
                /* Check condition and branch. */
                if (i32Ret != LL_OK) {
                    /* reset parameter */
                    Hash_ParaReset();
                    break;
                }
            }
        }
    }

    return i32Ret;
}

/**
 * @brief  Hash last frame padding and write.
 * @param  None
 * @retval None
 */
static void HASH_LastFrmPadWrite(void)
{
    uint64_t *pu64bitlength = (uint64_t *)((uint32_t)&m_stcHashFill.FillBuffer[HASH_LAST_GROUP_SIZE_MAX]);
    /* last frame >= 448bit */
    if (m_stcHashFill.Position >= HASH_LAST_GROUP_SIZE_MAX) {
        /* clear fillbuffer */
        HASH_MemSet(&m_stcHashFill.FillBuffer[m_stcHashFill.Position], 0U, HASH_GROUP_SIZE - (uint32_t)m_stcHashFill.Position);
        /* fill 0b10 */
        m_stcHashFill.FillBuffer[m_stcHashFill.Position] = 0x80U;
        /* write data */
        HASH_WriteData(m_stcHashFill.FillBuffer);
        /* Start hash calculating. */
        SET_REG32_BIT(CM_HASH->CR, HASH_CR_START | HASH_FLAG_CLR_ALL);
        (void)HASH_Wait(HASH_ACTION_START);
        /* clear fillbuffer */
        HASH_MemSet(m_stcHashFill.FillBuffer, 0U, HASH_GROUP_SIZE);
    } else {
        /* clear */
        HASH_MemSet(&m_stcHashFill.FillBuffer[m_stcHashFill.Position], 0U, HASH_GROUP_SIZE - (uint32_t)m_stcHashFill.Position);
        /* fill 0b10 */
        m_stcHashFill.FillBuffer[m_stcHashFill.Position] = 0x80U;
    }
    /* fill length */
    *pu64bitlength = ((uint64_t)__REV(m_stcHashFill.Total_Len * 8U) << 32U);
    /* write lastdata */
    HASH_WriteData(m_stcHashFill.FillBuffer);
    /* Set first group */
    if (m_stcHashFill.Total_Len < HASH_GROUP_SIZE) {
        SET_REG32_BIT(CM_HASH->CR, HASH_CR_FST_GRP | HASH_FLAG_CLR_ALL);
    }
    /* Start hash calculating. */
    SET_REG32_BIT(CM_HASH->CR, HASH_CR_START | HASH_FLAG_CLR_ALL);
    (void)HASH_Wait(HASH_ACTION_START);
    /* reset parameter */
    Hash_ParaReset();
    /* clear FST_GRP flag. */
    WRITE_REG32(bCM_HASH->CR_b.FST_GRP, 0U);
}

/**
 * @brief  HASH Filling data
 * @param  [in] pu8Data                 The source data buffer
 * @param  [in] u32DataSize             Length of the input buffer in bytes
 * @retval int32_t:
 *           - LL_OK:                   No errors occurred
 *           - LL_ERR_TIMEOUT:          Works timeout
 */
static int32_t HASH_DoCalc(const uint8_t *pu8Data, uint32_t u32DataSize)
{
    uint64_t au64FillBuffer[HASH_GROUP_SIZE / 8U] = {0};
    uint8_t  *u8FillBuffer = (uint8_t *)au64FillBuffer;
    uint64_t *pu64bitlength = &au64FillBuffer[HASH_LAST_GROUP_SIZE_MAX / 8U];
    uint32_t DataSize = u32DataSize;
    uint32_t u32Index      = 0U;
    uint8_t  u8FirstGroup  = 1U;
    uint8_t  u8HashEnd     = 0U;
    int32_t  i32Ret;

    /* wait hash calculating stop. */
    i32Ret = HASH_Wait(HASH_ACTION_START);

    /* Loop through current range. */
    while ((i32Ret == LL_OK) && (u8HashEnd == 0U)) {
        /* Check condition and branch. */
        if (DataSize >= HASH_GROUP_SIZE) {
            HASH_WriteData(&pu8Data[u32Index]);
            DataSize -= HASH_GROUP_SIZE;
            u32Index += HASH_GROUP_SIZE;
        } else if (DataSize >= HASH_LAST_GROUP_SIZE_MAX) {
            /* last frame >= 448bit */
            HASH_MemSet(u8FillBuffer, 0U, HASH_GROUP_SIZE);
            HASH_MemCopy(u8FillBuffer, &pu8Data[u32Index], DataSize);
            /* fill 0b10 */
            u8FillBuffer[DataSize] = 0x80U;
            HASH_WriteData(u8FillBuffer);
            DataSize = 0U;
        } else {
            /* last frame */
            HASH_MemSet(u8FillBuffer, 0U, HASH_GROUP_SIZE);
            /* last frame < 448bit */
            if (DataSize > 0U) {
                HASH_MemCopy(u8FillBuffer, &pu8Data[u32Index], DataSize);
            }
            /* fill 10 */
            u8FillBuffer[DataSize] = 0x80U;
            /* big/little endian convert */
            *pu64bitlength = ((uint64_t)__REV(u32DataSize * 8U) << 32U);
            HASH_WriteData(u8FillBuffer);
            u8HashEnd = 1U;
        }

        /* First group and last group check */
        /* check if first group */
        if (u8FirstGroup != 0U) {
            u8FirstGroup = 0U;
            /* Set first group. */
            WRITE_REG32(bCM_HASH->CR_b.FST_GRP, 1U);
        } else {
            /* Set continuous group. */
            WRITE_REG32(bCM_HASH->CR_b.FST_GRP, 0U);
        }
        /* Start hash calculating. */
        WRITE_REG32(bCM_HASH->CR_b.START, 1U);
        i32Ret = HASH_Wait(HASH_ACTION_START);
    }

    return i32Ret;
}

/**
 * @brief  Read message digest.
 * @param  [out] pu8MsgDigest           Buffer for message digest.
 * @retval None
 */
static void HASH_ReadMsgDigest(uint8_t *pu8MsgDigest)
{
    uint8_t i;
    __IO uint32_t *regHR = &CM_HASH->HR7;
    uint32_t *pu32MsgDigest = (uint32_t *)((uint32_t)pu8MsgDigest);

    /* Loop through current range. */
    for (i = 0U; i < HASH_MSG_DIGEST_SIZE_WORD; i++) {
        pu32MsgDigest[i] = __REV(regHR[i]);
    }
}

/**
 * @}
 */

/**
 * @defgroup HASH_Global_Functions HASH Global Functions
 * @{
 */

/**
 * @brief  De-initializes HASH.
 * @param  None
 * @retval int32_t:
 *           - LL_OK:                   No error occurred.
 *           - LL_ERR_TIMEOUT:          Works timeout.
 */
int32_t HASH_DeInit(void)
{
    int32_t i32Ret = LL_OK;

    __IO uint32_t u32TimeOut = 0U;

    /* Check parameters */
    DDL_ASSERT(IS_HASH_PWC_UNLOCKED());
    /* Reset HASH module */
    CLR_REG32_BIT(CM_RMU->FRST0, RMU_FRST0_HASH);
    /* Ensure reset procedure is completed */
    while (RMU_FRST0_HASH != READ_REG32_BIT(CM_RMU->FRST0, RMU_FRST0_HASH)) {
        u32TimeOut++;
        /* Check condition and branch. */
        if (u32TimeOut > HASH_RMU_TIMEOUT) {
            i32Ret = LL_ERR_TIMEOUT;
            break;
        }
    }
    /* reset parameter */
    Hash_ParaReset();

    return i32Ret;
}

/**
 * @brief  HASH calculate.
 * @param  [in]  pu8SrcData             Pointer to the source data buffer.
 * @param  [in]  u32SrcDataSize         Length of the source data buffer in bytes.
 * @param  [out] pu8MsgDigest           Buffer of the digest. The size must be 32 bytes.
 * @retval int32_t:
 *           - LL_OK:                   No errors occurred.
 *           - LL_ERR_INVD_PARAM:       Parameter error.
 *           - LL_ERR_TIMEOUT:          Works timeout.
 */
int32_t HASH_Calculate(const uint8_t *pu8SrcData, uint32_t u32SrcDataSize, uint8_t *pu8MsgDigest)
{
    int32_t i32Ret = LL_ERR_INVD_PARAM;

    /* Check condition and branch. */
    if ((pu8SrcData != NULL) && (u32SrcDataSize != 0UL) && (pu8MsgDigest != NULL)) {
        /* Set HASH mode */
        /* Filling data and Start hash calculating */
        i32Ret = HASH_DoCalc(pu8SrcData, u32SrcDataSize);
        /* Check condition and branch. */
        if (i32Ret == LL_OK) {
            /* Get the message digest result */
            HASH_ReadMsgDigest(pu8MsgDigest);
        }
    }

    return i32Ret;
}

/**
 * @brief  Provides the message digest result.
 * @param  [out] pu8MsgDigest           Buffer for message digest.
 * @retval None
 */
void HASH_GetMsgDigest(uint8_t *pu8MsgDigest)
{
    /* Check whether the HASH_InputData function is used */
    if (m_stcHashFill.Total_Len != 0UL) {
        HASH_LastFrmPadWrite();
    }
    HASH_ReadMsgDigest(pu8MsgDigest);
}

/**
 * @}
 */

#endif /* LL_HASH_ENABLE */

/**
 * @}
 */

/**
 * @}
 */
/*******************************************************************************
 * EOF (not truncated)
 ******************************************************************************/