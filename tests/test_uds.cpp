// UDS server (ISO 14229-1): services without transport, then over ISO-TP on a simulated bus.

#include <catch2/catch_all.hpp>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "isotp.h"
#include "uds.h"

namespace {

constexpr uint32_t kRequestId = 0x7E0;
constexpr uint32_t kResponseId = 0x7E8;
constexpr uint32_t kDtcLampShort = 0x900111;  // B1001-11
constexpr uint32_t kDtcLampOpen = 0x900113;   // B1001-13
const std::string kVin = "IOTWIN0LIGHTDEMO1";

using Bytes = std::vector<uint8_t>;

struct Server {
  UdsServer server{};
  UdsDtc dtcs[2]{};
  std::deque<CanFrame> sent;
  std::vector<uint8_t> resets;
  std::vector<UdsSession> sessions;
  Bytes written;
  uint16_t written_did = 0;
  uint8_t write_result = 0;
  bool with_reset = true;
  bool with_write = true;

  static bool send(void* context, const CanFrame* frame) {
    static_cast<Server*>(context)->sent.push_back(*frame);
    return true;
  }

  static int read_did(void* context, uint16_t did, uint8_t* out, size_t capacity) {
    (void)context;
    if (did == UDS_DID_VIN) {
      if (capacity < kVin.size()) {
        return -static_cast<int>(UDS_NRC_RESPONSE_TOO_LONG);
      }
      std::memcpy(out, kVin.data(), kVin.size());
      return static_cast<int>(kVin.size());
    }
    if (did == 0x0100) {
      out[0] = 0x01;
      return 1;
    }
    if (did == 0x0222) {
      return -static_cast<int>(UDS_NRC_CONDITIONS_NOT_CORRECT);
    }
    return -static_cast<int>(UDS_NRC_REQUEST_OUT_OF_RANGE);
  }

  static uint8_t write_did(void* context, uint16_t did, const uint8_t* data, size_t length) {
    auto* self = static_cast<Server*>(context);
    if (did != 0x0102) {
      return UDS_NRC_REQUEST_OUT_OF_RANGE;
    }
    self->written_did = did;
    self->written.assign(data, data + length);
    return self->write_result;
  }

  static void reset(void* context, uint8_t type) { static_cast<Server*>(context)->resets.push_back(type); }

  static void session_changed(void* context, UdsSession session) {
    static_cast<Server*>(context)->sessions.push_back(session);
  }

  explicit Server(bool reset_handler = true, bool write_handler = true) {
    uds_dtc_init(&dtcs[0], kDtcLampShort);
    uds_dtc_init(&dtcs[1], kDtcLampOpen);
    UdsServerConfig config;
    uds_server_default_config(&config, kRequestId, kResponseId);
    config.send = &Server::send;
    config.send_context = this;
    config.handlers.read_did = &Server::read_did;
    config.handlers.write_did = write_handler ? &Server::write_did : nullptr;
    config.handlers.reset = reset_handler ? &Server::reset : nullptr;
    config.handlers.session_changed = &Server::session_changed;
    config.handlers.context = this;
    config.dtcs = dtcs;
    config.dtc_count = 2;
    uds_server_init(&server, &config);
  }

  Bytes request(const Bytes& data, bool functional = false, uint64_t now_us = 0) {
    uint8_t response[UDS_MAX_RESPONSE];
    const size_t length =
        uds_server_process(&server, data.data(), data.size(), functional, response, sizeof(response), now_us);
    return Bytes(response, response + length);
  }
};

Bytes vin_response() {
  Bytes expected = {0x62, 0xF1, 0x90};
  expected.insert(expected.end(), kVin.begin(), kVin.end());
  return expected;
}

}  // namespace

