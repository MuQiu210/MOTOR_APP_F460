/// @file  adc.c
/// @brief ADC peripheral initialization.

#include "adc.h"
#include "timer4.h"

/* ADC1 EOCA is the single fast-loop interrupt entry for the application. */
extern void ADC1_Handler(void);

/* Legacy 12-bit AWD thresholds retained from MOTOR_APP.
 * Recalculate them after the F460 current-sense analog front end is measured. */
#define ADC_CURRENT_AWD_LOW  ((uint16_t)(2048 - 2000))
#define ADC_CURRENT_AWD_HIGH ((uint16_t)(2048 + 2000))


/// @brief Initialize the ADC peripherals.
/// @return None.
void adc_init(void)
{
    stc_adc_init_t       stc_adc_init;
    stc_adc_awd_config_t stc_awd;

    /* 1. F460 provides ADC1 and ADC2; they are synchronised as one sample set. */
    FCG_Fcg3PeriphClockCmd(FCG3_PERIPH_ADC1 | FCG3_PERIPH_ADC2, ENABLE);

    /* 2. Configure ADC parameters. */
    (void)ADC_StructInit(&stc_adc_init);
    stc_adc_init.u16ScanMode   = ADC_MD_SEQA_SINGLESHOT;
    stc_adc_init.u16Resolution = ADC_RESOLUTION_12BIT;
    stc_adc_init.u16DataAlign  = ADC_DATAALIGN_RIGHT;

    /* 3. Initialise ADC (AutoClear must come after Init to avoid CLREN overwrite). */
    (void)ADC_Init(CM_ADC1, &stc_adc_init);
    (void)ADC_Init(CM_ADC2, &stc_adc_init);
    /* The XHCode configuration does not enable data-register auto-clear. */

    /* 4. Channel remapping. */
    ADC_ChRemap(CM_ADC1, ADC_CH0, ADC1_PIN_PA6);
    ADC_ChRemap(CM_ADC1, ADC_CH1, ADC1_PIN_PA3);
    ADC_ChRemap(CM_ADC1, ADC_CH2, ADC1_PIN_PA1);
    ADC_ChRemap(CM_ADC1, ADC_CH3, ADC1_PIN_PA2);
    ADC_ChRemap(CM_ADC1, ADC_CH5, ADC1_PIN_PB1);
    ADC_ChRemap(CM_ADC1, ADC_CH8, ADC1_PIN_PA7);
    ADC_ChRemap(CM_ADC1, ADC_CH9, ADC1_PIN_PA1);
    ADC_ChRemap(CM_ADC2, ADC_CH0, ADC2_PIN_PA7);
    ADC_ChRemap(CM_ADC2, ADC_CH1, ADC2_PIN_PA4);
    ADC_ChRemap(CM_ADC2, ADC_CH2, ADC2_PIN_PB1);
    ADC_ChRemap(CM_ADC2, ADC_CH3, ADC2_PIN_PB0);
    ADC_ChRemap(CM_ADC2, ADC_CH4, ADC2_PIN_PA5);

    /* 5. Enable sequence A channels. */
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH0, ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH1, ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH2, ENABLE);
    ADC_ChCmd(CM_ADC1, ADC_SEQ_A, ADC_CH3, ENABLE);
    ADC_ChCmd(CM_ADC2, ADC_SEQ_A, ADC_CH0, ENABLE);
    ADC_ChCmd(CM_ADC2, ADC_SEQ_A, ADC_CH1, ENABLE);
    ADC_ChCmd(CM_ADC2, ADC_SEQ_A, ADC_CH2, ENABLE);
    /* ADC2 sequence A contains the remaining five application signals. */
    ADC_ChCmd(CM_ADC2, ADC_SEQ_A, ADC_CH3, ENABLE);
    ADC_ChCmd(CM_ADC2, ADC_SEQ_A, ADC_CH4, ENABLE);

    /* 6. Trigger: ADC1 SEQ_A ← TMR4_1 SCMP4 hardware event. */
    ADC_TriggerConfig(CM_ADC1, ADC_SEQ_A, ADC_HARDTRIG_EVT0);
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_AOS, ENABLE);
    AOS_SetTriggerEventSrc(AOS_ADC1_0, EVT_SRC_TMR4_1_SCMP4);
    ADC_TriggerCmd(CM_ADC1, ADC_SEQ_A, ENABLE);

    /* 7. AWD0: overcurrent — flag when sample is OUTSIDE [Low, High].
     * F460 monitors ADC1 CH0 (IW), ADC2 CH0 (IV), and ADC2 CH3 (IU). */
    stc_awd.u16WatchdogMode  = ADC_AWD_MD_CMP_OUT;
    stc_awd.u16LowThreshold  = ADC_CURRENT_AWD_LOW;
    stc_awd.u16HighThreshold = ADC_CURRENT_AWD_HIGH;
    (void)ADC_AWD_Config(CM_ADC1, ADC_AWD0, ADC_CH0, &stc_awd);
    (void)ADC_AWD_Config(CM_ADC2, ADC_AWD0, ADC_CH0, &stc_awd);
    /* AWD0 observes CH0 on both ADCs. ADC1 owns the fast-loop interrupt;
       ADC2's flag is sampled by ADC1_Handler after synchronized conversion. */
    ADC_AWD_Cmd(CM_ADC1, ADC_AWD0, ENABLE);
    ADC_AWD_Cmd(CM_ADC2, ADC_AWD0, ENABLE);
    /* F460 has no ADC3. The third phase-current input is ADC2 CH3 (PB0),
       so add it to the same ADC2 AWD unit used for ADC2 CH0 (PA7). */
    ADC_AWD_SelectCh(CM_ADC2, ADC_AWD0, ADC_CH3);
    ADC_AWD_IntCmd(CM_ADC1, ADC_AWD_INT_SEQA, ENABLE);

    /* 8. Sync mode: ADC1+2 single parallel, ADC1 as master trigger.
     *    Must be configured before NVIC enables so no spurious IRQ fires first. */
    ADC_SyncModeConfig(ADC_SYNC_ADC1_ADC2, ADC_SYNC_SINGLE_PARALLEL_TRIG, 1U);
    ADC_SyncModeCmd(ENABLE);

    /* 9. ADC1 EOCA interrupt, priority 00 (highest). */
    ADC_IntCmd(CM_ADC1, ADC_INT_EOCA, ENABLE);
    ADC_ClearStatus(CM_ADC1, ADC_FLAG_EOCA);
    ADC_AWD_ClearStatus(CM_ADC1, ADC_AWD_FLAG_CH0);
    ADC_AWD_ClearStatus(CM_ADC2, ADC_AWD_FLAG_CH0);
    ADC_AWD_ClearStatus(CM_ADC2, ADC_AWD_FLAG_CH3);

    /* F460 routes peripheral sources through INTC before they reach NVIC. */
    stc_irq_signin_config_t stc_irq;
    stc_irq.enIntSrc = INT_SRC_ADC1_EOCA;
    stc_irq.enIRQn = INT116_IRQn;
    stc_irq.pfnCallback = &ADC1_Handler;
    (void)INTC_IrqSignIn(&stc_irq);
    NVIC_ClearPendingIRQ(INT116_IRQn);
    NVIC_SetPriority(INT116_IRQn, DDL_IRQ_PRIO_00);
    NVIC_EnableIRQ(INT116_IRQn);
}
