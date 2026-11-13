// Diagnostics of the simulated light ECU: identification, fault monitors, DTCs, write and reset.

#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

#include "light_ecu.h"
#include "light_ecu_diag.h"

namespace {

using Bytes = std::vector<uint8_t>;

struct Fixture {
  LightEcu ecu{};
  LightEcuDiag diag{};
  std::vector<CanFrame> sent;
  uint64_t now_ms = 0;

  static bool send(void* context, const CanFrame* frame) {
    static_cast<Fixture*>(context)->sent.push_back(*frame);
    return true;
  }

  Fixture() {
    light_ecu_init(&ecu, LIGHT_ECU_DEFAULT_CYCLE_MS);
    light_ecu_diag_init(&diag, &ecu, &Fixture::send, this, "1.2.3");
  }

  Bytes request(const Bytes& data) {
    uint8_t response[UDS_MAX_RESPONSE];
    const size_t length = uds_server_process(&diag.uds, data.data(), data.size(), false, response,
                                             sizeof(response), now_ms * 1000);
    return Bytes(response, response + length);
  }

  // One control step; returns the LAMP_CMD payload if one was sent, -1 otherwise.
  int step(uint64_t advance_ms = 10) {
    now_ms += advance_ms;
    light_ecu_diag_step(&diag, now_ms);
    CanFrame out;
    if (!light_ecu_step(&ecu, now_ms, &out, nullptr)) {
      return -1;
    }
    return out.data[0];
  }

  uint8_t dtc_status(uint32_t code) { return uds_server_find_dtc(&diag.uds, code)->status; }
};

Bytes text_response(uint16_t did, const std::string& text) {
  Bytes expected = {0x62, static_cast<uint8_t>(did >> 8), static_cast<uint8_t>(did)};
  expected.insert(expected.end(), text.begin(), text.end());
  return expected;
}

}  // namespace

TEST_CASE("Light ECU diagnostics: identification", "[light_ecu_diag]") {
  Fixture f;
  CHECK(f.request({0x22, 0xF1, 0x90}) == text_response(0xF190, "IOTWIN0LIGHTDEMO1"));
  CHECK(f.request({0x22, 0xF1, 0x89}) == text_response(0xF189, "1.2.3"));
  CHECK(f.request({0x22, 0xF1, 0x87}) == text_response(0xF187, "IOTWIN-LIGHT-ECU"));
  CHECK(f.request({0x22, 0x01, 0x02}) == Bytes{0x62, 0x01, 0x02, 0x01, 0xF4});  // 500 ms cycle
  CHECK(f.request({0x22, 0x01, 0x04}) == Bytes{0x62, 0x01, 0x04, 0x31, 0x38});  // 12600 mV
}

TEST_CASE("Light ECU diagnostics: short circuit switches the output off", "[light_ecu_diag]") {
  Fixture f;
  light_ecu_set_lamp(&f.ecu, true);
  CHECK(f.step() == 1);
  CHECK(f.dtc_status(LIGHT_ECU_DTC_LAMP_SHORT) == 0x00);  // tested, passed

  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_LAMP_SHORT, true);
  CHECK(f.step() == 0);  // output inhibited, LAMP_CMD reports the real output
  CHECK(f.ecu.lamp_on);  // the command is still "on"
  CHECK(f.request({0x22, 0x01, 0x00}) == Bytes{0x62, 0x01, 0x00, 0x00});
  CHECK(f.dtc_status(LIGHT_ECU_DTC_LAMP_SHORT) == 0x2F);
  CHECK(f.request({0x19, 0x02, 0x09}) == Bytes{0x59, 0x02, 0x7F, 0x90, 0x01, 0x11, 0x2F});

  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_LAMP_SHORT, false);
  CHECK(f.step() == -1);     // the smart switch retries only every 100 ms
  CHECK(f.step(100) == 1);   // retry succeeds, lamp back
  CHECK(f.dtc_status(LIGHT_ECU_DTC_LAMP_SHORT) == 0x2E);  // stays confirmed until cleared
  CHECK(f.request({0x14, 0xFF, 0xFF, 0xFF}) == Bytes{0x54});
  CHECK(f.request({0x19, 0x02, 0x09}) == Bytes{0x59, 0x02, 0x7F});
}

TEST_CASE("Light ECU diagnostics: open load is only detected with the lamp on", "[light_ecu_diag]") {
  Fixture f;
  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_LAMP_OPEN, true);
  f.step();
  CHECK(f.dtc_status(LIGHT_ECU_DTC_LAMP_OPEN) == 0x50);  // not tested yet
  light_ecu_set_lamp(&f.ecu, true);
  f.step();
  CHECK((f.dtc_status(LIGHT_ECU_DTC_LAMP_OPEN) & UDS_DTC_CONFIRMED) != 0);
}

