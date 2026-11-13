// ISO-TP between two links on a simulated bus with simulated time.

#include <catch2/catch_all.hpp>

#include <deque>
#include <numeric>
#include <vector>

#include "isotp.h"

namespace {

constexpr uint32_t kTesterId = 0x7E0;
constexpr uint32_t kEcuId = 0x7E8;

struct SentFrame {
  CanFrame frame;
  uint64_t time_us;
};

struct Harness {
  std::deque<CanFrame> bus;
  std::vector<SentFrame> log;
  uint64_t now_us = 0;
  bool drop_flow_control = false;
  bool drop_consecutive = false;
  int queue_full_after = -1;  // simulate a full driver queue after N frames

  std::vector<uint8_t> tester_rx = std::vector<uint8_t>(4095);
  std::vector<uint8_t> ecu_rx = std::vector<uint8_t>(4095);
  IsoTpLink tester{};
  IsoTpLink ecu{};

  static bool send(void* context, const CanFrame* frame) {
    auto* self = static_cast<Harness*>(context);
    if (self->queue_full_after == 0) {
      return false;
    }
    if (self->queue_full_after > 0) {
      self->queue_full_after--;
    }
    const uint8_t pci = frame->data[0] >> 4;
    if ((self->drop_flow_control && pci == 3) || (self->drop_consecutive && pci == 2)) {
      return true;  // lost on the bus
    }
    self->bus.push_back(*frame);
    self->log.push_back({*frame, self->now_us});
    return true;
  }

  Harness(size_t ecu_capacity = 4095, uint8_t ecu_block_size = 0, uint8_t ecu_st_min = 0) {
    IsoTpConfig config;
    isotp_default_config(&config, kTesterId, kEcuId);
    config.send = &Harness::send;
    config.context = this;
    isotp_init(&tester, &config, tester_rx.data(), tester_rx.size());

    isotp_default_config(&config, kEcuId, kTesterId);
    config.send = &Harness::send;
    config.context = this;
    config.block_size = ecu_block_size;
    config.st_min = ecu_st_min;
    ecu_rx.resize(ecu_capacity);
    isotp_init(&ecu, &config, ecu_rx.data(), ecu_rx.size());
  }

  // Delivers frames and advances time in 1 ms steps.
  void run(uint64_t duration_us) {
    const uint64_t end = now_us + duration_us;
    while (now_us <= end) {
      while (!bus.empty()) {
        CanFrame frame = bus.front();
        bus.pop_front();
        isotp_on_frame(&tester, &frame, now_us);
        isotp_on_frame(&ecu, &frame, now_us);
      }
      isotp_poll(&tester, now_us);
      isotp_poll(&ecu, now_us);
      if (bus.empty()) {
        now_us += 1000;
      }
    }
  }

  size_t count_pci(uint8_t pci) const {
    size_t n = 0;
    for (const SentFrame& f : log) {
      n += (f.frame.data[0] >> 4) == pci;
    }
    return n;
  }
};

std::vector<uint8_t> pattern(size_t length) {
  std::vector<uint8_t> data(length);
  std::iota(data.begin(), data.end(), static_cast<uint8_t>(1));
  return data;
}

void transfer(Harness& h, size_t length) {
  const std::vector<uint8_t> data = pattern(length);
  REQUIRE(isotp_send(&h.tester, data.data(), data.size(), h.now_us) == ISOTP_OK);
  h.run(100000);
  size_t received = 0;
  REQUIRE(isotp_receive(&h.ecu, &received));
  REQUIRE(received == length);
  CHECK(std::equal(data.begin(), data.end(), h.ecu_rx.begin()));
  CHECK_FALSE(isotp_tx_busy(&h.tester));
  CHECK(isotp_tx_result(&h.tester) == ISOTP_OK);
}

}  // namespace

TEST_CASE("ISO-TP single frames up to 7 bytes", "[isotp]") {
  Harness h;
  transfer(h, 1);
  transfer(h, 7);
  CHECK(h.count_pci(0) == 2);
  CHECK(h.log[1].frame.data[0] == 0x07);
  CHECK(h.log[1].frame.dlc == 8);  // padded
  CHECK(h.log[0].frame.data[7] == 0xCC);
}

TEST_CASE("ISO-TP multi frame transfers", "[isotp]") {
  for (size_t length : {8u, 13u, 14u, 100u, 4095u}) {
    INFO("length " << length);
    Harness h;
    transfer(h, length);
    const size_t expected_cf = (length - 6 + 6) / 7;
    CHECK(h.count_pci(1) == 1);  // first frame
    CHECK(h.count_pci(3) == 1);  // one flow control (BS = 0)
    CHECK(h.count_pci(2) == expected_cf);
  }
}

TEST_CASE("ISO-TP first frame and consecutive frame encoding", "[isotp]") {
  Harness h;
  transfer(h, 20);
  const CanFrame& ff = h.log[0].frame;
  CHECK(ff.id == kTesterId);
  CHECK(ff.data[0] == 0x10);
  CHECK(ff.data[1] == 20);
  CHECK(ff.data[2] == 1);
  const CanFrame& fc = h.log[1].frame;
  CHECK(fc.id == kEcuId);
  CHECK(fc.data[0] == 0x30);
  CHECK(h.log[2].frame.data[0] == 0x21);
  CHECK(h.log[3].frame.data[0] == 0x22);
  CHECK(h.log[3].frame.data[1] == 14);  // last CF carries bytes 14..20
}

TEST_CASE("ISO-TP block size requests a flow control per block", "[isotp]") {
  Harness h(4095, /*block_size=*/2);
  transfer(h, 50);  // FF (6) + 7 CFs (44 bytes)
  CHECK(h.count_pci(2) == 7);
  CHECK(h.count_pci(3) == 4);  // after FF and after CF 2, 4, 6
}

