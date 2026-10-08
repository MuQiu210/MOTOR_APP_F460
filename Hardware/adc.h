/// @file  adc.h
/// @brief ADC peripheral initialization.

#ifndef __APP_HARDWARE_ADC_H__
#define __APP_HARDWARE_ADC_H__

#include "hc32_ll.h"

/* Quick ADC sample access macros named after the schematic nets. */
#define IU_ADC      ((uint16_t)(CM_ADC2->DR3)) /* ADC2 CH3, PB0 */
#define IV_ADC      ((uint16_t)(CM_ADC2->DR0)) /* ADC2 CH0, PA7 */
#define IW_ADC      ((uint16_t)(CM_ADC1->DR0)) /* ADC1 CH0, PA6 */
#define HALL2_ADC   ((uint16_t)(CM_ADC1->DR1)) /* ADC1 CH1, PA3 */
#define HALL1_ADC   ((uint16_t)(CM_ADC2->DR1)) /* ADC2 CH1, PA4 */
#define PWR_24V_DET ((uint16_t)(CM_ADC1->DR2)) /* ADC1 CH2, PA1 */
#define PWR_5V_DET  ((uint16_t)(CM_ADC1->DR3)) /* ADC1 CH3, PA2 */
#define MOTOR_TEMP  ((uint16_t)(CM_ADC2->DR2)) /* ADC2 CH2, PB1 */
#define MOS_TEMP    ((uint16_t)(CM_ADC2->DR4)) /* ADC2 CH4, PA5 */

void adc_init(void);

#endif /* __APP_HARDWARE_ADC_H__ */
