/// @file  encoder.h
/// @brief KTH7112 magnetic encoder driver via SPI + DMA, triggered by TMR4.

#ifndef __APP_HARDWARE_ENCODER_H__
#define __APP_HARDWARE_ENCODER_H__

#include <stdbool.h>
#include <stdint.h>

void encoder_init(void);

/// @brief  Poll for a new angle reading.
/// @param[in,out] angle  Pointer to the angle variable (fix(1,32,31) format).
///                       Updated only on a successful CRC-validated read;
///                       left unchanged on CRC failure (retains last valid value).
/// @retval true   Sensor healthy (angle may or may not have been updated this call).
/// @retval false  Sensor fault: 3 consecutive CRC failures, or sensor alarm detected.
bool encoder_recv(int32_t *angle);

#endif /* __APP_HARDWARE_ENCODER_H__ */
