// Serial port layer against a pseudo terminal (Linux only; Windows has no PTYs).

#include <catch2/catch_all.hpp>

#include "serial_port.h"

#if defined(__linux__)

#include <poll.h>
#include <pty.h>
#include <unistd.h>

#include <string>

namespace {

std::string read_master(int master, size_t expected, int timeout_ms = 1000) {
  std::string data;
  while (data.size() < expected) {
    pollfd entry{master, POLLIN, 0};
    if (poll(&entry, 1, timeout_ms) <= 0) {
      break;
    }
    char buffer[64];
    ssize_t n = read(master, buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    data.append(buffer, static_cast<size_t>(n));
  }
  return data;
}

}  // namespace

TEST_CASE("Serial port exchanges raw bytes with a pseudo terminal", "[serial]") {
  int master = -1;
  int slave = -1;
  char name[128];
  REQUIRE(openpty(&master, &slave, name, nullptr, nullptr) == 0);

  SerialPort* port = serial_open(name, 115200);
  REQUIRE(port != nullptr);

  // host -> device: raw mode must not translate CR
  REQUIRE(serial_write_all(port, "t100101\r", 8) == 0);
  CHECK(read_master(master, 8) == "t100101\r");

  // device -> host
  REQUIRE(write(master, "z\r\a", 3) == 3);
  std::string received;
  char buffer[64];
  for (int i = 0; i < 20 && received.size() < 3; i++) {
    int n = serial_read(port, buffer, sizeof(buffer), 100);
    REQUIRE(n >= 0);
    received.append(buffer, static_cast<size_t>(n));
  }
  CHECK(received == "z\r\a");

  // nothing pending: read times out
  CHECK(serial_read(port, buffer, sizeof(buffer), 20) == 0);

  serial_close(port);
  close(slave);
  close(master);
}

#endif

TEST_CASE("Serial port reports a missing device", "[serial]") {
#if defined(_WIN32)
  SerialPort* port = serial_open("COM250", 115200);
#else
  SerialPort* port = serial_open("/dev/iotwin-does-not-exist", 115200);
#endif
  CHECK(port == nullptr);
  CHECK(std::string(serial_last_error()).size() > 0);
}
