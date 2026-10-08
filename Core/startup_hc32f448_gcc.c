/**
 *******************************************************************************
 * @file  startup_hc32f448_gcc.c
 * @brief GNU startup and vector table for HC32F448.
 *******************************************************************************
 */

#include <stdint.h>
#include "system_hc32f448.h"

typedef void (*isr_handler_t)(void);

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

int main(void);
void __libc_init_array(void);

void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ000_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ001_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ002_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ003_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ004_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ005_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ006_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ007_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ008_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ009_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ010_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ011_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ012_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ013_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ014_Handler(void) __attribute__((weak, alias("Default_Handler")));
void IRQ015_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT00_SWINT16_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT01_SWINT17_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT02_SWINT18_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT03_SWINT19_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT04_SWINT20_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT05_SWINT21_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT06_SWINT22_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT07_SWINT23_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT08_SWINT24_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT09_SWINT25_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT10_SWINT26_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT11_SWINT27_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT12_SWINT28_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT13_SWINT29_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT14_SWINT30_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTINT15_SWINT31_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Error_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_TC0_BTC0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_TC1_BTC1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_TC2_BTC2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_TC3_BTC3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_TC4_BTC4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_TC5_BTC5_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EFM_PEError_ReadCol_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EFM_OpEnd_Handler(void) __attribute__((weak, alias("Default_Handler")));
void QSPI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DCU1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DCU2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DCU3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DCU4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Error_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_TC0_BTC0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_TC1_BTC1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_TC2_BTC2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_TC3_BTC3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_TC4_BTC4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_TC5_BTC5_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR0_1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR0_2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CLK_XtalStop_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PWC_WKTM_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SWDT_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_1_GCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_1_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_1_Dte_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_1_SCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_1_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_1_Cmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_2_GCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_2_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_2_Dte_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR6_2_SCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_2_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_2_Cmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_3_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_3_Cmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_4_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_4_Cmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_1_GCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_1_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_1_Reload_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_1_SCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_2_GCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_2_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_2_Reload_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_2_SCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_3_GCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_3_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_3_Reload_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMR4_3_SCmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CMP1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CMP2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CMP3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CMP4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_TxComplete_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_TxComplete_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SPI1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_5_Ovf_Udf_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TMRA_5_Cmp_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EVENT_PORT1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EVENT_PORT2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EVENT_PORT3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EVENT_PORT4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART3_TxComplete_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART4_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART4_TxComplete_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SPI2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SPI3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EMB_GR0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EMB_GR1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EMB_GR2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EMB_GR3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART5_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART5_TxComplete_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART6_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART6_TxComplete_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MCAN1_INT0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MCAN1_INT1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MCAN2_INT0_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MCAN2_INT1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_WKUP_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PWC_LVD1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PWC_LVD2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void FCM_Handler(void) __attribute__((weak, alias("Default_Handler")));
void WDT_Handler(void) __attribute__((weak, alias("Default_Handler")));
void CTC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void ADC1_Handler(void) __attribute__((weak, alias("Default_Handler")));
void ADC2_Handler(void) __attribute__((weak, alias("Default_Handler")));
void ADC3_Handler(void) __attribute__((weak, alias("Default_Handler")));
void TRNG_Handler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((used, section(".isr_vector")))
const isr_handler_t __Vectors[] = {
    (isr_handler_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    (isr_handler_t)0,
    (isr_handler_t)0,
    (isr_handler_t)0,
    (isr_handler_t)0,
    SVC_Handler,
    DebugMon_Handler,
    (isr_handler_t)0,
    PendSV_Handler,
    SysTick_Handler,
    IRQ000_Handler,
    IRQ001_Handler,
    IRQ002_Handler,
    IRQ003_Handler,
    IRQ004_Handler,
    IRQ005_Handler,
    IRQ006_Handler,
    IRQ007_Handler,
    IRQ008_Handler,
    IRQ009_Handler,
    IRQ010_Handler,
    IRQ011_Handler,
    IRQ012_Handler,
    IRQ013_Handler,
    IRQ014_Handler,
    IRQ015_Handler,
    EXTINT00_SWINT16_Handler,
    EXTINT01_SWINT17_Handler,
    EXTINT02_SWINT18_Handler,
    EXTINT03_SWINT19_Handler,
    EXTINT04_SWINT20_Handler,
    EXTINT05_SWINT21_Handler,
    EXTINT06_SWINT22_Handler,
    EXTINT07_SWINT23_Handler,
    EXTINT08_SWINT24_Handler,
    EXTINT09_SWINT25_Handler,
    EXTINT10_SWINT26_Handler,
    EXTINT11_SWINT27_Handler,
    EXTINT12_SWINT28_Handler,
    EXTINT13_SWINT29_Handler,
    EXTINT14_SWINT30_Handler,
    EXTINT15_SWINT31_Handler,
    DMA1_Error_Handler,
    DMA1_TC0_BTC0_Handler,
    DMA1_TC1_BTC1_Handler,
    DMA1_TC2_BTC2_Handler,
    DMA1_TC3_BTC3_Handler,
    DMA1_TC4_BTC4_Handler,
    DMA1_TC5_BTC5_Handler,
    EFM_PEError_ReadCol_Handler,
    EFM_OpEnd_Handler,
    QSPI_Handler,
    DCU1_Handler,
    DCU2_Handler,
    DCU3_Handler,
    DCU4_Handler,
    DMA2_Error_Handler,
    DMA2_TC0_BTC0_Handler,
    DMA2_TC1_BTC1_Handler,
    DMA2_TC2_BTC2_Handler,
    DMA2_TC3_BTC3_Handler,
    DMA2_TC4_BTC4_Handler,
    DMA2_TC5_BTC5_Handler,
    TMR0_1_Handler,
    TMR0_2_Handler,
    RTC_Handler,
    CLK_XtalStop_Handler,
    PWC_WKTM_Handler,
    SWDT_Handler,
    TMR6_1_GCmp_Handler,
    TMR6_1_Ovf_Udf_Handler,
    TMR6_1_Dte_Handler,
    TMR6_1_SCmp_Handler,
    TMRA_1_Ovf_Udf_Handler,
    TMRA_1_Cmp_Handler,
    TMR6_2_GCmp_Handler,
    TMR6_2_Ovf_Udf_Handler,
    TMR6_2_Dte_Handler,
    TMR6_2_SCmp_Handler,
    TMRA_2_Ovf_Udf_Handler,
    TMRA_2_Cmp_Handler,
    TMRA_3_Ovf_Udf_Handler,
    TMRA_3_Cmp_Handler,
    TMRA_4_Ovf_Udf_Handler,
    TMRA_4_Cmp_Handler,
    TMR4_1_GCmp_Handler,
    TMR4_1_Ovf_Udf_Handler,
    TMR4_1_Reload_Handler,
    TMR4_1_SCmp_Handler,
    TMR4_2_GCmp_Handler,
    TMR4_2_Ovf_Udf_Handler,
    TMR4_2_Reload_Handler,
    TMR4_2_SCmp_Handler,
    TMR4_3_GCmp_Handler,
    TMR4_3_Ovf_Udf_Handler,
    TMR4_3_Reload_Handler,
    TMR4_3_SCmp_Handler,
    I2C1_Handler,
    I2C2_Handler,
    CMP1_Handler,
    CMP2_Handler,
    CMP3_Handler,
    CMP4_Handler,
    USART1_Handler,
    USART1_TxComplete_Handler,
    USART2_Handler,
    USART2_TxComplete_Handler,
    SPI1_Handler,
    TMRA_5_Ovf_Udf_Handler,
    TMRA_5_Cmp_Handler,
    EVENT_PORT1_Handler,
    EVENT_PORT2_Handler,
    EVENT_PORT3_Handler,
    EVENT_PORT4_Handler,
    USART3_Handler,
    USART3_TxComplete_Handler,
    USART4_Handler,
    USART4_TxComplete_Handler,
    SPI2_Handler,
    SPI3_Handler,
    EMB_GR0_Handler,
    EMB_GR1_Handler,
    EMB_GR2_Handler,
    EMB_GR3_Handler,
    USART5_Handler,
    USART5_TxComplete_Handler,
    USART6_Handler,
    USART6_TxComplete_Handler,
    MCAN1_INT0_Handler,
    MCAN1_INT1_Handler,
    MCAN2_INT0_Handler,
    MCAN2_INT1_Handler,
    USART1_WKUP_Handler,
    PWC_LVD1_Handler,
    PWC_LVD2_Handler,
    FCM_Handler,
    WDT_Handler,
    CTC_Handler,
    ADC1_Handler,
    ADC2_Handler,
    ADC3_Handler,
    TRNG_Handler,
};

const isr_handler_t * const __Vectors_End = &__Vectors[sizeof(__Vectors) / sizeof(__Vectors[0])];
const uint32_t __Vectors_Size = (uint32_t)((uintptr_t)__Vectors_End - (uintptr_t)__Vectors);

/// @brief Handle the reset exception and initialize the runtime environment.
/// @param None
/// @retval None
void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    /* Clear SRAMC status flags: same behavior as vendor startup. */
    *(volatile uint32_t *)(0x40050810UL) = 0x1F8UL;

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    /* Loop through current range. */
    while (dst < &_ebss) {
        *dst++ = 0U;
    }

    SystemInit();
    __libc_init_array();
    (void)main();

    /* Loop through current range. */
    while (1) {
    }
}

/// @brief Handle unexpected interrupts and exceptions.
/// @param None
/// @retval None
void Default_Handler(void)
{
    /* Loop through current range. */
    while (1) {
    }
}