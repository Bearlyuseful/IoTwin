// SocketCAN conversion (Linux) and a live test of socketcan_bridge if vcan0 exists.

#include <catch2/catch_all.hpp>

#if defined(__linux__)

#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "bus_test_helpers.hpp"
#include "socketcan.h"

extern char** environ;

TEST_CASE("SocketCAN frames convert to and from CanFrame", "[socketcan]") {
  struct can_frame kernel {};
  kernel.can_id = 0x1F334455 | CAN_EFF_FLAG;
  kernel.len = 2;
  kernel.data[0] = 0xAA;
  kernel.data[1] = 0xBB;
  CanFrame frame{};
  REQUIRE(socketcan_to_frame(&kernel, &frame));
  CHECK(frame.extended);
  CHECK(frame.id == 0x1F334455);
  CHECK(frame.dlc == 2);
  CHECK(frame.data[1] == 0xBB);

  struct can_frame back {};
  REQUIRE(socketcan_from_frame(&frame, &back));
  CHECK(back.can_id == kernel.can_id);
  CHECK(back.len == 2);

  frame = CanFrame{};
  frame.id = 0x123;
  frame.rtr = true;
  frame.dlc = 4;
  REQUIRE(socketcan_from_frame(&frame, &back));
  CHECK(back.can_id == (0x123u | CAN_RTR_FLAG));

  kernel.can_id = CAN_ERR_FLAG | 0x4;
  CHECK_FALSE(socketcan_to_frame(&kernel, &frame));
}

TEST_CASE("socketcan_bridge connects vcan0 to the bus", "[socketcan][integration]") {
  if (if_nametoindex("vcan0") == 0) {
    SKIP("vcan0 not available (sudo modprobe vcan; sudo ip link add vcan0 type vcan; sudo ip link set up vcan0)");
  }
  int can = socketcan_open("vcan0");
  REQUIRE(can >= 0);

  bus_test::RunningServer server;
  const std::string port = std::to_string(server.port());
  std::vector<std::string> args = {SOCKETCAN_BRIDGE_PATH, "--interface", "vcan0", "--port", port,
                                   "--log-level", "warn"};
  std::vector<char*> argv;
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  pid_t pid = -1;
  REQUIRE(posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) == 0);

  bus_test::TestClient client(server.port());
  REQUIRE(server.wait_for_clients(2));

  // vcan0 -> bus
  struct can_frame out {};
  out.can_id = 0x321;
  out.len = 1;
  out.data[0] = 0x42;
  REQUIRE(write(can, &out, sizeof(out)) == static_cast<ssize_t>(sizeof(out)));
  REQUIRE(client.wait_lines(1));
  CHECK(client.lines[0] == "321#42");

  // bus -> vcan0
  client.send("100#01\n");
  pollfd entry{can, POLLIN, 0};
  REQUIRE(poll(&entry, 1, 2000) > 0);
  struct can_frame in {};
  REQUIRE(read(can, &in, sizeof(in)) == static_cast<ssize_t>(sizeof(in)));
  CHECK(in.can_id == 0x100);
  CHECK(in.data[0] == 0x01);

  kill(pid, SIGTERM);
  int status = 0;
  waitpid(pid, &status, 0);
  close(can);
}

#endif
