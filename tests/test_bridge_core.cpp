#include <catch2/catch_all.hpp>

#include <string>

#include "bridge_core.h"

namespace {

std::string adapter_to_bus(const std::string& line) {
  char out[64];
  int n = bridge_adapter_to_bus(line.data(), line.size(), out, sizeof(out));
  return n < 0 ? std::string("<invalid>") : std::string(out, static_cast<size_t>(n));
}

std::string bus_to_adapter(const std::string& line) {
  char out[64];
  int n = bridge_bus_to_adapter(line.data(), line.size(), out, sizeof(out));
  return n < 0 ? std::string("<invalid>") : std::string(out, static_cast<size_t>(n));
}

}  // namespace

TEST_CASE("Bridge converts adapter frames to bus lines", "[bridge]") {
  CHECK(adapter_to_bus("t2001" "01") == "200#01");
  CHECK(adapter_to_bus("T1F3344552AABB") == "1F334455#AABB");
  CHECK(adapter_to_bus("r1230") == "123#R");
  CHECK(adapter_to_bus("t1001011A2B") == "100#01");  // timestamp dropped
}

TEST_CASE("Bridge ignores adapter answers that are no frames", "[bridge]") {
  CHECK(adapter_to_bus("") == "<invalid>");
  CHECK(adapter_to_bus("z") == "<invalid>");
  CHECK(adapter_to_bus("V0100") == "<invalid>");
  CHECK(adapter_to_bus("t12") == "<invalid>");
}

TEST_CASE("Bridge converts bus lines to adapter commands", "[bridge]") {
  CHECK(bus_to_adapter("100#01") == "t100101\r");
  CHECK(bus_to_adapter("1F334455#") == "T1F3344550\r");
  CHECK(bus_to_adapter("7FF#R2") == "r7FF2\r");
  CHECK(bus_to_adapter("garbage") == "<invalid>");
}

TEST_CASE("Bridge setup commands", "[bridge]") {
  CHECK(std::string(bridge_bitrate_command(500000)) == "S6\r");
  CHECK(std::string(bridge_bitrate_command(1000000)) == "S8\r");
  CHECK(bridge_bitrate_command(33333) == nullptr);
  CHECK(std::string(bridge_open_command(BRIDGE_MODE_NORMAL)) == "O\r");
  CHECK(std::string(bridge_open_command(BRIDGE_MODE_LOOPBACK)) == "l\r");
  CHECK(std::string(bridge_open_command(BRIDGE_MODE_LISTEN)) == "L\r");

  BridgeMode mode{};
  CHECK(bridge_parse_mode("loopback", &mode));
  CHECK(mode == BRIDGE_MODE_LOOPBACK);
  CHECK_FALSE(bridge_parse_mode("fast", &mode));
}
