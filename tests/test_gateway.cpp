// Firmware application logic (firmware/app/gateway.c) running against fake hardware.

#include <catch2/catch_all.hpp>

#include <deque>
#include <string>
#include <vector>

#include "gateway.h"

namespace {

struct FakeBoard {
  std::string serial_out;
  std::vector<CanFrame> sent;
  bool started = false;
  uint32_t bitrate = 0;
  GatewayCanMode mode = GATEWAY_CAN_NORMAL;
  bool tx_full = false;
  bool led = false;
  int led_changes = 0;
  int system_resets = 0;
  GatewayCanStatus status{};

  static FakeBoard& self(void* context) { return *static_cast<FakeBoard*>(context); }

  GatewayIo io() {
    GatewayIo io{};
    io.context = this;
    io.serial_write = [](void* ctx, const char* data, size_t length) {
      self(ctx).serial_out.append(data, length);
    };
    io.can_start = [](void* ctx, uint32_t bitrate, GatewayCanMode mode) {
      self(ctx).started = true;
      self(ctx).bitrate = bitrate;
      self(ctx).mode = mode;
      return true;
    };
    io.can_stop = [](void* ctx) { self(ctx).started = false; };
    io.can_send = [](void* ctx, const CanFrame* frame) {
      if (self(ctx).tx_full) {
        return false;
      }
      self(ctx).sent.push_back(*frame);
      return true;
    };
    io.can_status = [](void* ctx, GatewayCanStatus* status) { *status = self(ctx).status; };
    io.led_set = [](void* ctx, bool on) {
      self(ctx).led = on;
      self(ctx).led_changes++;
    };
    io.system_reset = [](void* ctx) { self(ctx).system_resets++; };
    io.unique_id = [](void* ctx, uint8_t out[12]) {
      (void)ctx;
      for (uint8_t i = 0; i < 12; i++) {
        out[i] = static_cast<uint8_t>(0xA0 + i);
      }
    };
    return io;
  }

  std::string take_output() {
    std::string out = serial_out;
    serial_out.clear();
    return out;
  }
};

struct Fixture {
  FakeBoard board;
  Gateway gw{};
  Fixture() {
    GatewayIo io = board.io();
    gateway_init(&gw, &io);
  }
  void serial(const std::string& text) { gateway_on_serial_data(&gw, text.data(), text.size()); }
  void can_rx(uint32_t id, std::vector<uint8_t> data) {
    CanFrame frame;
    can_frame_set(&frame, id, false, data.data(), static_cast<uint8_t>(data.size()));
    gateway_on_can_frame(&gw, &frame);
  }
  // Delivers every frame the gateway sent back to it, as the controller does in loopback mode.
  void loop_back_sent() {
    std::vector<CanFrame> frames = board.sent;
    board.sent.clear();
    for (const CanFrame& f : frames) {
      gateway_on_can_frame(&gw, &f);
    }
  }
};

}  // namespace

TEST_CASE_METHOD(Fixture, "Gateway answers identification commands", "[gateway]") {
  serial("V\rN\r");
  CHECK(board.take_output() == "V0100\rNG0B1\r");
  serial("bogus\r");
  CHECK(board.take_output() == "\a");
}

TEST_CASE_METHOD(Fixture, "Gateway opens the channel with the configured bitrate and mode", "[gateway]") {
  serial("S4\r");
  CHECK(board.take_output() == "\r");
  serial("l\r");
  CHECK(board.take_output() == "\r");
  CHECK(board.started);
  CHECK(board.bitrate == 125000);
  CHECK(board.mode == GATEWAY_CAN_LOOPBACK);

  serial("S6\r");  // bitrate cannot change while open
  CHECK(board.take_output() == "\a");
  serial("O\r");  // already open
  CHECK(board.take_output() == "\a");
  serial("C\r");
  CHECK(board.take_output() == "\r");
  CHECK_FALSE(board.started);
}

TEST_CASE_METHOD(Fixture, "Gateway refuses frames while closed or listening", "[gateway]") {
  serial("t100101\r");
  CHECK(board.take_output() == "\a");
  serial("L\r");
  board.take_output();
  serial("t100101\r");
  CHECK(board.take_output() == "\a");
  CHECK(board.sent.empty());
}