TEST_CASE("UDS session control and tester present", "[uds]") {
  Server s;
  CHECK(uds_server_session(&s.server) == UDS_SESSION_DEFAULT);
  CHECK(s.request({0x22, 0xF1, 0x86}) == Bytes{0x62, 0xF1, 0x86, 0x01});

  // P2 50 ms, P2* 5000 ms in 10 ms units
  CHECK(s.request({0x10, 0x03}) == Bytes{0x50, 0x03, 0x00, 0x32, 0x01, 0xF4});
  CHECK(uds_server_session(&s.server) == UDS_SESSION_EXTENDED);
  CHECK(s.request({0x22, 0xF1, 0x86}) == Bytes{0x62, 0xF1, 0x86, 0x03});
  CHECK(s.sessions == std::vector<UdsSession>{UDS_SESSION_EXTENDED});

  CHECK(s.request({0x10, 0x81}).empty());  // suppressed positive response, still executed
  CHECK(uds_server_session(&s.server) == UDS_SESSION_DEFAULT);
  CHECK(s.request({0x10, 0x02}) == Bytes{0x7F, 0x10, 0x12});  // programming not supported
  CHECK(s.request({0x10}) == Bytes{0x7F, 0x10, 0x13});

  CHECK(s.request({0x3E, 0x00}) == Bytes{0x7E, 0x00});
  CHECK(s.request({0x3E, 0x80}).empty());
  CHECK(s.request({0x3E, 0x01}) == Bytes{0x7F, 0x3E, 0x12});
}

TEST_CASE("UDS rejects unknown services, silently for functional requests", "[uds]") {
  Server s;
  CHECK(s.request({0x31, 0x01, 0x02, 0x03}) == Bytes{0x7F, 0x31, 0x11});
  CHECK(s.request({0x31, 0x01, 0x02, 0x03}, true).empty());
  CHECK(s.request({0x22, 0x12, 0x34}, true).empty());                    // requestOutOfRange
  CHECK(s.request({0x22, 0xF1}, true) == Bytes{0x7F, 0x22, 0x13});        // other NRCs are sent
  CHECK(s.server.negative_responses == 2);
}

TEST_CASE("UDS ReadDataByIdentifier", "[uds]") {
  Server s;
  CHECK(s.request({0x22, 0xF1, 0x90}) == vin_response());

  Bytes both = vin_response();
  both.insert(both.end(), {0x01, 0x00, 0x01});
  CHECK(s.request({0x22, 0xF1, 0x90, 0x01, 0x00}) == both);

  // Unknown identifiers are skipped as long as one is supported.
  CHECK(s.request({0x22, 0x12, 0x34, 0x01, 0x00}) == Bytes{0x62, 0x01, 0x00, 0x01});
  CHECK(s.request({0x22, 0x12, 0x34}) == Bytes{0x7F, 0x22, 0x31});
  CHECK(s.request({0x22, 0x02, 0x22}) == Bytes{0x7F, 0x22, 0x22});  // NRC from the application
  CHECK(s.request({0x22, 0xF1, 0x90, 0x01}) == Bytes{0x7F, 0x22, 0x13});
  Bytes nine = {0x22};
  for (int i = 0; i < 9; i++) {
    nine.insert(nine.end(), {0x01, 0x00});
  }
  CHECK(s.request(nine) == Bytes{0x7F, 0x22, 0x13});
}

TEST_CASE("UDS WriteDataByIdentifier needs the extended session", "[uds]") {
  Server s;
  CHECK(s.request({0x2E, 0x01, 0x02, 0x01, 0xF4}) == Bytes{0x7F, 0x2E, 0x7F});
  s.request({0x10, 0x03});
  CHECK(s.request({0x2E, 0x01, 0x02, 0x01, 0xF4}) == Bytes{0x6E, 0x01, 0x02});
  CHECK(s.written_did == 0x0102);
  CHECK(s.written == Bytes{0x01, 0xF4});
  CHECK(s.request({0x2E, 0x09, 0x99, 0x00}) == Bytes{0x7F, 0x2E, 0x31});
  s.write_result = UDS_NRC_REQUEST_OUT_OF_RANGE;
  CHECK(s.request({0x2E, 0x01, 0x02, 0xFF, 0xFF}) == Bytes{0x7F, 0x2E, 0x31});
  CHECK(s.request({0x2E, 0x01, 0x02}) == Bytes{0x7F, 0x2E, 0x13});

  Server read_only(true, false);
  CHECK(read_only.request({0x2E, 0x01, 0x02, 0x00}) == Bytes{0x7F, 0x2E, 0x11});
}

