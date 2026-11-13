#include "board.h"

#include "stm32g0xx.h"

/* CMSIS: current core clock, updated by SystemInit(). */
uint32_t SystemCoreClock = 16000000u;

static volatile uint32_t g_millis;

enum { MODE_INPUT = 0u, MODE_OUTPUT = 1u, MODE_ALTERNATE = 2u };
enum { PULL_NONE = 0u, PULL_UP = 1u };

static void gpio_mode(GPIO_TypeDef* port, unsigned pin, uint32_t mode) {
  port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (mode << (pin * 2u));
}

static void gpio_pull(GPIO_TypeDef* port, unsigned pin, uint32_t pull) {
  port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | (pull << (pin * 2u));
}

static void gpio_speed_high(GPIO_TypeDef* port, unsigned pin) {
  port->OSPEEDR |= 2u << (pin * 2u);
}

static void gpio_alternate(GPIO_TypeDef* port, unsigned pin, uint32_t af) {
  const unsigned shift = (pin & 7u) * 4u;
  port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << shift)) | (af << shift);
  gpio_mode(port, pin, MODE_ALTERNATE);
}

/*
 * Called by Reset_Handler before main(): 64 MHz from HSI16 via the PLL.
 * Voltage scaling range 1 (reset default) allows 64 MHz; flash needs 2 wait states.
 */
void SystemInit(void) {
  FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_1 | FLASH_ACR_PRFTEN |
               FLASH_ACR_ICEN;
  while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_1) {
  }

  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY) {
  }
  /* VCO = 16 MHz / M(1) * N(8) = 128 MHz, R = /2 -> 64 MHz, Q = /2 -> 64 MHz */
  RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | (0u << RCC_PLLCFGR_PLLM_Pos) | (8u << RCC_PLLCFGR_PLLN_Pos) |
                 (1u << RCC_PLLCFGR_PLLR_Pos) | RCC_PLLCFGR_PLLREN | (1u << RCC_PLLCFGR_PLLQ_Pos) |
                 RCC_PLLCFGR_PLLQEN;
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY)) {
  }

  /* AHB and APB prescaler 1, SYSCLK = PLLRCLK */
  RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE)) | (2u << RCC_CFGR_SW_Pos);
  while ((RCC->CFGR & RCC_CFGR_SWS) != (2u << RCC_CFGR_SWS_Pos)) {
  }
  SystemCoreClock = BOARD_SYSCLK_HZ;
}

void board_init(void) {
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOCEN;
  (void)RCC->IOPENR; /* ensure the clock is running before touching the ports */

  /* LD4 */
  GPIOA->BSRR = 1u << (5u + 16u);
  gpio_mode(GPIOA, 5, MODE_OUTPUT);

  /* B1 (external pull-up on the board; internal one as well for safety) */
  gpio_pull(GPIOC, 13, PULL_UP);
  gpio_mode(GPIOC, 13, MODE_INPUT);

  /* USART2 to the ST-LINK virtual COM port */
  gpio_alternate(GPIOA, 2, 1);
  gpio_alternate(GPIOA, 3, 1);
  gpio_pull(GPIOA, 3, PULL_UP);

  /* FDCAN1; the pull-up keeps RX recessive when no transceiver is connected */
  gpio_alternate(GPIOA, 11, 3);
  gpio_alternate(GPIOA, 12, 3);
  gpio_pull(GPIOA, 11, PULL_UP);
  gpio_speed_high(GPIOA, 12);

  SysTick_Config(SystemCoreClock / 1000u);
}

void SysTick_Handler(void) { g_millis++; }

uint32_t board_millis(void) { return g_millis; }

void board_led_set(bool on) { GPIOA->BSRR = on ? (1u << 5) : (1u << (5u + 16u)); }

bool board_button_raw(void) { return (GPIOC->IDR & (1u << 13)) == 0; }
