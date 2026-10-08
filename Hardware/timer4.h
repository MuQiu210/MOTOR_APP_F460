/// @file  timer4.h
/// @brief TMR4 peripheral initialization.

#ifndef __APP_HARDWARE_TIMER4_H__
#define __APP_HARDWARE_TIMER4_H__

#include "hc32_ll.h"

#define PWMOUT_ENABLE()                                 \
    do                                                  \
    {                                                   \
        /* F460 has one output-enable bit per high/low OC channel. */ \
        SET_REG16_BIT(CM_TMR4_1->OCSRU,                \
                      TMR4_OCSR_OCEH | TMR4_OCSR_OCEL); \
        SET_REG16_BIT(CM_TMR4_1->OCSRV,                \
                      TMR4_OCSR_OCEH | TMR4_OCSR_OCEL); \
        SET_REG16_BIT(CM_TMR4_1->OCSRW,                \
                      TMR4_OCSR_OCEH | TMR4_OCSR_OCEL); \
    } while (0)

#define PWMOUT_DISABLE()                                \
    do                                                  \
    {                                                   \
        CLR_REG16_BIT(CM_TMR4_1->OCSRU,                \
                      TMR4_OCSR_OCEH | TMR4_OCSR_OCEL); \
        CLR_REG16_BIT(CM_TMR4_1->OCSRV,                \
                      TMR4_OCSR_OCEH | TMR4_OCSR_OCEL); \
        CLR_REG16_BIT(CM_TMR4_1->OCSRW,                \
                      TMR4_OCSR_OCEH | TMR4_OCSR_OCEL); \
    } while (0)

#define PWMOUT_IS_ENABLED()                                                \
    (((CM_TMR4_1->OCSRU & (TMR4_OCSR_OCEH | TMR4_OCSR_OCEL)) ==             \
      (TMR4_OCSR_OCEH | TMR4_OCSR_OCEL)) &&                                \
     ((CM_TMR4_1->OCSRV & (TMR4_OCSR_OCEH | TMR4_OCSR_OCEL)) ==             \
      (TMR4_OCSR_OCEH | TMR4_OCSR_OCEL)) &&                                \
     ((CM_TMR4_1->OCSRW & (TMR4_OCSR_OCEH | TMR4_OCSR_OCEL)) ==             \
      (TMR4_OCSR_OCEH | TMR4_OCSR_OCEL)))

#define PWM_TIM_CNT (5000U)
#define PWM_INITDUTY (0U)
#define PWM_DEADTIME (50U)
#define PWMOUT_MAX (4330U)

#define PWMOUT_U (CM_TMR4_1->OCCRUL)
#define PWMOUT_V (CM_TMR4_1->OCCRVL)
#define PWMOUT_W (CM_TMR4_1->OCCRWL)
#define PWM_TRIG_VAL (PWM_TIM_CNT - PWM_DEADTIME - 50)

void timer4_init(void);

#endif /* __APP_HARDWARE_TIMER4_H__ */
