#include <catch2/catch_all.hpp>

#include <thread>

#include "can_bus.h"
#include "log.h"

namespace {

CanFrame make_frame(uint32_t id, uint8_t first_byte) {
  CanFrame frame;
  const uint8_t data[2] = {first_byte, 0xCD};
  REQUIRE(can_frame_set(&frame, id, false, data, 2));
  return frame;
}

struct BusFixture {
  CanBus bus;
  BusFixture() {
    log_set_level(LOG_LEVEL_WARN);
    REQUIRE(can_bus_init(&bus));
  }
  ~BusFixture() { can_bus_destroy(&bus); }
};

}  // namespace

TEST_CASE_METHOD(BusFixture, "CAN bus initializes empty", "[can_bus]") {
  CHECK(can_bus_count(&bus) == 0);
  CHECK(can_bus_dropped(&bus) == 0);
}

TEST_CASE_METHOD(BusFixture, "CAN bus receive from empty buffer fails", "[can_bus]") {
  CanFrame frame;
  REQUIRE_FALSE(can_bus_receive(&bus, &frame));
}

TEST_CASE_METHOD(BusFixture, "CAN bus frame is sent and received unchanged", "[can_bus]") {
  CanFrame frame = make_frame(0x123, 0xAB);
  REQUIRE(can_bus_send(&bus, &frame));
  CHECK(can_bus_count(&bus) == 1);

  CanFrame out;
  REQUIRE(can_bus_receive(&bus, &out));
  CHECK(out.id == 0x123);
  CHECK(out.dlc == 2);
  CHECK(out.data[0] == 0xAB);
  CHECK(out.data[1] == 0xCD);
  CHECK(can_bus_count(&bus) == 0);
}

TEST_CASE_METHOD(BusFixture, "CAN bus rejects frames when full and counts them", "[can_bus]") {
  CanFrame frame = make_frame(0x123, 0xAB);
  for (size_t i = 0; i < CAN_BUS_BUFFER_SIZE; i++) {
    REQUIRE(can_bus_send(&bus, &frame));
  }
  REQUIRE_FALSE(can_bus_send(&bus, &frame));
  CHECK(can_bus_dropped(&bus) == 1);
  CHECK(can_bus_count(&bus) == CAN_BUS_BUFFER_SIZE);
}

TEST_CASE_METHOD(BusFixture, "CAN bus keeps FIFO order across the wrap-around", "[can_bus]") {
  CanFrame out;
  uint8_t next_expected = 0;
  uint8_t next_sent = 0;
  // Push and pop more frames than the capacity so head/tail wrap several times.
  for (int round = 0; round < 5; round++) {
    for (size_t i = 0; i < CAN_BUS_BUFFER_SIZE - 3; i++) {
      CanFrame frame = make_frame(0x100, next_sent++);
      REQUIRE(can_bus_send(&bus, &frame));
    }
    while (can_bus_receive(&bus, &out)) {
      REQUIRE(out.data[0] == next_expected++);
    }
  }
  CHECK(next_expected == next_sent);
}

TEST_CASE_METHOD(BusFixture, "CAN bus rejects invalid frames", "[can_bus]") {
  CanFrame frame = make_frame(0x123, 0x01);
  frame.dlc = 9;
  CHECK_FALSE(can_bus_send(&bus, &frame));
  frame.dlc = 1;
  frame.id = 0x800;  // does not fit into 11 bits
  CHECK_FALSE(can_bus_send(&bus, &frame));
  CHECK(can_bus_count(&bus) == 0);
}

TEST_CASE_METHOD(BusFixture, "CAN bus is safe for one producer and one consumer thread",
                 "[can_bus][thread]") {
  constexpr uint32_t kFrames = 20000;
  std::thread producer([this] {
    for (uint32_t i = 0; i < kFrames;) {
      CanFrame frame;
      const uint8_t data[4] = {uint8_t(i), uint8_t(i >> 8), uint8_t(i >> 16), uint8_t(i >> 24)};
      can_frame_set(&frame, 0x10, false, data, 4);
      if (can_bus_send(&bus, &frame)) {
        i++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  uint32_t expected = 0;
  bool in_order = true;
  while (expected < kFrames) {
    CanFrame out;
    if (can_bus_receive(&bus, &out)) {
      const uint32_t value = uint32_t(out.data[0]) | uint32_t(out.data[1]) << 8 |
                             uint32_t(out.data[2]) << 16 | uint32_t(out.data[3]) << 24;
      in_order = in_order && value == expected;
      expected++;
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();
  CHECK(in_order);
  CHECK(can_bus_count(&bus) == 0);
}