TEST_CASE("ISO-TP honours STmin between consecutive frames", "[isotp]") {
  Harness h(4095, 0, /*st_min=*/10);  // 10 ms
  transfer(h, 30);  // FF + 4 CFs
  std::vector<uint64_t> cf_times;
  for (const SentFrame& f : h.log) {
    if ((f.frame.data[0] >> 4) == 2) {
      cf_times.push_back(f.time_us);
    }
  }
  REQUIRE(cf_times.size() == 4);
  for (size_t i = 1; i < cf_times.size(); i++) {
    CHECK(cf_times[i] - cf_times[i - 1] >= 10000);
  }
}

TEST_CASE("ISO-TP receiver overflow is reported to the sender", "[isotp]") {
  Harness h(/*ecu_capacity=*/32);
  const std::vector<uint8_t> data = pattern(100);
  REQUIRE(isotp_send(&h.tester, data.data(), data.size(), 0) == ISOTP_OK);
  h.run(50000);
  CHECK(isotp_tx_result(&h.tester) == ISOTP_ERROR_OVERFLOW);
  CHECK(isotp_take_rx_error(&h.ecu) == ISOTP_ERROR_OVERFLOW);
  CHECK(h.log[1].frame.data[0] == 0x32);
}

TEST_CASE("ISO-TP timeouts N_Bs and N_Cr", "[isotp]") {
  {
    Harness h;
    h.drop_flow_control = true;
    const std::vector<uint8_t> data = pattern(20);
    REQUIRE(isotp_send(&h.tester, data.data(), data.size(), 0) == ISOTP_OK);
    h.run(900000);
    CHECK(isotp_tx_busy(&h.tester));
    h.run(200000);
    CHECK(isotp_tx_result(&h.tester) == ISOTP_ERROR_TIMEOUT_BS);
  }
  {
    Harness h;
    h.drop_consecutive = true;
    const std::vector<uint8_t> data = pattern(20);
    REQUIRE(isotp_send(&h.tester, data.data(), data.size(), 0) == ISOTP_OK);
    h.run(1100000);
    CHECK(isotp_take_rx_error(&h.ecu) == ISOTP_ERROR_TIMEOUT_CR);
    CHECK_FALSE(isotp_receive(&h.ecu, nullptr));
  }
}

TEST_CASE("ISO-TP detects wrong sequence numbers", "[isotp]") {
  Harness h;
  CanFrame ff{};
  ff.id = kTesterId;
  ff.dlc = 8;
  const uint8_t ff_data[8] = {0x10, 0x10, 1, 2, 3, 4, 5, 6};
  std::copy(ff_data, ff_data + 8, ff.data);
  isotp_on_frame(&h.ecu, &ff, 0);
  CanFrame cf = ff;
  const uint8_t cf_data[8] = {0x22, 7, 8, 9, 10, 11, 12, 13};  // expected SN 1
  std::copy(cf_data, cf_data + 8, cf.data);
  isotp_on_frame(&h.ecu, &cf, 1000);
  CHECK(isotp_take_rx_error(&h.ecu) == ISOTP_ERROR_SEQUENCE);
}

TEST_CASE("ISO-TP WAIT flow control and its limit", "[isotp]") {
  Harness h;
  const std::vector<uint8_t> data = pattern(20);
  REQUIRE(isotp_send(&h.tester, data.data(), data.size(), 0) == ISOTP_OK);
  CanFrame wait{};
  wait.id = kEcuId;
  wait.dlc = 3;
  wait.data[0] = 0x31;
  for (int i = 0; i < 10; i++) {
    isotp_on_frame(&h.tester, &wait, 1000);
    CHECK(isotp_tx_busy(&h.tester));
  }
  isotp_on_frame(&h.tester, &wait, 1000);
  CHECK(isotp_tx_result(&h.tester) == ISOTP_ERROR_WAIT_LIMIT);
}

TEST_CASE("ISO-TP retries consecutive frames when the driver queue is full", "[isotp]") {
  Harness h;
  const std::vector<uint8_t> data = pattern(40);
  REQUIRE(isotp_send(&h.tester, data.data(), data.size(), 0) == ISOTP_OK);
  h.queue_full_after = 2;  // FC + 1 CF, then the queue is full
  h.run(3000);
  CHECK(isotp_tx_busy(&h.tester));
  h.queue_full_after = -1;
  h.run(50000);
  size_t received = 0;
  REQUIRE(isotp_receive(&h.ecu, &received));
  CHECK(received == 40);
}

TEST_CASE("ISO-TP rejects invalid requests and STmin encoding", "[isotp]") {
  Harness h;
  uint8_t byte = 0;
  CHECK(isotp_send(&h.tester, &byte, 0, 0) == ISOTP_ERROR_LENGTH);
  std::vector<uint8_t> big(4096);
  CHECK(isotp_send(&h.tester, big.data(), big.size(), 0) == ISOTP_ERROR_LENGTH);
  REQUIRE(isotp_send(&h.tester, big.data(), 100, 0) == ISOTP_OK);
  CHECK(isotp_send(&h.tester, big.data(), 100, 0) == ISOTP_BUSY);

  CHECK(isotp_st_min_to_us(0x00) == 0);
  CHECK(isotp_st_min_to_us(0x7F) == 127000);
  CHECK(isotp_st_min_to_us(0xF1) == 100);
  CHECK(isotp_st_min_to_us(0xF9) == 900);
  CHECK(isotp_st_min_to_us(0x80) == 127000);
}
