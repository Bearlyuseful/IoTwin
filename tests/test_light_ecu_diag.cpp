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
  CHECK(f.step() == 1);
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
  CHECK_FALSE(light_ecu_fault_from_name("smoke", &fault));
  CHECK(std::string(light_ecu_fault_name(LIGHT_FAULT_LAMP_OPEN)) == "lamp output open circuit");
}
