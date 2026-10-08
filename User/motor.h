/// @file  motor.h
/// @brief Motor ISR driver entry points.

#ifndef __APP_USER_MOTOR_H__
#define __APP_USER_MOTOR_H__

#include "hc32_ll.h"

#include "adc.h"
#include "timer4.h"
#include "encoder.h"

#include "bsp.h"

/// @brief Initialize the motor-control runtime defaults.
void motor_init(void);

#endif /* __APP_USER_MOTOR_H__ */
