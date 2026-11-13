// Scenario scripts (parser, frame patterns) and candump log lines.

#include <catch2/catch_all.hpp>

#include <cstring>
#include <string>

#include "candump_log.h"
#include "scenario.h"

namespace {

CanFrame frame(const char* text) {
  CanFrame f;
  REQUIRE(can_frame_from_text(text, std::strlen(text), &f));
  return f;
}

bool matches(const char* pattern_text, const char* frame_text) {
  FramePattern pattern;
  REQUIRE(frame_pattern_parse(pattern_text, &pattern));
  const CanFrame f = frame(frame_text);
  return frame_pattern_matches(&pattern, &f);
}

}  // namespace

TEST_CASE("Frame patterns", "[scenario]") {
  CHECK(matches("100#01", "100#01"));
  CHECK_FALSE(matches("100#01", "100#0100"));  // exact length
  CHECK_FALSE(matches("100#01", "101#01"));
  CHECK(matches("100#01*", "100#0122"));
  CHECK_FALSE(matches("100#01*", "100#"));
  CHECK(matches("100#*", "100#"));
  CHECK(matches("100#*", "100#DEADBEEF"));
  CHECK(matches("100#XX01", "100#FF01"));
  CHECK_FALSE(matches("100#XX01", "100#FF02"));
  CHECK(matches("100#R", "100#R"));
  CHECK_FALSE(matches("100#R", "100#00"));
  CHECK(matches("1F334455#11", "1F334455#11"));
  CHECK_FALSE(matches("1F334455#11", "455#11"));
  CHECK(matches("100#de", "100#DE"));

  FramePattern pattern;
  CHECK_FALSE(frame_pattern_parse("100", &pattern));
  CHECK_FALSE(frame_pattern_parse("1000#00", &pattern));
  CHECK_FALSE(frame_pattern_parse("800#00", &pattern));
  CHECK_FALSE(frame_pattern_parse("100#0", &pattern));
  CHECK_FALSE(frame_pattern_parse("100#001122334455667788", &pattern));
  CHECK_FALSE(frame_pattern_parse("100#GG", &pattern));
}

TEST_CASE("Scenario steps are parsed", "[scenario]") {
  Scenario s;
  scenario_init(&s);
  char error[256] = "";
  const char* lines[] = {
      "name Lamp test",
      "# comment",
      "",
      "send 200#01            # switch on",
      "expect 100#01 within 300",
      "expect 100#*",
      "expect-not 100#01 for 500",
      "wait 250",
      "bench ecu fault short on",
      "bus drop 50 200/7FF",
      "uds ecu 190209 expect 59027F*",
      "uds 7E1:7E9 22F187",
      "log checking # not part of the text",
  };
  for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
    INFO(lines[i]);
    REQUIRE(scenario_parse_line(&s, lines[i], static_cast<int>(i + 1), error, sizeof(error)));
  }
  CHECK(std::string(s.name) == "Lamp test");
  REQUIRE(s.count == 10);

  CHECK(s.steps[0].type == STEP_SEND);
  CHECK(s.steps[0].frame.id == 0x200);
  CHECK(s.steps[0].line == 4);
  CHECK(s.steps[1].type == STEP_EXPECT);
  CHECK(s.steps[1].ms == 300);
  CHECK(s.steps[2].ms == SCENARIO_DEFAULT_TIMEOUT_MS);
  CHECK(s.steps[3].type == STEP_EXPECT_NOT);
  CHECK(s.steps[3].ms == 500);
  CHECK(s.steps[4].type == STEP_WAIT);
  CHECK(s.steps[5].type == STEP_BENCH);
  CHECK(std::string(s.steps[5].text) == "@ecu fault short on");
  CHECK(s.steps[6].type == STEP_BUS);
  CHECK(std::string(s.steps[6].text) == "fault drop 50 200/7FF");

  const ScenarioStep& uds = s.steps[7];
  CHECK(uds.type == STEP_UDS);
  CHECK(uds.uds_tx == 0x7E0);
  CHECK(uds.uds_rx == 0x7E8);
  CHECK(uds.uds_request_length == 3);
  CHECK(uds.uds_request[0] == 0x19);
  CHECK(uds.uds_expect_given);
  CHECK(uds.uds_expect_prefix);
  CHECK(uds.uds_expect_length == 3);
  CHECK(s.steps[8].uds_tx == 0x7E1);
  CHECK(s.steps[8].uds_rx == 0x7E9);
  CHECK_FALSE(s.steps[8].uds_expect_given);
  CHECK(std::string(s.steps[9].text) == "checking");
}

