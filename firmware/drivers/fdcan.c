#include "fdcan.h"

#include "can_bit_timing.h"
#include "stm32g0xx.h"

/* Message RAM layout of FDCAN1 (byte offsets, see header). */
#define RAM_RX_FIFO0 0x0B0u
#define RAM_TX_BUFFERS 0x278u
#define RAM_SIZE 0x350u
#define ELEMENT_WORDS 18u

#define ELEMENT_XTD (1u << 30)
#define ELEMENT_RTR (1u << 29)
#define ELEMENT_DLC_POS 16u

#define RX_QUEUE_SIZE 32u /* power of two */
#define INIT_TIMEOUT_LOOPS 1000000u

static volatile uint32_t* const g_ram = (volatile uint32_t*)SRAMCAN_BASE;

static CanFrame g_rx_queue[RX_QUEUE_SIZE];
static volatile uint32_t g_rx_head; /* ISR */
static volatile uint32_t g_rx_tail; /* main loop */
static volatile bool g_rx_overrun;
static bool g_running;

static bool wait_init(bool set) {
  for (uint32_t i = 0; i < INIT_TIMEOUT_LOOPS; i++) {
    if (((FDCAN1->CCCR & FDCAN_CCCR_INIT) != 0) == set) {
      return true;
    }
  }
  return false;
}

bool fdcan_start(uint32_t kernel_clock_hz, uint32_t bitrate, FdcanMode mode) {
  CanBitTiming timing;
  if (!can_bit_timing_calculate(kernel_clock_hz, bitrate, CAN_BIT_TIMING_DEFAULT_SAMPLE_POINT,
                                &CAN_BIT_TIMING_LIMITS_FDCAN_NOMINAL, &timing)) {
    return false;
  }

  RCC->CCIPR2 &= ~RCC_CCIPR2_FDCANSEL; /* kernel clock = PCLK */
  RCC->APBENR1 |= RCC_APBENR1_FDCANEN;
  (void)RCC->APBENR1;

  NVIC_DisableIRQ(TIM16_FDCAN_IT0_IRQn);

  /* Initialisation mode with write access to the configuration. */
  FDCAN1->CCCR |= FDCAN_CCCR_INIT;
  if (!wait_init(true)) {
    return false;
  }
  FDCAN1->CCCR |= FDCAN_CCCR_CCE;
  FDCAN_CONFIG->CKDIV = 0; /* kernel clock divider 1 */

  /* The message RAM is not initialised by reset; it must be written word-wise. */
  for (uint32_t i = 0; i < RAM_SIZE / 4u; i++) {
    g_ram[i] = 0;
  }

  /* Classic CAN, automatic retransmission, mode bits per request. */
  uint32_t cccr = FDCAN1->CCCR & ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_MON |
                                   FDCAN_CCCR_TEST | FDCAN_CCCR_DAR | FDCAN_CCCR_ASM);
  if (mode == FDCAN_MODE_INTERNAL_LOOPBACK) {
    cccr |= FDCAN_CCCR_TEST | FDCAN_CCCR_MON;
  } else if (mode == FDCAN_MODE_BUS_MONITORING) {
    cccr |= FDCAN_CCCR_MON;
  }
  FDCAN1->CCCR = cccr;
  if (mode == FDCAN_MODE_INTERNAL_LOOPBACK) {
    FDCAN1->TEST |= FDCAN_TEST_LBCK; /* writable only while CCCR.TEST is set */
  }

  FDCAN1->NBTP = ((timing.sjw - 1u) << FDCAN_NBTP_NSJW_Pos) |
                 ((timing.prescaler - 1u) << FDCAN_NBTP_NBRP_Pos) |
                 ((timing.seg1 - 1u) << FDCAN_NBTP_NTSEG1_Pos) |
                 ((timing.seg2 - 1u) << FDCAN_NBTP_NTSEG2_Pos);

  FDCAN1->RXGFC = 0; /* no filter elements; accept all (also remote) frames into Rx FIFO 0 */
  FDCAN1->TXBC = 0;  /* Tx FIFO mode */

  FDCAN1->IR = 0xFFFFFFFFu; /* clear pending flags */
  FDCAN1->IE = FDCAN_IE_RF0NE | FDCAN_IE_RF0LE;
  FDCAN1->ILS = 0; /* all interrupt groups on line 0 */
  FDCAN1->ILE = FDCAN_ILE_EINT0;

  g_rx_head = 0;
  g_rx_tail = 0;
  g_rx_overrun = false;

  NVIC_SetPriority(TIM16_FDCAN_IT0_IRQn, 1);
  NVIC_ClearPendingIRQ(TIM16_FDCAN_IT0_IRQn);
  NVIC_EnableIRQ(TIM16_FDCAN_IT0_IRQn);

  /* Leave initialisation; the controller joins the bus after 11 recessive bits. */
  FDCAN1->CCCR &= ~FDCAN_CCCR_INIT;
  g_running = wait_init(false);
  return g_running;
}

void fdcan_stop(void) {
  NVIC_DisableIRQ(TIM16_FDCAN_IT0_IRQn);
  FDCAN1->CCCR |= FDCAN_CCCR_INIT;
  wait_init(true);
  g_running = false;
}

