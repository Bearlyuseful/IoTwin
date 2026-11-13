#include <catch2/catch_all.hpp>

#include <string>
#include <vector>

#include "line_reader.h"

namespace {

struct Collector {
  LineReader reader;
  std::vector<std::string> lines;

  Collector() { line_reader_init(&reader); }

  size_t feed(const std::string& data) {
    return line_reader_feed(&reader, data.data(), data.size(), &Collector::callback, this);
  }

  static void callback(const char* line, size_t length, void* user) {
    auto* self = static_cast<Collector*>(user);
    REQUIRE(line[length] == '\0');
    self->lines.emplace_back(line, length);
  }
};

}  // namespace

TEST_CASE("Line reader splits several lines in one chunk", "[line_reader]") {
  Collector c;
  CHECK(c.feed("123#01\n456#02\n") == 2);
  REQUIRE(c.lines.size() == 2);
  CHECK(c.lines[0] == "123#01");
  CHECK(c.lines[1] == "456#02");
}

TEST_CASE("Line reader joins lines split across chunks", "[line_reader]") {
  Collector c;
  CHECK(c.feed("12") == 0);
  CHECK(c.feed("3#0") == 0);
  CHECK(c.feed("1\n45") == 1);
  CHECK(c.feed("6#02\n") == 1);
  REQUIRE(c.lines.size() == 2);
  CHECK(c.lines[0] == "123#01");
  CHECK(c.lines[1] == "456#02");
}

TEST_CASE("Line reader strips CR and skips empty lines", "[line_reader]") {
  Collector c;
  c.feed("123#01\r\n\r\n\n456#02\r\n");
  REQUIRE(c.lines.size() == 2);
  CHECK(c.lines[0] == "123#01");
  CHECK(c.lines[1] == "456#02");
}

TEST_CASE("Line reader drops over-long lines and recovers", "[line_reader]") {
  Collector c;
  std::string long_line(LINE_READER_MAX_LINE * 2, 'A');
  c.feed(long_line.substr(0, 100));
  c.feed(long_line.substr(100) + "\n123#01\n");
  REQUIRE(c.lines.size() == 1);
  CHECK(c.lines[0] == "123#01");
  CHECK(c.reader.dropped_lines == 1);
}

TEST_CASE("Line reader accepts a line of maximum length", "[line_reader]") {
  Collector c;
  std::string line(LINE_READER_MAX_LINE - 1, 'B');
  c.feed(line + "\n");
  REQUIRE(c.lines.size() == 1);
  CHECK(c.lines[0] == line);
  CHECK(c.reader.dropped_lines == 0);
}
