/// @file  uart3.h
/// @brief APP UART3 non-blocking API (DMA + ring buffer).

#ifndef __APP_HARDWARE_UART3_H__
#define __APP_HARDWARE_UART3_H__

#include <stdint.h>
/// @brief Initialize UART3 peripheral and DMA TX/RX channels.
/// @param[in] bps UART3 baudrate in bps.
void uart3_init(uint32_t bps);

/// @brief Send data in non-blocking mode.
/// @details Input data is first pushed to TX ring buffer and then moved to UART3 TDR by DMA.
/// Extra bytes are dropped when TX buffer has insufficient space.
/// @param[in] data Pointer to source buffer.
/// @param[in] len  Number of bytes to send.
void uart3_send(const void *data, uint32_t len);

/// @brief Receive data in non-blocking mode.
/// @details RX DMA continuously writes incoming bytes into ring buffer memory.
/// This API reads available bytes from the software FIFO.
/// @param[out] data Pointer to destination buffer.
/// @param[in]  len  Maximum number of bytes to read.
/// @retval uint32_t Actual number of bytes copied to @p data.
uint32_t uart3_recv(void *data, uint32_t len);

/// @brief Print formatted text via UART3.
/// @param[in] fmt `printf`-style format string.
/// @param[in] ... Variable argument list.
void uart3_printf(const char *fmt, ...);

#endif /* __APP_HARDWARE_UART3_H__ */
