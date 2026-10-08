/// @file  can.h
/// @brief CAN peripheral initialization and message interface.

#ifndef __APP_HARDWARE_CAN_H__
#define __APP_HARDWARE_CAN_H__

#include <stdbool.h>
#include <stdint.h>

/// @brief Simplified CAN message descriptor (classic CAN, ≤8 bytes).
typedef struct {
    uint32_t id;        /*!< CAN identifier (11-bit if ide=0, 29-bit if ide=1) */
    uint8_t  ide;       /*!< 0 = standard ID, 1 = extended ID */
    uint8_t  rtr;       /*!< 0 = data frame, 1 = remote frame */
    uint8_t  dlc;       /*!< Data length code: 0..8 */
    uint8_t  data[8];   /*!< Payload bytes */
} can_msg_t;

void can1_init(void);
bool can1_send(const can_msg_t *msg);
bool can1_recv(can_msg_t *msg);

#endif /* __APP_HARDWARE_CAN_H__ */
