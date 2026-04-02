#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

// "extern C" damit C-Header in C++ funktionieren:
extern "C" {
#include "../src/simulators/can_sim/can_bus.h"
}

TEST_CASE("CAN bus initializes", "[can]") { REQUIRE(can_bus_init() == true); }

TEST_CASE("CAN bus initializes and destroy", "[can]") { 
  can_bus_init();
  
  REQUIRE_NOTHROW(can_bus_destroy()); 
}

TEST_CASE("Can bus initializes and receives a frame from empty buffer", "[can]"){
  can_bus_init();
  CanFrame frame;
  REQUIRE(can_bus_receive(&frame) == false);
}

TEST_CASE("Can bus initialized and Frame successfully send", "[can]"){
  can_bus_init();
  CanFrame frame;
  frame.type = CAN_FRAME_STANDARD;
  frame.frame.std.identifier = 0x123;
  frame.frame.std.dlc = 2;
  frame.frame.std.data[0] = 0xAB;
  frame.frame.std.data[1] = 0xCD;

  REQUIRE(can_bus_send(&frame) == true);
}


TEST_CASE("Can bus initialized and Frame successfully send and received correctly", "[can]"){
  can_bus_init();
  CanFrame frame;
  frame.type = CAN_FRAME_STANDARD;
  frame.frame.std.identifier = 0x123;
  frame.frame.std.dlc = 2;
  frame.frame.std.data[0] = 0xAB;
  frame.frame.std.data[1] = 0xCD;

  REQUIRE(can_bus_send(&frame) == true);

  SECTION("receive returns true"){
    CanFrame out;
    REQUIRE(can_bus_receive(&out) == true);
  }

  SECTION("received frame matches send frame"){
    CanFrame out;
    can_bus_receive(&out);
    REQUIRE(out.frame.std.identifier == 0x123);
    REQUIRE(out.frame.std.data[0] == 0xAB);
    REQUIRE(out.frame.std.data[1] == 0xCD);
  }
}

TEST_CASE("Can bus initialized and sending into full buffer", "[can]") {
can_bus_init();
CanFrame frame;
frame.type = CAN_FRAME_STANDARD;
frame.frame.std.identifier = 0x123;
frame.frame.std.dlc = 2;
frame.frame.std.data[0] = 0xAB;
frame.frame.std.data[1] = 0xCD;

for (uint8_t i = 0; i < CAN_BUS_BUFFER_SIZE; i++) {
  can_bus_send(&frame);
}
REQUIRE(can_bus_send(&frame) == false);
}