TEST_CASE("UDS DTC status, read and clear", "[uds]") {
  Server s;
  // Freshly cleared: both "not completed" bits.
  CHECK(s.request({0x19, 0x0A}) == Bytes{0x59, 0x0A, 0x7F, 0x90, 0x01, 0x11, 0x50, 0x90, 0x01, 0x13, 0x50});
  CHECK(s.request({0x19, 0x01, 0x09}) == Bytes{0x59, 0x01, 0x7F, 0x01, 0x00, 0x00});

  uds_dtc_report(uds_server_find_dtc(&s.server, kDtcLampShort), true);
  uds_dtc_report(uds_server_find_dtc(&s.server, kDtcLampOpen), false);
  CHECK(s.dtcs[0].status == 0x2F);  // failed, failed this cycle, pending, confirmed, failed since clear
  CHECK(s.dtcs[1].status == 0x00);
  CHECK(s.request({0x19, 0x01, 0x09}) == Bytes{0x59, 0x01, 0x7F, 0x01, 0x00, 0x01});
  CHECK(s.request({0x19, 0x02, 0x09}) == Bytes{0x59, 0x02, 0x7F, 0x90, 0x01, 0x11, 0x2F});

  uds_dtc_report(&s.dtcs[0], false);  // fault gone: confirmed stays until cleared
  CHECK(s.dtcs[0].status == 0x2E);
  uds_dtc_start_operation_cycle(&s.dtcs[0]);  // next cycle: "this cycle" results reset
  CHECK(s.dtcs[0].status == 0x6C);
  uds_dtc_report(&s.dtcs[0], false);
  CHECK(s.dtcs[0].status == 0x2C);
  CHECK(s.request({0x19, 0x02, 0x01}) == Bytes{0x59, 0x02, 0x7F});

  CHECK(s.request({0x14, 0x90, 0x01, 0x11}) == Bytes{0x54});
  CHECK(s.dtcs[0].status == 0x50);
  CHECK(s.request({0x14, 0x12, 0x34, 0x56}) == Bytes{0x7F, 0x14, 0x31});
  uds_dtc_report(&s.dtcs[1], true);
  CHECK(s.request({0x14, 0xFF, 0xFF, 0xFF}) == Bytes{0x54});
  CHECK(s.dtcs[1].status == 0x50);

  CHECK(s.request({0x19, 0x04, 0x00}) == Bytes{0x7F, 0x19, 0x12});
  CHECK(s.request({0x19, 0x02}) == Bytes{0x7F, 0x19, 0x13});
  CHECK(s.request({0x14, 0xFF, 0xFF}) == Bytes{0x7F, 0x14, 0x13});
}

TEST_CASE("UDS ECUReset runs after the response and returns to the default session", "[uds]") {
  Server s;
  s.request({0x10, 0x03});
  CHECK(s.request({0x11, 0x01}) == Bytes{0x51, 0x01});
  CHECK(s.resets.empty());
  uds_server_poll(&s.server, 1000);
  CHECK(s.resets == std::vector<uint8_t>{UDS_RESET_HARD});
  CHECK(uds_server_session(&s.server) == UDS_SESSION_DEFAULT);

  CHECK(s.request({0x11, 0x83}).empty());
  uds_server_poll(&s.server, 2000);
  CHECK(s.resets == std::vector<uint8_t>{UDS_RESET_HARD, UDS_RESET_SOFT});
  CHECK(s.request({0x11, 0x02}) == Bytes{0x7F, 0x11, 0x12});

  Server no_reset(false);
  CHECK(no_reset.request({0x11, 0x01}) == Bytes{0x7F, 0x11, 0x12});
}

TEST_CASE("UDS falls back to the default session after S3", "[uds]") {
  Server s;
  s.request({0x10, 0x03}, false, 0);
  uds_server_poll(&s.server, 4'000'000);
  CHECK(uds_server_session(&s.server) == UDS_SESSION_EXTENDED);
  s.request({0x3E, 0x80}, false, 4'000'000);  // tester present restarts S3
  uds_server_poll(&s.server, 8'500'000);
  CHECK(uds_server_session(&s.server) == UDS_SESSION_EXTENDED);
  uds_server_poll(&s.server, 9'100'000);
  CHECK(uds_server_session(&s.server) == UDS_SESSION_DEFAULT);
  CHECK(s.sessions == std::vector<UdsSession>{UDS_SESSION_EXTENDED, UDS_SESSION_DEFAULT});
}

