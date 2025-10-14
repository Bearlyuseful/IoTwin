#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

// "extern C" damit C-Header in C++ funktionieren:
extern "C" {
#include "../src/simulators/can_sim/can_bus.h"
}

TEST_CASE("CAN bus initializes", "[can]") { REQUIRE(can_init() == true); }

TEST_CASE("CAN bus can send data", "[can]") {
  REQUIRE(can_send(42, 255) == true);
}