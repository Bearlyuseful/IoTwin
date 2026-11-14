// Electrical simulation: dimmer, junction and the session interface (ADR 0008).

#include <catch2/catch_all.hpp>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "el_session.h"
#include "elsim.h"

using Catch::Approx;

namespace {

// supply -> f1 (B10) -> dimmer -> junction "box" -> lamp (100 W), N and PE through the box.
void build_dimmed_lamp(ElModel& m) {
  el_model_init(&m);
  el_model_add_device(&m, "supply", EL_SUPPLY);
  const int f1 = el_model_add_device(&m, "f1", EL_BREAKER);
  m.devices[f1].rated_current_a = 10.0;
  el_model_add_device(&m, "dim", EL_DIMMER);
  el_model_add_device(&m, "box", EL_JUNCTION);
  el_model_add_device(&m, "lamp", EL_LOAD);
  el_model_add_cable(&m, "w", "NYM-J 3x1.5", 1.5, 10.0);
  REQUIRE(el_model_connect(&m, nullptr, "supply:L1", "f1:in", "L", nullptr) >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "f1:out", "dim:in", "L", nullptr) >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "dim:out", "box:L", "L", "w") >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "supply:N", "box:N", "N", "w") >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "supply:PE", "box:PE", "PE", "w") >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "box:L", "lamp:L", "L", nullptr) >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "box:N", "lamp:N", "N", nullptr) >= 0);
  REQUIRE(el_model_connect(&m, nullptr, "box:PE", "lamp:PE", "PE", nullptr) >= 0);
  REQUIRE(el_model_set_on(&m, "dim", true));
}

const ElDevice& device(const ElModel& m, const char* id) { return m.devices[el_model_find_device(&m, id)]; }

}  // namespace

TEST_CASE("Dimmer: power follows the level, current its square root", "[elsim]") {
  ElModel m;
  build_dimmed_lamp(m);
  REQUIRE(el_model_step(&m, 0.01));
  const double full_power = device(m, "lamp").power_w;
  const double full_current = device(m, "lamp").current_a;
  CHECK(full_power == Approx(100.0).epsilon(0.03));
  CHECK(device(m, "box").voltage_v_measured == Approx(229.0).margin(2.0));  // junction: L-N
  CHECK(el_device_powered(&device(m, "lamp")));

  for (double level : {0.75, 0.25, 0.04}) {
    REQUIRE(el_model_set_level(&m, "dim", level));
    REQUIRE(el_model_step(&m, 0.01));
    INFO("level " << level);
    CHECK(device(m, "lamp").power_w == Approx(level * full_power).epsilon(0.01));
    CHECK(device(m, "lamp").current_a == Approx(std::sqrt(level) * full_current).epsilon(0.01));
    CHECK(device(m, "dim").current_a == Approx(device(m, "lamp").current_a).epsilon(0.001));  // series element
  }

  REQUIRE(el_model_set_level(&m, "dim", 0.0));
  REQUIRE(el_model_step(&m, 0.01));
  CHECK_FALSE(el_device_powered(&device(m, "lamp")));
  REQUIRE(el_model_set_level(&m, "dim", 1.0));
  REQUIRE(el_model_set_on(&m, "dim", false));
  REQUIRE(el_model_step(&m, 0.01));
  CHECK_FALSE(el_device_powered(&device(m, "lamp")));
  CHECK(device(m, "lamp").power_w < 0.01);
  CHECK_FALSE(el_model_set_level(&m, "lamp", 0.5));  // only dimmers have a level
  el_model_free(&m);
}