TEST_CASE("Light ECU diagnostics: supply voltage", "[light_ecu_diag]") {
  Fixture f;
  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_SUPPLY_LOW, true);
  f.step();
  CHECK(f.request({0x22, 0x01, 0x04}) == Bytes{0x62, 0x01, 0x04, 0x26, 0x48});  // 9800 mV
  CHECK((f.dtc_status(LIGHT_ECU_DTC_SUPPLY_LOW) & UDS_DTC_TEST_FAILED) != 0);
}

TEST_CASE("Light ECU diagnostics: write blink period and cycle time", "[light_ecu_diag]") {
  Fixture f;
  CHECK(f.request({0x2E, 0x01, 0x03, 0x01, 0x2C}) == Bytes{0x7F, 0x2E, 0x7F});  // default session
  f.request({0x10, 0x03});
  CHECK(f.request({0x2E, 0x01, 0x03, 0x01, 0x2C}) == Bytes{0x6E, 0x01, 0x03});
  CHECK(f.ecu.blink_period_ms == 300);
  CHECK(f.request({0x2E, 0x01, 0x02, 0x00, 0x00}) == Bytes{0x6E, 0x01, 0x02});
  CHECK(f.ecu.cycle_ms == 0);
  CHECK(f.request({0x2E, 0x01, 0x03, 0x00, 0x0A}) == Bytes{0x7F, 0x2E, 0x31});  // 10 ms too short
  CHECK(f.request({0x2E, 0x01, 0x03, 0x01}) == Bytes{0x7F, 0x2E, 0x13});
  CHECK(f.request({0x2E, 0xF1, 0x90, 0x41}) == Bytes{0x7F, 0x2E, 0x31});  // VIN is read only
}

TEST_CASE("Light ECU diagnostics: ECU reset restarts the application, DTCs survive", "[light_ecu_diag]") {
  Fixture f;
  light_ecu_set_lamp(&f.ecu, true);
  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_SUPPLY_LOW, true);
  f.step();
  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_SUPPLY_LOW, false);

  CHECK(f.request({0x11, 0x01}) == Bytes{0x51, 0x01});
  f.step();
  CHECK(f.diag.resets == 1);
  CHECK_FALSE(f.ecu.lamp_on);
  // Confirmed and failed-since-clear remain; the new cycle has not run the test yet ...
  CHECK(f.dtc_status(LIGHT_ECU_DTC_SUPPLY_LOW) == 0x6C);
  f.step();
  // ... and passes it in the next step.
  CHECK(f.dtc_status(LIGHT_ECU_DTC_SUPPLY_LOW) == 0x2C);
}

TEST_CASE("Light ECU diagnostics: fault names", "[light_ecu_diag]") {
  LightEcuFault fault;
  CHECK(light_ecu_fault_from_name("short", &fault));
  CHECK(fault == LIGHT_FAULT_LAMP_SHORT);
  CHECK(light_ecu_fault_from_name("supply", &fault));
  CHECK(fault == LIGHT_FAULT_SUPPLY_LOW);
  CHECK(light_ecu_fault_from_name("overload", &fault));
  CHECK(fault == LIGHT_FAULT_OVERLOAD);
  CHECK_FALSE(light_ecu_fault_from_name("smoke", &fault));
  CHECK(std::string(light_ecu_fault_name(LIGHT_FAULT_LAMP_OPEN)) == "lamp output open circuit");
}

TEST_CASE("Light ECU output stage: current follows the brightness", "[light_ecu_diag][power]") {
  Fixture f;
  light_ecu_set_lamp(&f.ecu, true);
  f.step();
  // 12.6 V / (6.857 + 0.05 + 0.02) Ohm = 1.819 A at 100 %
  CHECK(f.diag.reading.current_a == Catch::Approx(1.819).epsilon(0.002));
  CHECK(f.diag.reading.supply_v == Catch::Approx(12.6 - 1.819 * 0.05).epsilon(0.001));
  CHECK(f.request({0x22, 0x01, 0x06}) == Bytes{0x62, 0x01, 0x06, 0x07, 0x1B});  // 1819 mA

  light_ecu_set_level(&f.ecu, 25);
  f.step();
  CHECK(f.diag.reading.current_a == Catch::Approx(1.819 * 0.25).epsilon(0.002));
  CHECK(f.request({0x22, 0x01, 0x05}) == Bytes{0x62, 0x01, 0x05, 25});

  f.request({0x10, 0x03});
  CHECK(f.request({0x2E, 0x01, 0x05, 60}) == Bytes{0x6E, 0x01, 0x05});
  CHECK(f.ecu.level == 60);
  CHECK(f.request({0x2E, 0x01, 0x05, 101}) == Bytes{0x7F, 0x2E, 0x31});

  light_ecu_set_lamp(&f.ecu, false);
  f.step();
  CHECK(f.diag.reading.current_a == 0.0);
  CHECK(f.dtc_status(LIGHT_ECU_DTC_OVERCURRENT) == 0x00);
  CHECK(f.dtc_status(LIGHT_ECU_DTC_FUSE_BLOWN) == 0x00);
}