TEST_CASE_METHOD(Fixture, "Gateway sends serial frames on CAN and reports CAN frames", "[gateway]") {
  serial("O\r");
  board.take_output();

  serial("t2001AB\rT1F3344551CD\r");
  CHECK(board.take_output() == "z\rZ\r");
  REQUIRE(board.sent.size() == 2);
  CHECK(board.sent[0].id == 0x200);
  CHECK(board.sent[1].extended);
}

TEST_CASE_METHOD(Fixture, "Gateway reports frames received from the CAN bus", "[gateway]") {
  serial("O\r");
  board.take_output();
  can_rx(0x321, {0x11, 0x22});
  CHECK(board.take_output() == "t32121122\r");
}

TEST_CASE_METHOD(Fixture, "Gateway suppresses loopback echoes", "[gateway]") {
  serial("l\r");
  board.take_output();

  serial("t100101\rt3000\r");
  CHECK(board.take_output() == "z\rz\r");
  loop_back_sent();
  CHECK(board.take_output().empty());  // own frames are not reported twice
  CHECK(gw.echoes == 2);
  CHECK(gw.rx_frames == 2);

  can_rx(0x123, {0x01});  // a frame that was not sent by us is reported
  CHECK(board.take_output() == "t123101\r");
}

TEST_CASE_METHOD(Fixture, "Gateway LED follows the lamp command once open", "[gateway]") {
  serial("O\r");
  board.take_output();
  CHECK_FALSE(board.led);

  serial("t100101\r");  // LAMP_CMD on from the virtual bus
  CHECK(board.led);
  can_rx(0x100, {0x00});  // LAMP_CMD off from the CAN bus
  CHECK_FALSE(board.led);
}

TEST_CASE_METHOD(Fixture, "Gateway button toggles the switch on both buses", "[gateway]") {
  gateway_on_button(&gw);  // closed: ignored
  CHECK(board.take_output().empty());

  serial("l\r");
  board.take_output();
  gateway_on_button(&gw);
  CHECK(board.take_output() == "t200101\r");
  REQUIRE(board.sent.size() == 1);
  CHECK(board.sent[0].data[0] == 0x01);

  loop_back_sent();  // echo of the switch frame must not be reported again
  CHECK(board.take_output().empty());

  gateway_on_button(&gw);
  CHECK(board.take_output() == "t200100\r");
}

TEST_CASE_METHOD(Fixture, "Gateway status flags and statistics", "[gateway]") {
  serial("O\r");
  board.take_output();
  board.tx_full = true;
  serial("t1000\r");
  CHECK(board.take_output() == "\a");

  board.status.error_passive = true;
  board.status.tx_error_count = 0x80;
  serial("F\r");
  CHECK(board.take_output() == "F22\r");  // TX queue full + error passive
  serial("F\r");
  CHECK(board.take_output() == "F20\r");  // sticky flag cleared, error passive persists

  serial("Q\r");
  const std::string q = board.take_output();
  REQUIRE(q.size() == 32);  // Q + 3x8 counters + TEC + REC + flags + CR
  CHECK(q.substr(0, 1) == "Q");
  CHECK(q.substr(25, 2) == "80");  // TEC
  CHECK(q.back() == '\r');
}

TEST_CASE_METHOD(Fixture, "Gateway blinks while the channel is closed", "[gateway]") {
  const int before = board.led_changes;
  gateway_tick(&gw, 0);
  gateway_tick(&gw, 499);
  gateway_tick(&gw, 500);
  gateway_tick(&gw, 1000);
  CHECK(board.led_changes - before == 2);

  serial("O\r");
  const int open_changes = board.led_changes;
  gateway_tick(&gw, 5000);
  CHECK(board.led_changes == open_changes);
}

// --- Diagnostics (UDS on 0x7E1 -> 0x7E9) ------------------------------------