TEST_CASE("Session: commands in, snapshot and events out", "[elsim][session]") {
  ElModel m;
  build_dimmed_lamp(m);
  ElSession* s = el_session_create(&m);
  REQUIRE(s != nullptr);
  CHECK(m.devices == nullptr);  // the model was moved into the session
  const int lamp = el_session_find_device(s, "lamp");
  const int f1 = el_session_find_device(s, "f1");
  REQUIRE(lamp >= 0);
  REQUIRE(f1 >= 0);
  CHECK(std::string(el_session_device_id(s, (size_t)lamp)) == "lamp");
  CHECK(el_session_device_type(s, (size_t)lamp) == EL_LOAD);
  CHECK(el_session_rated_power(s, (size_t)lamp) == 100.0);
  CHECK(el_session_snapshot(s)->step == 0);  // nothing computed yet

  REQUIRE(el_session_step(s, 0.01));
  const ElSnapshot* snap = el_session_snapshot(s);
  CHECK(snap->step == 1);
  CHECK(snap->solved);
  CHECK(snap->device_count == el_session_device_count(s));
  CHECK((snap->devices[lamp].flags & EL_STATE_POWERED) != 0);
  const double full_power = snap->devices[lamp].power_w;

  ElCommand level = el_command(EL_CMD_SET_LEVEL, "dim", nullptr, 0.5);
  REQUIRE(el_session_send(s, &level));
  CHECK(el_session_snapshot(s)->devices[lamp].power_w == Approx(full_power));  // applied in the next step
  REQUIRE(el_session_step(s, 0.01));
  snap = el_session_snapshot(s);
  CHECK(snap->devices[lamp].power_w == Approx(0.5 * full_power).epsilon(0.01));
  CHECK(snap->devices[el_session_find_device(s, "dim")].level == 0.5);

  ElCommand fault = el_command(EL_CMD_ADD_FAULT, "box:L", "box:N", 0.0);
  ElCommand full = el_command(EL_CMD_SET_LEVEL, "dim", nullptr, 1.0);
  REQUIRE(el_session_send(s, &full));
  REQUIRE(el_session_send(s, &fault));
  REQUIRE(el_session_step(s, 0.01));
  snap = el_session_snapshot(s);
  CHECK((snap->devices[f1].flags & EL_STATE_TRIPPED) != 0);
  CHECK((snap->devices[lamp].flags & EL_STATE_POWERED) == 0);
  ElEvent event;
  REQUIRE(el_session_next_event(s, &event));
  CHECK(std::string(event.text).find("f1 tripped (short circuit") == 0);
  CHECK_FALSE(el_session_next_event(s, &event));

  ElCommand clear = el_command(EL_CMD_CLEAR_FAULTS, nullptr, nullptr, 0.0);
  ElCommand reset = el_command(EL_CMD_RESET, "f1", nullptr, 0.0);
  ElCommand wrong = el_command(EL_CMD_RESET, "lamp", nullptr, 0.0);
  REQUIRE(el_session_send(s, &clear));
  REQUIRE(el_session_send(s, &reset));
  REQUIRE(el_session_send(s, &wrong));
  REQUIRE(el_session_step(s, 0.01));
  CHECK((el_session_snapshot(s)->devices[lamp].flags & EL_STATE_POWERED) != 0);
  REQUIRE(el_session_next_event(s, &event));
  CHECK(std::string(event.text) == "command rejected: reset lamp ");
  CHECK(el_session_dropped_events(s) == 0);
  el_session_destroy(s);
}

TEST_CASE("Session: a full command queue is reported, not overwritten", "[elsim][session]") {
  ElModel m;
  build_dimmed_lamp(m);
  ElSession* s = el_session_create(&m);
  REQUIRE(s != nullptr);
  const ElCommand command = el_command(EL_CMD_SET_LEVEL, "dim", nullptr, 0.3);
  for (int i = 0; i < EL_SESSION_QUEUE; i++) {
    REQUIRE(el_session_send(s, &command));
  }
  CHECK_FALSE(el_session_send(s, &command));
  REQUIRE(el_session_step(s, 0.01));  // empties the queue
  CHECK(el_session_send(s, &command));
  el_session_destroy(s);
}

TEST_CASE("Session: simulation thread and reader run in parallel", "[elsim][session]") {
  ElModel m;
  build_dimmed_lamp(m);
  ElSession* s = el_session_create(&m);
  REQUIRE(s != nullptr);
  const int lamp = el_session_find_device(s, "lamp");
  constexpr int kSteps = 4000;

  std::atomic<bool> ok{true};
  std::thread simulation([&] {
    for (int i = 0; i < kSteps; i++) {
      ok = el_session_step(s, 0.01) && ok;
    }
  });

  // Reader and command sender on this thread: snapshots never go back in time
  // and are always complete (power matches one of the two levels).
  uint64_t last = 0;
  int sent = 0;
  bool consistent = true;
  while (last < kSteps) {
    const ElSnapshot* snap = el_session_snapshot(s);
    consistent = consistent && snap->step >= last && snap->device_count == el_session_device_count(s);
    if (snap->step > 0) {
      const double p = snap->devices[lamp].power_w;
      const double level = snap->devices[el_session_find_device(s, "dim")].level;
      consistent = consistent && p == Approx(level * 98.0).margin(4.0);
    }
    last = snap->step;
    if (sent < 50 && last > (uint64_t)sent * 50) {
      const ElCommand c = el_command(EL_CMD_SET_LEVEL, "dim", nullptr, sent % 2 ? 1.0 : 0.5);
      sent += el_session_send(s, &c) ? 1 : 0;
    }
    std::this_thread::yield();
  }
  simulation.join();
  CHECK(ok);
  CHECK(consistent);
  CHECK(sent == 50);
  CHECK(el_session_snapshot(s)->step == (uint64_t)kSteps);
  el_session_destroy(s);
}
