/// @file  timer4.c
/// @brief TMR4 peripheral initialization.

#include "timer4.h"
#include "hc32_ll.h"

/// @brief Initialize TMR4 for center-aligned PWM generation.
/// @return None.
void timer4_init(void)
{
   un_tmr4_oc_ocmrh_t ocmrh;
   un_tmr4_oc_ocmrl_t ocmrl;
   stc_tmr4_init_t tmr4_init_cfg;
   stc_tmr4_oc_init_t tmr4_oc_init;
   stc_tmr4_pwm_init_t tmr4_pwm_init;
   stc_tmr4_evt_init_t tmr4_evt_init;

   /* 1) Route the six PWM pins to the TMR4 outputs (UH/UL, VH/VL, WH/WL).
      Without GPIO_FUNC_2 the timer runs, but the pins still behave as GPIOs. */
   GPIO_SetFunc(GPIO_PORT_B, GPIO_PIN_13, GPIO_FUNC_2); /* TMR4_1-OUL */
   GPIO_SetFunc(GPIO_PORT_B, GPIO_PIN_14, GPIO_FUNC_2); /* TMR4_1-OVL */
   GPIO_SetFunc(GPIO_PORT_B, GPIO_PIN_15, GPIO_FUNC_2); /* TMR4_1-OWL */
   GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_08, GPIO_FUNC_2); /* TMR4_1-OUH */
   GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_09, GPIO_FUNC_2); /* TMR4_1-OVH */
   GPIO_SetFunc(GPIO_PORT_A, GPIO_PIN_10, GPIO_FUNC_2); /* TMR4_1-OWH */

   /* 2) Enable the TMR4_1 peripheral clock before touching the timer registers. */

   FCG_Fcg2PeriphClockCmd(FCG2_PERIPH_TMR4_1, ENABLE);

   /* 3) Configure the timer base counter. Use the internal clock, DIV1, and
      triangle mode for center-aligned PWM. PWM_TIM_CNT defines the period. */


   tmr4_init_cfg.u16ClockSrc = TMR4_CLK_SRC_INTERNCLK;
   tmr4_init_cfg.u16ClockDiv = TMR4_CLK_DIV1;
   tmr4_init_cfg.u16CountMode = TMR4_MD_TRIANGLE;
   tmr4_init_cfg.u16PeriodValue = (uint16_t)PWM_TIM_CNT;
   (void)TMR4_Init(CM_TMR4_1, &tmr4_init_cfg);

   /* 4) Configure the default OC settings shared by all channels. The initial
      compare value is the startup duty cycle and the invalid polarity stays low. */


   (void)TMR4_OC_StructInit(&tmr4_oc_init);
   tmr4_oc_init.u16CompareValue = (uint16_t)PWM_INITDUTY;
   tmr4_oc_init.u16OcInvalidPolarity = TMR4_OC_INVD_LOW;
   tmr4_oc_init.u16CompareValueBufCond = TMR4_OC_BUF_COND_VALLEY;
   tmr4_oc_init.u16CompareModeBufCond = TMR4_OC_BUF_COND_VALLEY;
   tmr4_oc_init.u16BufLinkTransObject = (TMR4_OC_BUF_CMP_VALUE | TMR4_OC_BUF_CMP_MD);

   /* 5) Configure the high-side OCMRH state table. HOLD is used on non-match
      peak/valley events to avoid a forced toggle at zero duty. */
   ocmrh.OCMRx = 0U;
   ocmrh.OCMRx_f.OPUCH = TMR4_OC_HIGH;
   ocmrh.OCMRx_f.OPDCH = TMR4_OC_LOW;
   ocmrh.OCMRx_f.OPPKH = TMR4_OC_HIGH;
   ocmrh.OCMRx_f.OPZRH = TMR4_OC_LOW;
   ocmrh.OCMRx_f.OPNPKH = TMR4_OC_HOLD;
   ocmrh.OCMRx_f.OPNZRH = TMR4_OC_HOLD;

   /* 6) Initialize the UH channel and enable its compare output. */

   (void)TMR4_OC_Init(CM_TMR4_1, TMR4_OC_CH_UH, &tmr4_oc_init);
   TMR4_OC_SetHighChCompareMode(CM_TMR4_1, TMR4_OC_CH_UH, ocmrh);
   /* Match XHCode: release the external gate pin to Hi-Z on EMB/MOE-off. */
   TMR4_PWM_SetAbnormalPinStatus(CM_TMR4_1, TMR4_PWM_PIN_OUH, TMR4_PWM_ABNORMAL_PIN_HIZ);
   TMR4_OC_Cmd(CM_TMR4_1, TMR4_OC_CH_UH, ENABLE);

   /* 7) Initialize the VH channel and enable its compare output. */

   (void)TMR4_OC_Init(CM_TMR4_1, TMR4_OC_CH_VH, &tmr4_oc_init);
   TMR4_OC_SetHighChCompareMode(CM_TMR4_1, TMR4_OC_CH_VH, ocmrh);
   TMR4_PWM_SetAbnormalPinStatus(CM_TMR4_1, TMR4_PWM_PIN_OVH, TMR4_PWM_ABNORMAL_PIN_HIZ);
   TMR4_OC_Cmd(CM_TMR4_1, TMR4_OC_CH_VH, ENABLE);

   /* 8) Initialize the WH channel and enable its compare output. */

   (void)TMR4_OC_Init(CM_TMR4_1, TMR4_OC_CH_WH, &tmr4_oc_init);
   TMR4_OC_SetHighChCompareMode(CM_TMR4_1, TMR4_OC_CH_WH, ocmrh);
   TMR4_PWM_SetAbnormalPinStatus(CM_TMR4_1, TMR4_PWM_PIN_OWH, TMR4_PWM_ABNORMAL_PIN_HIZ);
   TMR4_OC_Cmd(CM_TMR4_1, TMR4_OC_CH_WH, ENABLE);

   /* 9) Configure the low-side OCMRL state table with the extended bits.
      Force the count-zero match action low to avoid a false pulse at zero duty.
      The low-side outputs follow the same complementary rules as the high side. */
   ocmrl.OCMRx = 0UL;
   ocmrl.OCMRx_f.OPUCL = TMR4_OC_LOW;
   ocmrl.OCMRx_f.OPDCL = TMR4_OC_HIGH;
   ocmrl.OCMRx_f.OPPKL = TMR4_OC_LOW;
   ocmrl.OCMRx_f.OPZRL = TMR4_OC_LOW;
   ocmrl.OCMRx_f.OPNPKL = TMR4_OC_HOLD;
   ocmrl.OCMRx_f.OPNZRL = TMR4_OC_HOLD;
   ocmrl.OCMRx_f.EOPUCL = TMR4_OC_LOW;
   ocmrl.OCMRx_f.EOPDCL = TMR4_OC_HIGH;
   ocmrl.OCMRx_f.EOPPKL = TMR4_OC_LOW;
   ocmrl.OCMRx_f.EOPZRL = TMR4_OC_LOW;
   ocmrl.OCMRx_f.EOPNPKL = TMR4_OC_HOLD;
   ocmrl.OCMRx_f.EOPNZRL = TMR4_OC_HOLD;

   /* 10) Initialize UL, enable the low-side extended controls, and enable output. */

   (void)TMR4_OC_Init(CM_TMR4_1, TMR4_OC_CH_UL, &tmr4_oc_init);
   TMR4_OC_SetLowChCompareMode(CM_TMR4_1, TMR4_OC_CH_UL, ocmrl);
   TMR4_PWM_SetAbnormalPinStatus(CM_TMR4_1, TMR4_PWM_PIN_OUL, TMR4_PWM_ABNORMAL_PIN_HIZ);
   TMR4_OC_ExtendControlCmd(CM_TMR4_1, TMR4_OC_CH_UL, ENABLE);
   TMR4_OC_Cmd(CM_TMR4_1, TMR4_OC_CH_UL, ENABLE);

   /* 11) Initialize VL with the same complementary switching rules as UL. */

   (void)TMR4_OC_Init(CM_TMR4_1, TMR4_OC_CH_VL, &tmr4_oc_init);
   TMR4_OC_SetLowChCompareMode(CM_TMR4_1, TMR4_OC_CH_VL, ocmrl);
   TMR4_PWM_SetAbnormalPinStatus(CM_TMR4_1, TMR4_PWM_PIN_OVL, TMR4_PWM_ABNORMAL_PIN_HIZ);
   TMR4_OC_ExtendControlCmd(CM_TMR4_1, TMR4_OC_CH_VL, ENABLE);
   TMR4_OC_Cmd(CM_TMR4_1, TMR4_OC_CH_VL, ENABLE);

   /* 12) Initialize WL with the same complementary switching rules as UL and VL. */

   (void)TMR4_OC_Init(CM_TMR4_1, TMR4_OC_CH_WL, &tmr4_oc_init);
   TMR4_OC_SetLowChCompareMode(CM_TMR4_1, TMR4_OC_CH_WL, ocmrl);
   TMR4_PWM_SetAbnormalPinStatus(CM_TMR4_1, TMR4_PWM_PIN_OWL, TMR4_PWM_ABNORMAL_PIN_HIZ);
   TMR4_OC_ExtendControlCmd(CM_TMR4_1, TMR4_OC_CH_WL, ENABLE);
   TMR4_OC_Cmd(CM_TMR4_1, TMR4_OC_CH_WL, ENABLE);

   /* 13) Configure the PWM module mode. Hardware dead time is enabled and the
      PWM block does not apply any extra output inversion. */


   tmr4_pwm_init.u16Mode = TMR4_PWM_MD_DEAD_TMR;
   tmr4_pwm_init.u16ClockDiv = TMR4_PWM_CLK_DIV1;
   tmr4_pwm_init.u16Polarity = TMR4_PWM_OXH_HOLD_OXL_HOLD;

   /* 14) Initialize PWM U and configure dead time on both edge paths. */

   (void)TMR4_PWM_Init(CM_TMR4_1, TMR4_PWM_CH_U, &tmr4_pwm_init);
   TMR4_PWM_SetDeadTimeValue(CM_TMR4_1, TMR4_PWM_CH_U, TMR4_PWM_PDAR_IDX, (uint16_t)PWM_DEADTIME);
   TMR4_PWM_SetDeadTimeValue(CM_TMR4_1, TMR4_PWM_CH_U, TMR4_PWM_PDBR_IDX, (uint16_t)PWM_DEADTIME);

   /* 15) Initialize PWM V and configure dead time on both edge paths. */

   (void)TMR4_PWM_Init(CM_TMR4_1, TMR4_PWM_CH_V, &tmr4_pwm_init);
   TMR4_PWM_SetDeadTimeValue(CM_TMR4_1, TMR4_PWM_CH_V, TMR4_PWM_PDAR_IDX, (uint16_t)PWM_DEADTIME);
   TMR4_PWM_SetDeadTimeValue(CM_TMR4_1, TMR4_PWM_CH_V, TMR4_PWM_PDBR_IDX, (uint16_t)PWM_DEADTIME);

   /* 16) Initialize PWM W and configure dead time on both edge paths. */

   (void)TMR4_PWM_Init(CM_TMR4_1, TMR4_PWM_CH_W, &tmr4_pwm_init);
   TMR4_PWM_SetDeadTimeValue(CM_TMR4_1, TMR4_PWM_CH_W, TMR4_PWM_PDAR_IDX, (uint16_t)PWM_DEADTIME);
   TMR4_PWM_SetDeadTimeValue(CM_TMR4_1, TMR4_PWM_CH_W, TMR4_PWM_PDBR_IDX, (uint16_t)PWM_DEADTIME);

   /* 17) Configure the TMR4 special compare event used as the ADC trigger point.
      SCMP4 acts as the AOS source and only fires on the configured compare match.
      This keeps the ADC sampling point fixed inside every PWM cycle. */

   (void)TMR4_EVT_StructInit(&tmr4_evt_init);
   tmr4_evt_init.u16Mode = TMR4_EVT_MD_CMP;
   tmr4_evt_init.u16CompareValue = (uint16_t)PWM_TRIG_VAL;
   tmr4_evt_init.u16OutputEvent = TMR4_EVT_OUTPUT_EVT4;
   tmr4_evt_init.u16MatchCond = TMR4_EVT_MATCH_CNT_UP;
   (void)TMR4_EVT_Init(CM_TMR4_1, TMR4_EVT_CH_WH, &tmr4_evt_init);

   /* 18) Program safe initial duties before starting the counter and opening the
      global output gate to prevent shoot-through during startup. */
   PWMOUT_U = PWMOUT_MAX >> 1;
   PWMOUT_V = PWMOUT_MAX >> 1;
   PWMOUT_W = PWMOUT_MAX >> 1;
   TMR4_Start(CM_TMR4_1);
   PWMOUT_DISABLE();
}
