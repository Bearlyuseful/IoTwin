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
