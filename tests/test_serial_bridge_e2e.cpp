// End-to-end test of the hardware path without hardware (Linux only):
//
//   TestClient <-TCP-> iotwin-bus <-TCP-> serial_bridge (real process) <-pty-> virtual adapter
//
// The virtual adapter runs the real firmware application logic (firmware/app/gateway.c)
// with a simulated CAN controller in internal loopback mode, i.e. everything the
// NUCLEO-G0B1RE does except the register level drivers.

#include <catch2/catch_all.hpp>

#if defined(__linux__)

#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include "bus_test_helpers.hpp"
#include "gateway.h"

extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;

class VirtualAdapter {
 public:
  VirtualAdapter() {
    REQUIRE(openpty(&master_, &slave_, name_, nullptr, nullptr) == 0);
    GatewayIo io{};
    io.context = this;
    io.serial_write = [](void* ctx, const char* data, size_t length) {
      // No REQUIRE here: exceptions must not unwind through the C code of gateway.c.
      auto* self = static_cast<VirtualAdapter*>(ctx);
      if (write(self->master_, data, length) != static_cast<ssize_t>(length)) {
        self->write_errors++;
      }
    };
    io.can_start = [](void* ctx, uint32_t bitrate, GatewayCanMode mode) {
      auto* self = static_cast<VirtualAdapter*>(ctx);
      self->open = true;
      self->bitrate = bitrate;
      self->mode = mode;
      return true;
    };
    io.can_stop = [](void* ctx) { static_cast<VirtualAdapter*>(ctx)->open = false; };
    io.can_send = [](void* ctx, const CanFrame* frame) {
      static_cast<VirtualAdapter*>(ctx)->controller_.push_back(*frame);
      return true;
    };
    io.can_status = [](void*, GatewayCanStatus* status) { *status = GatewayCanStatus{}; };
    io.led_set_level = [](void* ctx, uint8_t percent) { static_cast<VirtualAdapter*>(ctx)->led = percent > 0; };
    gateway_init(&gateway, &io);
  }

  ~VirtualAdapter() {
    close(slave_);
    close(master_);
  }

  const char* device() const { return name_; }

  // Processes serial input for up to timeout_ms, then delivers loopback echoes.
  void pump(int timeout_ms) {
    pollfd entry{master_, POLLIN, 0};
    if (poll(&entry, 1, timeout_ms) > 0) {
      char buffer[256];
      ssize_t n = read(master_, buffer, sizeof(buffer));
      if (n > 0) {
        gateway_on_serial_data(&gateway, buffer, static_cast<size_t>(n));
      }
    }
    std::vector<CanFrame> frames;
    frames.swap(controller_);
    if (mode == GATEWAY_CAN_LOOPBACK) {
      for (const CanFrame& frame : frames) {
        gateway_on_can_frame(&gateway, &frame);
      }
    }
  }

  template <typename Predicate>
  bool pump_until(Predicate done, int timeout_ms = 5000) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!done()) {
      if (Clock::now() > deadline) {
        return false;
      }
      pump(10);
    }
    return true;
  }

  Gateway gateway{};
  bool open = false;
  bool led = false;
  int write_errors = 0;
  uint32_t bitrate = 0;
  GatewayCanMode mode = GATEWAY_CAN_NORMAL;

 private:
  int master_ = -1;
  int slave_ = -1;
  char name_[128] = {};
  std::vector<CanFrame> controller_;
};

class BridgeProcess {
 public:
  BridgeProcess(const std::string& device, uint16_t bus_port) {
    const std::string port = std::to_string(bus_port);
    std::vector<std::string> args = {SERIAL_BRIDGE_PATH, "--serial", device, "--port", port,
                                     "--mode", "loopback", "--log-level", "warn"};
    std::vector<char*> argv;
    for (std::string& arg : args) {
      argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    REQUIRE(posix_spawn(&pid_, argv[0], nullptr, nullptr, argv.data(), environ) == 0);
  }

  ~BridgeProcess() {
    kill(pid_, SIGTERM);
    int status = 0;
    waitpid(pid_, &status, 0);
  }

 private:
  pid_t pid_ = -1;
};

}  // namespace

TEST_CASE("serial_bridge connects an SLCAN adapter to the bus", "[bridge][integration]") {
  bus_test::RunningServer server;
  VirtualAdapter adapter;
  BridgeProcess bridge(adapter.device(), server.port());

  // The bridge configures the adapter: close, version, bitrate, open in loopback mode.
  REQUIRE(adapter.pump_until([&] { return adapter.open; }));
  CHECK(adapter.bitrate == 500000);
  CHECK(adapter.mode == GATEWAY_CAN_LOOPBACK);

  bus_test::TestClient ecu(server.port());
  REQUIRE(server.wait_for_clients(2));

  // Bus -> adapter: the lamp command switches the board LED.
  ecu.send("100#01\n");
  REQUIRE(adapter.pump_until([&] { return adapter.led; }));

  // Adapter -> bus: the user button reports the wall switch.
  gateway_on_button(&adapter.gateway);
  adapter.pump(10);
  REQUIRE(ecu.wait_lines(1));
  CHECK(ecu.lines[0] == "200#01");

  // Loopback echoes were recognised, nothing was reported twice.
  adapter.pump(50);
  CHECK_FALSE(ecu.wait_lines(2, 200));
  CHECK(adapter.gateway.echoes == 2);
  CHECK(adapter.write_errors == 0);
}

#endif
