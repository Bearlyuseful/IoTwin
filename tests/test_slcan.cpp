#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

#include "slcan.h"

namespace {

bool parse(const std::string& line, SlcanCommand& command) {
  return slcan_parse(line.data(), line.size(), &command);
}

std::string format(const CanFrame& frame) {
  char buffer[SLCAN_MAX_LINE];
  int length = slcan_format_frame(&frame, buffer, sizeof(buffer));
  REQUIRE(length > 0);
  return std::string(buffer, static_cast<size_t>(length));
}

struct LineCollector {
  SlcanReader reader;
  std::vector<std::string> lines;
  LineCollector() { slcan_reader_init(&reader); }
  void feed(const std::string& data) {
    slcan_reader_feed(&reader, data.data(), data.size(), &LineCollector::callback, this);
  }
  static void callback(const char* line, size_t length, void* user) {
    static_cast<LineCollector*>(user)->lines.emplace_back(line, length);
  }
};

}  // namespace

TEST_CASE("SLCAN parses standard and extended data frames", "[slcan]") {
  SlcanCommand c{};
  REQUIRE(parse("t1232DEAD", c));
  CHECK(c.type == SLCAN_CMD_FRAME);
  CHECK(c.frame.id == 0x123);
  CHECK_FALSE(c.frame.extended);
  CHECK(c.frame.dlc == 2);
  CHECK(c.frame.data[0] == 0xDE);
  CHECK(c.frame.data[1] == 0xAD);

  REQUIRE(parse("T1F3344551aa", c));
  CHECK(c.frame.id == 0x1F334455);
  CHECK(c.frame.extended);
  CHECK(c.frame.dlc == 1);
  CHECK(c.frame.data[0] == 0xAA);

  REQUIRE(parse("t7FF0", c));
  CHECK(c.frame.dlc == 0);
}

TEST_CASE("SLCAN parses remote frames and ignores timestamps", "[slcan]") {
  SlcanCommand c{};
  REQUIRE(parse("r1234", c));
  CHECK(c.frame.rtr);
  CHECK(c.frame.dlc == 4);
  REQUIRE(parse("R1F3344552", c));
  CHECK(c.frame.rtr);
  CHECK(c.frame.extended);

  REQUIRE(parse("t1001011A2B", c));  // Z1 mode: 4 hex digit timestamp
  CHECK(c.frame.dlc == 1);
  CHECK(c.frame.data[0] == 0x01);
}

TEST_CASE("SLCAN rejects malformed frames", "[slcan]") {
  const char* invalid[] = {
      "t12",          // short id
      "t1239",        // DLC 9
      "t1232DE",      // missing byte
      "t1232DEADBE",  // trailing garbage (not a 4 digit timestamp)
      "t8001",        // standard id out of range
      "T200000000",   // extended id out of range
      "t12G1",        // non-hex id
      "t1231ZZ",      // non-hex data
      "x",            // unknown command
      "S9",           // invalid bitrate index
      "Ox",           // trailing characters
  };
  for (const char* line : invalid) {
    INFO(line);
    SlcanCommand c{};
    CHECK_FALSE(parse(line, c));
  }
}

TEST_CASE("SLCAN parses control commands", "[slcan]") {
  SlcanCommand c{};
  REQUIRE(parse("O", c));
  CHECK(c.type == SLCAN_CMD_OPEN);
  REQUIRE(parse("l", c));
  CHECK(c.type == SLCAN_CMD_OPEN_LOOPBACK);
  REQUIRE(parse("L", c));
  CHECK(c.type == SLCAN_CMD_OPEN_LISTEN);
  REQUIRE(parse("C", c));
  CHECK(c.type == SLCAN_CMD_CLOSE);
  REQUIRE(parse("S6", c));
  CHECK(c.type == SLCAN_CMD_BITRATE);
  CHECK(c.bitrate == 500000);
  REQUIRE(parse("Z1", c));
  CHECK(c.type == SLCAN_CMD_TIMESTAMP);
  CHECK(c.enable);
  REQUIRE(parse("Q", c));
  CHECK(c.type == SLCAN_CMD_STATISTICS);
}

TEST_CASE("SLCAN formats frames and round-trips them", "[slcan]") {
  CanFrame frame;
  const uint8_t data[2] = {0xDE, 0xAD};
  can_frame_set(&frame, 0x123, false, data, 2);
  CHECK(format(frame) == "t1232DEAD\r");

  can_frame_set(&frame, 0x1F334455, true, data, 1);
  CHECK(format(frame) == "T1F3344551DE\r");

  can_frame_clear(&frame);
  frame.id = 0x7FF;
  frame.rtr = true;
  frame.dlc = 3;
  CHECK(format(frame) == "r7FF3\r");

  const char* samples[] = {"t0000", "t7FF80011223344556677", "T0000000111F", "R1FFFFFFF0"};
  for (const char* sample : samples) {
    INFO(sample);
    SlcanCommand c{};
    REQUIRE(parse(sample, c));
    CHECK(format(c.frame) == std::string(sample) + "\r");
  }
}

TEST_CASE("SLCAN bitrate table", "[slcan]") {
  CHECK(slcan_bitrate_from_index(0) == 10000);
  CHECK(slcan_bitrate_from_index(8) == 1000000);
  CHECK(slcan_bitrate_from_index(9) == 0);
  CHECK(slcan_index_from_bitrate(500000) == 6);
  CHECK(slcan_index_from_bitrate(33333) == -1);
}

TEST_CASE("SLCAN reader splits CR, LF, CR LF and reports BEL", "[slcan]") {
  LineCollector c;
  c.feed("t1001");
  c.feed("01\r\a\rV\r\nO\n");
  REQUIRE(c.lines.size() == 5);
  CHECK(c.lines[0] == "t100101");
  CHECK(c.lines[1] == "\a");
  CHECK(c.lines[2].empty());  // plain CR = ok answer
  CHECK(c.lines[3] == "V");   // CR LF counts once
  CHECK(c.lines[4] == "O");
}

TEST_CASE("SLCAN reader drops over-long lines", "[slcan]") {
  LineCollector c;
  c.feed(std::string(100, 'A') + "\rC\r");
  REQUIRE(c.lines.size() == 1);
  CHECK(c.lines[0] == "C");
  CHECK(c.reader.overflows == 1);
}