TEST_CASE("UDS over ISO-TP: multi-frame response and functional addressing", "[uds]") {
  Server s;
  std::vector<uint8_t> tester_rx(4095);
  IsoTpLink tester{};
  std::deque<CanFrame> to_server;
  IsoTpConfig config;
  isotp_default_config(&config, kRequestId, kResponseId);
  config.send = [](void* context, const CanFrame* frame) {
    static_cast<std::deque<CanFrame>*>(context)->push_back(*frame);
    return true;
  };
  config.context = &to_server;
  isotp_init(&tester, &config, tester_rx.data(), tester_rx.size());

  uint64_t now_us = 0;
  auto run = [&](int steps) {
    for (int i = 0; i < steps; i++) {
      while (!to_server.empty() || !s.sent.empty()) {
        while (!to_server.empty()) {
          CanFrame frame = to_server.front();
          to_server.pop_front();
          uds_server_on_frame(&s.server, &frame, now_us);
        }
        while (!s.sent.empty()) {
          CanFrame frame = s.sent.front();
          s.sent.pop_front();
          REQUIRE(frame.id == kResponseId);
          isotp_on_frame(&tester, &frame, now_us);
        }
      }
      isotp_poll(&tester, now_us);
      uds_server_poll(&s.server, now_us);
      now_us += 1000;
    }
  };

  const uint8_t read_vin[] = {0x22, 0xF1, 0x90};
  REQUIRE(isotp_send(&tester, read_vin, sizeof(read_vin), now_us) == ISOTP_OK);
  run(20);
  size_t length = 0;
  REQUIRE(isotp_receive(&tester, &length));
  CHECK(Bytes(tester_rx.begin(), tester_rx.begin() + static_cast<long>(length)) == vin_response());

  // Functional TesterPresent: single frame on 0x7DF, answered physically.
  CanFrame functional;
  const uint8_t tester_present[] = {0x02, 0x3E, 0x00};
  REQUIRE(can_frame_set(&functional, UDS_FUNCTIONAL_ID, false, tester_present, sizeof(tester_present)));
  CHECK(uds_server_on_frame(&s.server, &functional, now_us));
  REQUIRE(s.sent.size() == 1);
  CHECK(s.sent.front().data[0] == 0x02);
  CHECK(s.sent.front().data[1] == 0x7E);
  s.sent.clear();

  // Functional request for an unknown service: no answer at all.
  const uint8_t unknown[] = {0x02, 0x31, 0x01};
  REQUIRE(can_frame_set(&functional, UDS_FUNCTIONAL_ID, false, unknown, sizeof(unknown)));
  uds_server_on_frame(&s.server, &functional, now_us);
  CHECK(s.sent.empty());

  CanFrame other;
  REQUIRE(can_frame_set(&other, 0x123, false, tester_present, sizeof(tester_present)));
  CHECK_FALSE(uds_server_on_frame(&s.server, &other, now_us));
}

TEST_CASE("UDS text helpers", "[uds]") {
  char text[16];
  uds_format_dtc(kDtcLampShort, text, sizeof(text));
  CHECK(std::string(text) == "B1001-11");
  uds_format_dtc(0xC07388, text, sizeof(text));
  CHECK(std::string(text) == "U0073-88");

  uint32_t code = 0;
  CHECK(uds_parse_dtc("B1001-11", &code));
  CHECK(code == kDtcLampShort);
  CHECK(uds_parse_dtc("u0073-88", &code));
  CHECK(code == 0xC07388);
  CHECK(uds_parse_dtc("P0100", &code));
  CHECK(code == 0x010000);
  CHECK(uds_parse_dtc("900113", &code));
  CHECK(code == kDtcLampOpen);
  CHECK_FALSE(uds_parse_dtc("X1001-11", &code));
  CHECK_FALSE(uds_parse_dtc("B4001", &code));
  CHECK_FALSE(uds_parse_dtc("B1001+11", &code));

  CHECK(std::string(uds_service_name(0x22)) == "ReadDataByIdentifier");
  CHECK(std::string(uds_service_name(0x62)) == "ReadDataByIdentifier");
  CHECK(std::string(uds_service_name(0xC5)) == "ControlDTCSetting");
  CHECK(std::string(uds_service_name(0x7F)) == "NegativeResponse");
  CHECK(std::string(uds_nrc_name(0x31)) == "requestOutOfRange");
  CHECK(std::string(uds_session_name(0x03)) == "extended");
}
