/**
 * @file main.c
 * @brief IoTwin CAN gateway firmware for the NUCLEO-G0B1RE.
 *
 * SLCAN over the ST-LINK virtual COM port (115200 8N1) <-> FDCAN1, plus a
 * local node: user button = wall switch (0x200), LED = lamp command (0x100),
 * and a UDS server (0x7E1 -> 0x7E9).
 */

#include "board.h"
#include "fdcan.h"
#include "gateway.h"
#include "stm32g0xx.h"
#include "uart.h"

#define SERIAL_BAUDRATE 115200u
#define DEBOUNCE_MS 20u

static Gateway g_gateway;

static void io_serial_write(void* context, const char* data, size_t length) {
  (void)context;
  uart_write((const uint8_t*)data, length);
}

static bool io_can_start(void* context, uint32_t bitrate, GatewayCanMode mode) {
  (void)context;
  FdcanMode fdcan_mode = FDCAN_MODE_NORMAL;
  if (mode == GATEWAY_CAN_LOOPBACK) {
    fdcan_mode = FDCAN_MODE_INTERNAL_LOOPBACK;
  } else if (mode == GATEWAY_CAN_LISTEN) {
    fdcan_mode = FDCAN_MODE_BUS_MONITORING;
  }
  return fdcan_start(BOARD_FDCAN_CLOCK_HZ, bitrate, fdcan_mode);
}

static void io_can_stop(void* context) {
  (void)context;
  fdcan_stop();
}

static bool io_can_send(void* context, const CanFrame* frame) {
  (void)context;
  return fdcan_send(frame);
}

static void io_can_status(void* context, GatewayCanStatus* status) {
  (void)context;
  FdcanStatus s;
  fdcan_get_status(&s);
  status->tx_error_count = s.tx_error_count;
  status->rx_error_count = s.rx_error_count;
  status->error_warning = s.error_warning;
  status->error_passive = s.error_passive;
  status->bus_off = s.bus_off;
  status->bus_error = s.last_error_code != 0 && s.last_error_code != 7;
}

static void io_led_set(void* context, bool on) {
  (void)context;
  board_led_set(on);
}

static void io_system_reset(void* context) {
  (void)context;
  NVIC_SystemReset();
}

static void io_unique_id(void* context, uint8_t out[12]) {
  (void)context;
  const volatile uint8_t* uid = (const volatile uint8_t*)UID_BASE; /* 96-bit unique device id */
  for (size_t i = 0; i < 12; i++) {
    out[i] = uid[i];
  }
}

/* Reports a press once the button level was stable for DEBOUNCE_MS. */
static bool button_pressed_event(void) {
  static bool stable = false;
  static uint32_t changing_ms = 0;
  const bool raw = board_button_raw();
  if (raw == stable) {
    changing_ms = 0;
    return false;
  }
  if (++changing_ms < DEBOUNCE_MS) {
    return false;
  }
  stable = raw;
  changing_ms = 0;
  return stable;
}

int main(void) {
  board_init();
  uart_init(BOARD_PCLK_HZ, SERIAL_BAUDRATE);

  const GatewayIo io = {
      .serial_write = io_serial_write,
      .can_start = io_can_start,
      .can_stop = io_can_stop,
      .can_send = io_can_send,
      .can_status = io_can_status,
      .led_set = io_led_set,
      .system_reset = io_system_reset,
      .unique_id = io_unique_id,
      .context = 0,
  };
  gateway_init(&g_gateway, &io);

  uint32_t last_ms = board_millis();
  for (;;) {
    uint8_t buffer[64];
    size_t count;
    while ((count = uart_read(buffer, sizeof(buffer))) > 0) {
      gateway_on_serial_data(&g_gateway, (const char*)buffer, count);
    }

    CanFrame frame;
    while (fdcan_receive(&frame)) {
      gateway_on_can_frame(&g_gateway, &frame);
    }
    if (fdcan_take_overrun()) {
      gateway_on_rx_overrun(&g_gateway);
    }
    fdcan_poll();

    const uint32_t now = board_millis();
    if (now != last_ms) {
      /* once per millisecond */
      last_ms = now;
      if (button_pressed_event()) {
        gateway_on_button(&g_gateway);
      }
      gateway_tick(&g_gateway, now);
    }

    __WFI(); /* SysTick, UART or FDCAN interrupts wake us up */
  }
}
