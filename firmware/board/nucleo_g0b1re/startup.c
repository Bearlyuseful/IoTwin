/**
 * @file startup.c
 * @brief Reset handler and vector table for the STM32G0B1 (Cortex-M0+), in C.
 */

#include <stdint.h>

#include "stm32g0xx.h"

/* Symbols from linker.ld */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Handlers a driver may implement; everything else ends in Default_Handler. */
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_LPUART2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM16_FDCAN_IT0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

typedef void (*Vector)(void);

#define CORE_VECTORS 16
#define DEVICE_VECTORS 32

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used)) const Vector g_vector_table[CORE_VECTORS + DEVICE_VECTORS] = {
    [0 ... CORE_VECTORS + DEVICE_VECTORS - 1] = Default_Handler,
    [0] = (Vector)(uintptr_t)&_estack, /* initial stack pointer */
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4 ... 10] = 0, /* reserved on ARMv6-M */
    [11] = SVC_Handler,
    [12 ... 13] = 0,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [CORE_VECTORS + USART2_LPUART2_IRQn] = USART2_LPUART2_IRQHandler,
    [CORE_VECTORS + TIM16_FDCAN_IT0_IRQn] = TIM16_FDCAN_IT0_IRQHandler,
};
#pragma GCC diagnostic pop

void Reset_Handler(void) {
  /* Copy initialised data from flash to RAM, clear .bss. */
  uint32_t* src = &_sidata;
  for (uint32_t* dst = &_sdata; dst < &_edata;) {
    *dst++ = *src++;
  }
  for (uint32_t* dst = &_sbss; dst < &_ebss;) {
    *dst++ = 0;
  }

  SystemInit();
  main();
  for (;;) {
  }
}

void Default_Handler(void) {
  /* Unexpected interrupt or fault: stop here (inspect with a debugger). */
  for (;;) {
  }
}