TEST_CASE("Light ECU output stage: overload heats the fuse until it blows", "[light_ecu_diag][power]") {
  Fixture f;
  light_ecu_set_lamp(&f.ecu, true);
  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_OVERLOAD, true);
  f.step();
  CHECK(f.diag.reading.current_a == Catch::Approx(5.35).epsilon(0.01));  // 63 W bulb
  for (int i = 0; i < 25; i++) {
    f.step();
  }
  CHECK((f.dtc_status(LIGHT_ECU_DTC_OVERCURRENT) & UDS_DTC_CONFIRMED) != 0);  // after 200 ms
  CHECK_FALSE(f.diag.reading.fuse_blown);

  int steps = 0;
  int lamp_cmd = -1;
  while (!f.diag.reading.fuse_blown && steps < 500) {
    lamp_cmd = f.step();
    steps++;
  }
  REQUIRE(f.diag.reading.fuse_blown);
  // (5.35^2 - 3^2) A^2 = 19.6 A^2 per second against 40 A^2 s: about two seconds
  CHECK(steps * 10 + 260 > 1700);
  CHECK(steps * 10 + 260 < 2500);
  CHECK(lamp_cmd == 0);  // lamp dark, reported in LAMP_CMD right away
  CHECK((f.dtc_status(LIGHT_ECU_DTC_FUSE_BLOWN) & UDS_DTC_TEST_FAILED) != 0);
  CHECK(f.request({0x22, 0x01, 0x07}) == Bytes{0x62, 0x01, 0x07, 0x01, 100});

  f.step();  // the step that blew the fuse still conducted part of the time
  CanFrame status;
  f.diag.last_status_ms = 0;
  REQUIRE(light_ecu_diag_status(&f.diag, f.now_ms, &status));
  CHECK(status.id == LIGHT_ECU_ID_LAMP_STATUS);
  CHECK(status.dlc == 6);
  CHECK(status.data[0] == 0);  // no current
  CHECK((status.data[5] & LIGHT_ECU_STATUS_FUSE_BLOWN) != 0);

  // Removing the overload alone does not help: the fuse has to be replaced.
  light_ecu_diag_set_fault(&f.diag, LIGHT_FAULT_OVERLOAD, false);
  f.step();
  CHECK(f.diag.reading.fuse_blown);
  light_ecu_diag_replace_fuse(&f.diag);
  CHECK(f.step() == 1);
  CHECK((f.dtc_status(LIGHT_ECU_DTC_FUSE_BLOWN) & UDS_DTC_TEST_FAILED) == 0);
  CHECK((f.dtc_status(LIGHT_ECU_DTC_FUSE_BLOWN) & UDS_DTC_CONFIRMED) != 0);
}

TEST_CASE("Light ECU output stage: LAMP_STATUS every 200 ms", "[light_ecu_diag][power]") {
  Fixture f;
  light_ecu_set_lamp(&f.ecu, true);
  light_ecu_set_level(&f.ecu, 50);
  f.step();
  CanFrame status;
  REQUIRE(light_ecu_diag_status(&f.diag, f.now_ms, &status));
  const unsigned current_ma = status.data[0] | (status.data[1] << 8);
  const unsigned supply_mv = status.data[2] | (status.data[3] << 8);
  CHECK(current_ma == Catch::Approx(910).margin(3));
  CHECK(supply_mv == Catch::Approx(12555).margin(3));
  CHECK(status.data[4] == 50);
  CHECK(status.data[5] == 0);
  CHECK_FALSE(light_ecu_diag_status(&f.diag, f.now_ms + 199, &status));
  CHECK(light_ecu_diag_status(&f.diag, f.now_ms + 200, &status));
}
