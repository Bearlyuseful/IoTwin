#include "board.h"

#include "stm32g0xx.h"

/* CMSIS: current core clock, updated by SystemInit(). */
uint32_t SystemCoreClock = 16000000u;

static volatile uint32_t g_millis;

#define LED_PWM_PERIOD 1000u

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

  /* LD4 dimmed by TIM2 CH1 PWM: 64 MHz / 64 = 1 MHz, period 1000 counts = 1 kHz */
  RCC->APBENR1 |= RCC_APBENR1_TIM2EN;
  (void)RCC->APBENR1;
  TIM2->PSC = (BOARD_PCLK_HZ / 1000000u) - 1u;
  TIM2->ARR = LED_PWM_PERIOD - 1u;
  TIM2->CCR1 = 0;
  TIM2->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE; /* PWM mode 1, preload */
  TIM2->CCER = TIM_CCER_CC1E;
  TIM2->CR1 = TIM_CR1_ARPE;
  TIM2->EGR = TIM_EGR_UG;
  TIM2->CR1 |= TIM_CR1_CEN;
  gpio_alternate(GPIOA, 5, 2); /* PA5 AF2 = TIM2_CH1 */

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

void board_led_set(bool on) { board_led_set_level(on ? 100u : 0u); }

void board_led_set_level(uint8_t percent) {
  if (percent > 100u) {
    percent = 100u;
  }
  /* CCR1 = period (or more) keeps the output high all the time, 0 keeps it low. */
  TIM2->CCR1 = (uint32_t)percent * LED_PWM_PERIOD / 100u;
}

uint8_t board_led_measure_percent(void) {
  uint32_t high = 0;
  uint32_t samples = 0;
  /* About 5 ms of samples covers five PWM periods; the loop runs from flash at 64 MHz. */
  for (uint32_t i = 0; i < 40000u; i++) {
    high += (GPIOA->IDR >> 5) & 1u;
    samples++;
  }
  return (uint8_t)((high * 100u + samples / 2u) / samples);
}

bool board_button_raw(void) { return (GPIOC->IDR & (1u << 13)) == 0; }
