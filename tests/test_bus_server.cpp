// Integration tests: real TCP connections against an in-process bus server.

#include <catch2/catch_all.hpp>

#include "bus_test_helpers.hpp"

using bus_test::RunningServer;
using bus_test::TestClient;

TEST_CASE("Bus server forwards a frame to all other nodes", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  TestClient b(server.port());
  TestClient c(server.port());
  REQUIRE(server.wait_for_clients(3));

  a.send("123#DEADBEEF\n");

  REQUIRE(b.wait_lines(1));
  REQUIRE(c.wait_lines(1));
  CHECK(b.lines[0] == "123#DEADBEEF");
  CHECK(c.lines[0] == "123#DEADBEEF");
  CHECK_FALSE(a.wait_lines(1, 100));  // no echo to the sender
  CHECK(server.stats().frames_routed == 1);
}

TEST_CASE("Bus server normalises frames and drops invalid lines", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  TestClient b(server.port());
  REQUIRE(server.wait_for_clients(2));

  a.send("hello world\n1ab#de.ad\r\n");

  REQUIRE(b.wait_lines(1));
  CHECK(b.lines[0] == "1AB#DEAD");
  CHECK_FALSE(b.wait_lines(2, 100));
  CHECK(server.stats().frames_rejected == 1);
  CHECK(server.stats().frames_routed == 1);
}

TEST_CASE("Bus server handles frames split across TCP segments", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  TestClient b(server.port());
  REQUIRE(server.wait_for_clients(2));

  a.send("1F33");
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  a.send("4455#11\n200#01\n");

  REQUIRE(b.wait_lines(2));
  CHECK(b.lines[0] == "1F334455#11");
  CHECK(b.lines[1] == "200#01");
}

TEST_CASE("Bus server survives disconnecting nodes", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  {
    TestClient leaving(server.port());
    REQUIRE(server.wait_for_clients(2));
  }
  REQUIRE(server.wait_for_clients(1));

  TestClient b(server.port());
  REQUIRE(server.wait_for_clients(2));
  a.send("100#01\n");
  REQUIRE(b.wait_lines(1));
  CHECK(b.lines[0] == "100#01");
  CHECK(server.stats().clients_accepted == 3);
}

TEST_CASE("Bus server echo mode returns frames to the sender", "[bus][integration]") {
  RunningServer server(/*echo=*/true);
  TestClient a(server.port());
  REQUIRE(server.wait_for_clients(1));
  a.send("7FF#00\n");
  REQUIRE(a.wait_lines(1));
  CHECK(a.lines[0] == "7FF#00");
}

// --- Control commands, test bench lines and fault injection -----------------

namespace {

// Sends a control command and returns the server's answer.
std::string command(TestClient& client, const std::string& text) {
  const size_t before = client.lines.size();
  client.send(text + "\n");
  REQUIRE(client.wait_lines(before + 1));
  return client.lines.back();
}

}  // namespace

TEST_CASE("Bus server answers control commands only to the sender", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  TestClient b(server.port());
  REQUIRE(server.wait_for_clients(2));

  CHECK(command(a, "!name scenario") == "!ok");
  CHECK(command(a, "!status").rfind("!status nodes=2 routed=0 dropped=0", 0) == 0);
  CHECK(command(a, "!bogus").rfind("!error unknown command", 0) == 0);
  CHECK(command(a, "!fault drop 150").rfind("!error", 0) == 0);
  CHECK(command(a, "!fault delay x").rfind("!error", 0) == 0);
  CHECK_FALSE(b.wait_lines(1, 100));  // other nodes see none of it
  CHECK(server.stats().frames_rejected == 0);
}

TEST_CASE("Bus server forwards test bench lines to subscribed nodes only", "[bus][integration]") {
  RunningServer server;
  TestClient bench(server.port());
  TestClient ecu(server.port());
  TestClient other(server.port());
  REQUIRE(server.wait_for_clients(3));
  CHECK(command(ecu, "!testbench on") == "!ok");
  ecu.lines.clear();

  bench.send("@ecu fault short on\n");
  REQUIRE(ecu.wait_lines(1));
  CHECK(ecu.lines[0] == "@ecu fault short on");
  CHECK_FALSE(other.wait_lines(1, 100));
  CHECK(server.stats().frames_routed == 0);
}

TEST_CASE("Bus server drops frames: all, filtered by id, during silence", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  TestClient b(server.port());
  REQUIRE(server.wait_for_clients(2));

  CHECK(command(a, "!fault drop 100 200") == "!ok");  // only id 0x200
  a.send("200#01\n100#01\n");
  REQUIRE(b.wait_lines(1));
  CHECK(b.lines[0] == "100#01");
  CHECK_FALSE(b.wait_lines(2, 100));
  CHECK(server.stats().frames_dropped == 1);

  CHECK(command(a, "!fault drop 100 100/700") == "!ok");  // 0x1xx
  a.send("1FF#01\n200#02\n");
  REQUIRE(b.wait_lines(2));
  CHECK(b.lines[1] == "200#02");

  CHECK(command(a, "!fault clear") == "!ok");
  CHECK(command(a, "!fault silence 300") == "!ok");
  a.send("300#03\n");
  CHECK_FALSE(b.wait_lines(3, 150));
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  a.send("300#04\n");
  REQUIRE(b.wait_lines(3));
  CHECK(b.lines[2] == "300#04");
}

TEST_CASE("Bus server drops a reproducible share of frames", "[bus][integration]") {
  auto run = [](int seed) {
    RunningServer server;
    TestClient a(server.port());
    TestClient b(server.port());
    REQUIRE(server.wait_for_clients(2));
    CHECK(command(a, "!fault seed " + std::to_string(seed)) == "!ok");
    CHECK(command(a, "!fault drop 30") == "!ok");
    for (int i = 0; i < 200; i++) {
      a.send("123#" + std::string(i % 2 ? "01" : "02") + "\n");
    }
    REQUIRE(server.wait_for_routed(200));
    b.wait_lines(200, 300);
    return b.lines.size();
  };
  const size_t first = run(7);
  CHECK(first > 100);  // about 140 of 200
  CHECK(first < 180);
  CHECK(run(7) == first);  // same seed, same losses
}

TEST_CASE("Bus server delays frames and keeps their order", "[bus][integration]") {
  RunningServer server;
  TestClient a(server.port());
  TestClient b(server.port());
  REQUIRE(server.wait_for_clients(2));

  CHECK(command(a, "!fault delay 200 50") == "!ok");
  const auto start = bus_test::Clock::now();
  a.send("101#01\n102#02\n103#03\n");
  CHECK_FALSE(b.wait_lines(1, 150));
  REQUIRE(b.wait_lines(3, 2000));
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(bus_test::Clock::now() - start).count();
  CHECK(elapsed >= 190);
  CHECK(b.lines == std::vector<std::string>{"101#01", "102#02", "103#03"});
  CHECK(server.stats().frames_delayed == 3);

  // After "clear" frames still queued keep their place, new ones follow them.
  CHECK(command(a, "!fault clear") == "!ok");
  a.send("104#04\n");
  REQUIRE(b.wait_lines(4));
  CHECK(b.lines[3] == "104#04");
}