bool fdcan_send(const CanFrame* frame) {
  if (!g_running || (FDCAN1->TXFQS & FDCAN_TXFQS_TFQF)) {
    return false;
  }
  const uint32_t index = (FDCAN1->TXFQS & FDCAN_TXFQS_TFQPI) >> FDCAN_TXFQS_TFQPI_Pos;
  volatile uint32_t* element = g_ram + (RAM_TX_BUFFERS / 4u) + index * ELEMENT_WORDS;

  uint32_t t0 = frame->extended ? (ELEMENT_XTD | (frame->id & CAN_EFF_MASK)) : ((frame->id & CAN_SFF_MASK) << 18);
  if (frame->rtr) {
    t0 |= ELEMENT_RTR;
  }
  element[0] = t0;
  element[1] = (uint32_t)frame->dlc << ELEMENT_DLC_POS; /* classic CAN, no event FIFO entry */
  element[2] = (uint32_t)frame->data[0] | ((uint32_t)frame->data[1] << 8) |
               ((uint32_t)frame->data[2] << 16) | ((uint32_t)frame->data[3] << 24);
  element[3] = (uint32_t)frame->data[4] | ((uint32_t)frame->data[5] << 8) |
               ((uint32_t)frame->data[6] << 16) | ((uint32_t)frame->data[7] << 24);

  FDCAN1->TXBAR = 1u << index;
  return true;
}

bool fdcan_receive(CanFrame* frame) {
  const uint32_t tail = g_rx_tail;
  if (tail == g_rx_head) {
    return false;
  }
  *frame = g_rx_queue[tail];
  g_rx_tail = (tail + 1u) & (RX_QUEUE_SIZE - 1u);
  return true;
}

bool fdcan_take_overrun(void) {
  if (!g_rx_overrun) {
    return false;
  }
  g_rx_overrun = false;
  return true;
}

void fdcan_get_status(FdcanStatus* status) {
  const uint32_t ecr = FDCAN1->ECR;
  const uint32_t psr = FDCAN1->PSR;
  status->tx_error_count = (uint8_t)((ecr & FDCAN_ECR_TEC) >> FDCAN_ECR_TEC_Pos);
  status->rx_error_count = (uint8_t)((ecr & FDCAN_ECR_REC) >> FDCAN_ECR_REC_Pos);
  status->error_warning = (psr & FDCAN_PSR_EW) != 0;
  status->error_passive = (psr & FDCAN_PSR_EP) != 0;
  status->bus_off = (psr & FDCAN_PSR_BO) != 0;
  status->last_error_code = (uint8_t)((psr & FDCAN_PSR_LEC) >> FDCAN_PSR_LEC_Pos);
}

void fdcan_poll(void) {
  /* Bus-off sets CCCR.INIT. Clearing it starts the recovery sequence
     (128 x 11 recessive bits) after which the node rejoins the bus. */
  if (g_running && (FDCAN1->PSR & FDCAN_PSR_BO) && (FDCAN1->CCCR & FDCAN_CCCR_INIT)) {
    FDCAN1->CCCR &= ~FDCAN_CCCR_INIT;
  }
}

static void read_element(uint32_t index, CanFrame* frame) {
  const volatile uint32_t* element = g_ram + (RAM_RX_FIFO0 / 4u) + index * ELEMENT_WORDS;
  const uint32_t r0 = element[0];
  const uint32_t r1 = element[1];
  const uint32_t d0 = element[2];
  const uint32_t d1 = element[3];

  frame->extended = (r0 & ELEMENT_XTD) != 0;
  frame->rtr = (r0 & ELEMENT_RTR) != 0;
  frame->id = frame->extended ? (r0 & CAN_EFF_MASK) : ((r0 >> 18) & CAN_SFF_MASK);
  uint8_t dlc = (uint8_t)((r1 >> ELEMENT_DLC_POS) & 0xFu);
  frame->dlc = dlc > CAN_MAX_DLEN ? CAN_MAX_DLEN : dlc; /* DLC 9..15 means 8 bytes in classic CAN */
  for (unsigned i = 0; i < 4; i++) {
    frame->data[i] = (uint8_t)(d0 >> (8u * i));
    frame->data[i + 4u] = (uint8_t)(d1 >> (8u * i));
  }
  for (unsigned i = frame->rtr ? 0u : frame->dlc; i < CAN_MAX_DLEN; i++) {
    frame->data[i] = 0;
  }
}

void TIM16_FDCAN_IT0_IRQHandler(void) {
  const uint32_t flags = FDCAN1->IR & (FDCAN_IR_RF0N | FDCAN_IR_RF0L);
  FDCAN1->IR = flags; /* write 1 to clear */
  if (flags & FDCAN_IR_RF0L) {
    g_rx_overrun = true;
  }

  while ((FDCAN1->RXF0S & FDCAN_RXF0S_F0FL) != 0) {
    const uint32_t index = (FDCAN1->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    const uint32_t next = (g_rx_head + 1u) & (RX_QUEUE_SIZE - 1u);
    if (next != g_rx_tail) {
      read_element(index, &g_rx_queue[g_rx_head]);
      g_rx_head = next;
    } else {
      g_rx_overrun = true;
    }
    FDCAN1->RXF0A = index; /* release the element */
  }
}
