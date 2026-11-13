// Integration tests: real TCP connections against an in-process bus server.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "bus_server.h"
#include "line_reader.h"
#include "log.h"
#include "net.h"

namespace {

using Clock = std::chrono::steady_clock;

class RunningServer {
 public:
  explicit RunningServer(bool echo = false) {
    log_set_level(LOG_LEVEL_ERROR);
    BusServerConfig config;
    bus_server_default_config(&config);
    config.port = 0;  // ephemeral port, tests can run in parallel
    config.echo = echo;
    server_ = bus_server_create(&config);
    REQUIRE(server_ != nullptr);
    thread_ = std::thread([this] {
      while (!stop_) {
        bus_server_poll(server_, 10);
      }
    });
  }

  ~RunningServer() {
    stop_ = true;
    thread_.join();
    bus_server_destroy(server_);
  }

  uint16_t port() const { return bus_server_port(server_); }
  BusServerStats stats() const { return bus_server_get_stats(server_); }

  bool wait_for_clients(size_t count) const {
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline) {
      if (bus_server_client_count(server_) == count) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

  bool wait_for_routed(uint64_t routed) const {
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline) {
      if (stats().frames_routed + stats().frames_rejected >= routed) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }

 private:
  BusServer* server_ = nullptr;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};

class TestClient {
 public:
  explicit TestClient(uint16_t port) {
    REQUIRE(net_init());
    socket_ = net_connect_tcp("127.0.0.1", port);
    REQUIRE(socket_ != NET_INVALID_SOCKET);
    line_reader_init(&reader_);
  }

  ~TestClient() {
    close();
    net_cleanup();
  }

  void close() {
    net_close(socket_);
    socket_ = NET_INVALID_SOCKET;
  }

  void send(const std::string& data) {
    REQUIRE(net_send_all(socket_, data.data(), data.size(), 1000) == 0);
  }

  // Waits until at least `count` lines arrived or the timeout expired.
  bool wait_lines(size_t count, int timeout_ms = 2000) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (lines.size() < count) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (remaining <= 0) {
        return false;
      }
      NetPollFd entry{socket_, NET_POLL_IN, 0};
      if (net_poll(&entry, 1, static_cast<int>(remaining)) <= 0) {
        continue;
      }
      char buffer[512];
      int received = net_recv(socket_, buffer, sizeof(buffer));
      if (received <= 0) {
        return false;
      }
      line_reader_feed(&reader_, buffer, static_cast<size_t>(received), &TestClient::on_line, this);
    }
    return true;
  }

  std::vector<std::string> lines;

 private:
  static void on_line(const char* line, size_t length, void* user) {
    static_cast<TestClient*>(user)->lines.emplace_back(line, length);
  }

  net_socket_t socket_ = NET_INVALID_SOCKET;
  LineReader reader_;
};

}  // namespace

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
