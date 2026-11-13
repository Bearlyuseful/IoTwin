#include "uart.h"

#include "stm32g0xx.h"

#define RX_SIZE 256u  /* power of two */
#define TX_SIZE 1024u /* power of two */

static volatile uint8_t g_rx[RX_SIZE];
static volatile uint32_t g_rx_head; /* written by the ISR */
static volatile uint32_t g_rx_tail; /* written by the main loop */
static volatile uint32_t g_rx_overruns;

static volatile uint8_t g_tx[TX_SIZE];
static volatile uint32_t g_tx_head; /* written by the main loop */
static volatile uint32_t g_tx_tail; /* written by the ISR */

void uart_init(uint32_t peripheral_clock_hz, uint32_t baudrate) {
  RCC->APBENR1 |= RCC_APBENR1_USART2EN;
  (void)RCC->APBENR1;

  USART2->CR1 = 0;
  USART2->CR2 = 0;
  USART2->CR3 = 0;
  USART2->BRR = (peripheral_clock_hz + baudrate / 2u) / baudrate; /* oversampling by 16 */
  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;

  NVIC_SetPriority(USART2_LPUART2_IRQn, 1);
  NVIC_EnableIRQ(USART2_LPUART2_IRQn);
}

size_t uart_read(uint8_t* buffer, size_t size) {
  size_t count = 0;
  uint32_t tail = g_rx_tail;
  while (count < size && tail != g_rx_head) {
    buffer[count++] = g_rx[tail];
    tail = (tail + 1u) & (RX_SIZE - 1u);
  }
  g_rx_tail = tail;
  return count;
}

void uart_write(const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    const uint32_t next = (g_tx_head + 1u) & (TX_SIZE - 1u);
    while (next == g_tx_tail) {
      /* buffer full: the TXE interrupt makes room */
    }
    g_tx[g_tx_head] = data[i];
    g_tx_head = next;
  }
  __disable_irq();
  USART2->CR1 |= USART_CR1_TXEIE_TXFNFIE;
  __enable_irq();
}

uint32_t uart_rx_overruns(void) { return g_rx_overruns; }

void USART2_LPUART2_IRQHandler(void) {
  const uint32_t isr = USART2->ISR;

  if (isr & USART_ISR_ORE) {
    USART2->ICR = USART_ICR_ORECF;
    g_rx_overruns++;
  }
  if (isr & USART_ISR_RXNE_RXFNE) {
    const uint8_t byte = (uint8_t)USART2->RDR;
    const uint32_t next = (g_rx_head + 1u) & (RX_SIZE - 1u);
    if (next != g_rx_tail) {
      g_rx[g_rx_head] = byte;
      g_rx_head = next;
    } else {
      g_rx_overruns++;
    }
  }
  if ((USART2->CR1 & USART_CR1_TXEIE_TXFNFIE) && (isr & USART_ISR_TXE_TXFNF)) {
    if (g_tx_tail != g_tx_head) {
      USART2->TDR = g_tx[g_tx_tail];
      g_tx_tail = (g_tx_tail + 1u) & (TX_SIZE - 1u);
    } else {
      USART2->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
  }
}
