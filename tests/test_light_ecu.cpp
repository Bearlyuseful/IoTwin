#include <catch2/catch_all.hpp>

#include "light_ecu.h"

namespace {

CanFrame switch_frame(bool on) {
  CanFrame frame;
  const uint8_t data[1] = {static_cast<uint8_t>(on ? 1 : 0)};
  REQUIRE(can_frame_set(&frame, LIGHT_ECU_ID_SWITCH_STATE, false, data, 1));
  return frame;
}

}  // namespace

TEST_CASE("Light ECU announces its initial state immediately", "[light_ecu]") {
  LightEcu ecu;
  light_ecu_init(&ecu, 500);
  CanFrame out;
  bool changed = false;
  REQUIRE(light_ecu_step(&ecu, 1000, &out, &changed));
  CHECK(changed);
  CHECK(out.id == LIGHT_ECU_ID_LAMP_CMD);
  CHECK(out.dlc == 1);
  CHECK(out.data[0] == 0);
}

TEST_CASE("Light ECU sends LAMP_CMD cyclically", "[light_ecu]") {
  LightEcu ecu;
  light_ecu_init(&ecu, 500);
  CanFrame out;
  bool changed = false;
  REQUIRE(light_ecu_step(&ecu, 1000, &out, &changed));
  CHECK_FALSE(light_ecu_step(&ecu, 1499, &out, &changed));
  REQUIRE(light_ecu_step(&ecu, 1500, &out, &changed));
  CHECK_FALSE(changed);
  CHECK(out.data[0] == 0);
}

TEST_CASE("Light ECU follows the wall switch", "[light_ecu]") {
  LightEcu ecu;
  light_ecu_init(&ecu, 500);
  CanFrame out;
  bool changed = false;
  light_ecu_step(&ecu, 0, &out, &changed);

  CanFrame on = switch_frame(true);
  REQUIRE(light_ecu_on_frame(&ecu, &on));
  REQUIRE(light_ecu_step(&ecu, 10, &out, &changed));
  CHECK(changed);
  CHECK(out.data[0] == 1);

  CanFrame off = switch_frame(false);
  REQUIRE(light_ecu_on_frame(&ecu, &off));
  REQUIRE(light_ecu_step(&ecu, 20, &out, &changed));
  CHECK(out.data[0] == 0);
}

TEST_CASE("Light ECU ignores unrelated and malformed frames", "[light_ecu]") {
  LightEcu ecu;
  light_ecu_init(&ecu, 500);
  CanFrame frame;
  const uint8_t data[1] = {1};

  can_frame_set(&frame, 0x201, false, data, 1);
  CHECK_FALSE(light_ecu_on_frame(&ecu, &frame));

  can_frame_set(&frame, LIGHT_ECU_ID_SWITCH_STATE, false, nullptr, 0);
  CHECK_FALSE(light_ecu_on_frame(&ecu, &frame));

  can_frame_set(&frame, LIGHT_ECU_ID_SWITCH_STATE, true, data, 1);
  CHECK_FALSE(light_ecu_on_frame(&ecu, &frame));

  CHECK_FALSE(ecu.lamp_on);
}

TEST_CASE("Light ECU blinks and the switch stops blinking", "[light_ecu]") {
  LightEcu ecu;
  light_ecu_init(&ecu, 0);
  light_ecu_set_blink(&ecu, 100);
  CanFrame out;
  bool changed = false;

  REQUIRE(light_ecu_step(&ecu, 1000, &out, &changed));  // initial state
  CHECK(out.data[0] == 0);
  CHECK_FALSE(light_ecu_step(&ecu, 1099, &out, &changed));
  REQUIRE(light_ecu_step(&ecu, 1100, &out, &changed));
  CHECK(out.data[0] == 1);
  REQUIRE(light_ecu_step(&ecu, 1200, &out, &changed));
  CHECK(out.data[0] == 0);

  CanFrame on = switch_frame(true);
  light_ecu_on_frame(&ecu, &on);
  REQUIRE(light_ecu_step(&ecu, 1210, &out, &changed));
  CHECK(out.data[0] == 1);
  CHECK_FALSE(light_ecu_step(&ecu, 1400, &out, &changed));  // no more toggling
  CHECK(ecu.lamp_on);
}

TEST_CASE("Light ECU resends on request", "[light_ecu]") {
  LightEcu ecu;
  light_ecu_init(&ecu, 0);
  CanFrame out;
  REQUIRE(light_ecu_step(&ecu, 0, &out, nullptr));
  CHECK_FALSE(light_ecu_step(&ecu, 10, &out, nullptr));
  light_ecu_request_send(&ecu);
  CHECK(light_ecu_step(&ecu, 20, &out, nullptr));
}