TEST_CASE_METHOD(Fixture, "Gateway answers UDS requests from the serial side", "[gateway][uds]") {
  serial("l\r");
  board.take_output();
  // ReadDataByIdentifier 0100 (lamp): single frame, padded with 0xCC.
  serial("t7E18" "03220100CCCCCCCC\r");
  CHECK(board.take_output() == "t7E980462010000CCCCCC\rz\r");
  // The response also went to the CAN bus; its loopback echo is suppressed.
  REQUIRE(board.sent.size() == 2);
  CHECK(board.sent[1].id == GATEWAY_DIAG_RESPONSE_ID);
  loop_back_sent();
  CHECK(board.take_output().empty());

  // Functional TesterPresent on 0x7DF.
  serial("t7DF8023E00CCCCCCCCCC\r");
  CHECK(board.take_output() == "t7E98027E00CCCCCCCCCC\rz\r");
  // Not addressed to the gateway: plain SLCAN forwarding.
  serial("t7E08023E00CCCCCCCCCC\r");
  CHECK(board.take_output() == "z\r");
}

TEST_CASE_METHOD(Fixture, "Gateway sends long UDS responses with flow control", "[gateway][uds]") {
  serial("O\r");
  board.take_output();
  gateway_tick(&gw, 1);
  // Serial number = 12-byte unique id -> 15 byte response: first frame, wait for flow control.
  serial("t7E18" "0322F18CCCCCCCCC\r");
  CHECK(board.take_output() == "t7E98100F62F18CA0A1A2\rz\r");
  // FC: continue, no block limit, STmin 0 -> the consecutive frames follow right away.
  serial("t7E18300000CCCCCCCCCC\r");
  CHECK(board.take_output() == "t7E9821A3A4A5A6A7A8A9\rt7E98" "22AAABCCCCCCCCCC\rz\r");
  gateway_tick(&gw, 2);
  CHECK(board.take_output().empty());
}

TEST_CASE_METHOD(Fixture, "Gateway reports bus off as DTC U0073-88", "[gateway][uds]") {
  serial("O\r");
  board.take_output();
  gateway_tick(&gw, 1);
  serial("t7E18" "03190209CCCCCCCC\r");  // DTCs with status failed or confirmed
  CHECK(board.take_output() == "t7E98" "0359027FCCCCCCCC\rz\r");

  board.status.bus_off = true;
  gateway_tick(&gw, 2);
  serial("t7E18" "03190209CCCCCCCC\r");
  CHECK(board.take_output() == "t7E980759027FC073882F\rz\r");

  // Live data: TEC, REC, state 3 = bus off.
  board.status.tx_error_count = 0xF8;
  serial("t7E18" "03220200CCCCCCCC\r");
  CHECK(board.take_output() == "t7E980662020" "0F80003CC\rz\r");
}

TEST_CASE_METHOD(Fixture, "Gateway ECUReset: soft resets counters, hard restarts the MCU later", "[gateway][uds]") {
  serial("O\r");
  serial("t1000\r");
  board.take_output();
  REQUIRE(gw.tx_frames == 1);

  serial("t7E18021103CCCCCCCCCC\r");  // soft reset
  CHECK(board.take_output() == "t7E980251" "03CCCCCCCCCC\rz\r");
  gateway_tick(&gw, 10);
  CHECK(gw.soft_resets == 1);
  CHECK(gw.tx_frames == 0);
  CHECK(board.system_resets == 0);

  serial("t7E18021101CCCCCCCCCC\r");  // hard reset
  board.take_output();
  gateway_tick(&gw, 20);  // response is out, reset scheduled
  gateway_tick(&gw, 69);
  CHECK(board.system_resets == 0);
  gateway_tick(&gw, 70);
  CHECK(board.system_resets == 1);
}

TEST_CASE_METHOD(Fixture, "Gateway answers testers on the CAN bus, not while closed", "[gateway][uds]") {
  can_rx(0x7E1, {0x03, 0x22, 0x01, 0x01, 0xCC, 0xCC, 0xCC, 0xCC});
  CHECK(board.take_output().empty());  // channel closed: no response anywhere
  CHECK(board.sent.empty());

  serial("O\r");
  board.take_output();
  can_rx(0x7E1, {0x03, 0x22, 0x01, 0x01, 0xCC, 0xCC, 0xCC, 0xCC});
  // The request itself is reported to the host like any CAN frame, then the response.
  CHECK(board.take_output() == "t7E1803220101CCCCCCCC\rt7E980462010100CCCCCC\r");
  REQUIRE(board.sent.size() == 1);
  CHECK(board.sent[0].id == GATEWAY_DIAG_RESPONSE_ID);
}