TEST_CASE("Scenario syntax errors name the line", "[scenario]") {
  const char* invalid[] = {
      "send 200",        "send",           "expect 100#01 within",   "expect 100#01 after 10",
      "expect-not 100#01", "wait",         "wait soon",              "bench ecu",
      "bus",             "uds ecu",        "uds plc 22F190",         "uds ecu 22F",
      "uds ecu 22F190 expect", "jump 3",
  };
  for (const char* line : invalid) {
    Scenario s;
    scenario_init(&s);
    char error[256] = "";
    INFO(line);
    CHECK_FALSE(scenario_parse_line(&s, line, 7, error, sizeof(error)));
    CHECK(std::string(error).rfind("line 7:", 0) == 0);
  }
}

TEST_CASE("Scenario hex bytes", "[scenario]") {
  uint8_t out[8];
  bool star = false;
  CHECK(scenario_parse_hex("22F190", out, sizeof(out), &star) == 3);
  CHECK(out[2] == 0x90);
  CHECK_FALSE(star);
  CHECK(scenario_parse_hex("59 02 7F*", out, sizeof(out), &star) == 3);
  CHECK(star);
  CHECK(scenario_parse_hex("2", out, sizeof(out), &star) == -1);
  CHECK(scenario_parse_hex("ZZ", out, sizeof(out), &star) == -1);
  CHECK(scenario_parse_hex("001122334455667788", out, sizeof(out), &star) == -1);
}

TEST_CASE("candump log lines", "[candump]") {
  char text[CANDUMP_LOG_LINE_MAX];
  const CanFrame f = frame("123#DEADBEEF");
  REQUIRE(candump_log_format(&f, 1700000000123456ull, "iotwin", text, sizeof(text)) > 0);
  CHECK(std::string(text) == "(1700000000.123456) iotwin 123#DEADBEEF");

  CanFrame parsed;
  uint64_t time_us = 0;
  bool has_time = false;
  REQUIRE(candump_log_parse(text, std::strlen(text), &parsed, &time_us, &has_time) == CANDUMP_LINE_FRAME);
  CHECK(has_time);
  CHECK(time_us == 1700000000123456ull);
  CHECK(can_frame_equals(&parsed, &f));

  const char* candump = "(1699999999.5) can0 1F334455#11 R\n";
  REQUIRE(candump_log_parse(candump, std::strlen(candump), &parsed, &time_us, &has_time) == CANDUMP_LINE_FRAME);
  CHECK(time_us == 1699999999500000ull);
  CHECK(parsed.extended);

  const char* plain = "  100#01  ";
  REQUIRE(candump_log_parse(plain, std::strlen(plain), &parsed, &time_us, &has_time) == CANDUMP_LINE_FRAME);
  CHECK_FALSE(has_time);
  CHECK(parsed.id == 0x100);

  CHECK(candump_log_parse("", 0, &parsed, &time_us, nullptr) == CANDUMP_LINE_SKIP);
  CHECK(candump_log_parse("# comment", 9, &parsed, &time_us, nullptr) == CANDUMP_LINE_SKIP);
  CHECK(candump_log_parse("(12.3 can0 100#01", 17, &parsed, &time_us, nullptr) == CANDUMP_LINE_ERROR);
  CHECK(candump_log_parse("(12.3) can0", 11, &parsed, &time_us, nullptr) == CANDUMP_LINE_ERROR);
  CHECK(candump_log_parse("hello", 5, &parsed, &time_us, nullptr) == CANDUMP_LINE_ERROR);
}
