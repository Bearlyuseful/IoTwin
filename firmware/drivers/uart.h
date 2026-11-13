#ifndef IOTWIN_UART_H
#define IOTWIN_UART_H

/**
 * @file uart.h
 * @brief Interrupt driven USART2 (ST-LINK virtual COM port) with ring buffers.
 *
 * RX: the ISR stores bytes in a ring buffer, the main loop reads them.
 * TX: the main loop fills a ring buffer, the TXE interrupt drains it.
 */

#include <stddef.h>
#include <stdint.h>

void uart_init(uint32_t peripheral_clock_hz, uint32_t baudrate);

/** @brief Copies up to @p size received bytes; returns the number copied. */
size_t uart_read(uint8_t* buffer, size_t size);

/** @brief Queues all bytes for transmission (waits while the TX buffer is full). */
void uart_write(const uint8_t* data, size_t length);

/** @brief Bytes lost because the RX buffer or the hardware overran (since start). */
uint32_t uart_rx_overruns(void);

#endif /* IOTWIN_UART_H */
