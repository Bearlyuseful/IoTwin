#include <catch2/catch_all.hpp>

#include <cstring>
#include <string>

#include "can_frame.h"

namespace {

bool parse(const std::string& text, CanFrame& frame) {
  return can_frame_from_text(text.data(), text.size(), &frame);
}

std::string encode(const CanFrame& frame) {
  char buffer[CAN_FRAME_TEXT_MAX];
  int length = can_frame_to_text(&frame, buffer, sizeof(buffer));
  REQUIRE(length >= 0);
  return std::string(buffer, static_cast<size_t>(length));
}

}  // namespace

TEST_CASE("CAN frame encodes standard data frames", "[can_frame]") {
  CanFrame frame;
  const uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  REQUIRE(can_frame_set(&frame, 0x123, false, data, 4));
  CHECK(encode(frame) == "123#DEADBEEF");

  REQUIRE(can_frame_set(&frame, 0x7, false, nullptr, 0));
  CHECK(encode(frame) == "007#");
}

TEST_CASE("CAN frame encodes extended and remote frames", "[can_frame]") {
  CanFrame frame;
  const uint8_t data[1] = {0x11};
  REQUIRE(can_frame_set(&frame, 0x1F334455, true, data, 1));
  CHECK(encode(frame) == "1F334455#11");

  can_frame_clear(&frame);
  frame.id = 0x123;
  frame.rtr = true;
  CHECK(encode(frame) == "123#R");
  frame.dlc = 4;
  CHECK(encode(frame) == "123#R4");
}

TEST_CASE("CAN frame text round trip", "[can_frame]") {
  const char* samples[] = {"123#DEADBEEF", "7FF#0011223344556677", "000#", "1FFFFFFF#AA",
                           "00000001#",    "123#R",                "123#R8"};
  for (const char* sample : samples) {
    INFO(sample);
    CanFrame frame;
    REQUIRE(parse(sample, frame));
    CHECK(encode(frame) == sample);
  }
}

TEST_CASE("CAN frame parser accepts lower case and byte separators", "[can_frame]") {
  CanFrame frame;
  REQUIRE(parse("1ab#de.ad.be.ef", frame));
  CHECK(frame.id == 0x1AB);
  CHECK_FALSE(frame.extended);
  CHECK(frame.dlc == 4);
  CHECK(encode(frame) == "1AB#DEADBEEF");
}

TEST_CASE("CAN frame parser only reads the given length", "[can_frame]") {
  const char text[] = "123#ABCD-garbage";
  CanFrame frame;
  REQUIRE(can_frame_from_text(text, 8, &frame));
  CHECK(frame.dlc == 2);
}

TEST_CASE("CAN frame parser rejects malformed input", "[can_frame]") {
  const char* invalid[] = {
      "",              // empty
      "123",           // no separator
      "12#00",         // identifier too short
      "1234#00",       // neither 3 nor 8 digits
      "800#00",        // standard identifier out of range
      "20000000#00",   // extended identifier out of range
      "12G#00",        // non-hex identifier
      "123#0",         // odd number of digits
      "123#XYZW",      // non-hex payload
      "123#001122334455667788",  // 9 bytes
      "123##1122",     // CAN FD
      "123#R9",        // RTR DLC out of range
      "123#R12",       // RTR with garbage
      "123#.11",       // leading separator
      "123#11..22",    // double separator
      "123#11.",       // trailing separator
      "123#11 22",     // whitespace
  };
  for (const char* text : invalid) {
    INFO('"' << text << '"');
    CanFrame frame;
    CHECK_FALSE(parse(text, frame));
  }
}

TEST_CASE("CAN frame failed parse leaves the output untouched", "[can_frame]") {
  CanFrame frame;
  REQUIRE(parse("321#01", frame));
  CHECK_FALSE(parse("bogus", frame));
  CHECK(frame.id == 0x321);
  CHECK(frame.data[0] == 0x01);
}

TEST_CASE("CAN frame encoder rejects invalid frames and small buffers", "[can_frame]") {
  CanFrame frame;
  can_frame_clear(&frame);
  frame.id = 0x123;
  frame.dlc = 9;
  char buffer[CAN_FRAME_TEXT_MAX];
  CHECK(can_frame_to_text(&frame, buffer, sizeof(buffer)) == -1);

  frame.dlc = 4;
  char small[6];
  CHECK(can_frame_to_text(&frame, small, sizeof(small)) == -1);
}

TEST_CASE("CAN frame equality ignores bytes beyond the DLC", "[can_frame]") {
  CanFrame a;
  CanFrame b;
  const uint8_t data[2] = {1, 2};
  can_frame_set(&a, 0x10, false, data, 2);
  can_frame_set(&b, 0x10, false, data, 2);
  b.data[5] = 0x55;
  CHECK(can_frame_equals(&a, &b));
  b.data[1] = 3;
  CHECK_FALSE(can_frame_equals(&a, &b));
}

TEST_CASE("CAN frame set validates its arguments", "[can_frame]") {
  CanFrame frame;
  const uint8_t data[8] = {};
  CHECK_FALSE(can_frame_set(&frame, 0x800, false, data, 1));
  CHECK(can_frame_set(&frame, 0x800, true, data, 1));
  CHECK_FALSE(can_frame_set(&frame, 0x123, false, data, 9));
  CHECK_FALSE(can_frame_set(&frame, 0x123, false, nullptr, 2));
}
