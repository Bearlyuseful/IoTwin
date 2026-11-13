#ifndef IOTWIN_BOARD_H
#define IOTWIN_BOARD_H

/**
 * @file board.h
 * @brief NUCLEO-G0B1RE board support (STM32G0B1RET6, Cortex-M0+).
 *
 *  - SYSCLK 64 MHz: HSI16 -> PLL (M=1, N=8, R=2), APB prescaler 1 (PCLK 64 MHz)
 *  - LD4 (green LED): PA5, active high
 *  - B1 (user button): PC13, active low
 *  - ST-LINK virtual COM port: USART2, PA2 (TX) / PA3 (RX), AF1
 *  - FDCAN1: PA11 (RX) / PA12 (TX), AF3 - connect a CAN transceiver here for a real bus
 */

#include <stdbool.h>
#include <stdint.h>

#define BOARD_SYSCLK_HZ 64000000u
#define BOARD_PCLK_HZ 64000000u
#define BOARD_FDCAN_CLOCK_HZ BOARD_PCLK_HZ /* FDCANSEL = PCLK */

/** @brief Clocks are set up by SystemInit() (startup); this configures GPIO and SysTick. */
void board_init(void);

/** @brief Milliseconds since start (SysTick). */
uint32_t board_millis(void);

/** @brief LD4 on or off (100 % / 0 %). */
void board_led_set(bool on);

/** @brief LD4 brightness 0..100 %: TIM2 CH1 PWM on PA5 (AF2), 1 kHz. */
void board_led_set_level(uint8_t percent);

/**
 * @brief Measures the duty cycle on the LED pin by sampling its input register for
 *        about 5 ms (self test of the PWM, independent of the timer registers).
 */
uint8_t board_led_measure_percent(void);

/** @brief Raw (not debounced) state of the user button, true = pressed. */
bool board_button_raw(void);

#endif /* IOTWIN_BOARD_H */
