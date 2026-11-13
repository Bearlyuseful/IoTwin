// Electrical model of a switched output: supply, fuse, smart switch, resistive load.

#include <catch2/catch_all.hpp>

#include "power_path.h"

TEST_CASE("Power path: nominal lamp with PWM", "[power]") {
  PowerPath path;
  power_path_init(&path);
  PowerReading r;
  CHECK(power_path_nominal_current(&path) == Catch::Approx(1.75));

  power_path_step(&path, false, 1.0, 10, &r);
  CHECK(r.current_a == 0.0);
  CHECK(r.supply_v == Catch::Approx(12.6));

  power_path_step(&path, true, 1.0, 10, &r);
  const double on = 12.6 / (12.0 * 12.0 / 21.0 + 0.05 + 0.02);
  CHECK(r.current_a == Catch::Approx(on));
  CHECK(r.power_w == Catch::Approx(on * on * 12.0 * 12.0 / 21.0));
  CHECK(r.duty == 1.0);

  power_path_step(&path, true, 0.3, 10, &r);
  CHECK(r.current_a == Catch::Approx(0.3 * on));
  CHECK(r.power_w == Catch::Approx(0.3 * on * on * 12.0 * 12.0 / 21.0));
  power_path_step(&path, true, 7.0, 10, &r);  // clamped
  CHECK(r.duty == 1.0);
  CHECK(path.fuse.heat == 0.0);
}

TEST_CASE("Power path: short circuit trips the smart switch, the fuse survives", "[power]") {
  PowerPath path;
  power_path_init(&path);
  path.load.shorted = true;
  PowerReading r;
  power_path_step(&path, true, 1.0, 10, &r);
  CHECK(r.tripped);
  CHECK(r.current_a == 0.0);
  CHECK_FALSE(r.fuse_blown);
  for (int i = 0; i < 9; i++) {
    power_path_step(&path, true, 1.0, 10, &r);
    CHECK(r.tripped);
  }
  path.load.shorted = false;
  power_path_step(&path, true, 1.0, 10, &r);  // 100 ms: retry, now fine
  CHECK_FALSE(r.tripped);
  CHECK(r.current_a > 1.7);

  path.load.shorted = true;
  power_path_step(&path, true, 1.0, 10, &r);
  CHECK(r.tripped);
  power_path_step(&path, false, 1.0, 10, &r);  // switching off resets the protection
  CHECK_FALSE(r.tripped);
}

TEST_CASE("Power path: slow fuse under overload, cooling below the rating", "[power]") {
  PowerPath path;
  power_path_init(&path);
  path.load.power_factor = 3.0;
  PowerReading r;
  power_path_step(&path, true, 1.0, 1000, &r);
  const double excess = r.current_a * r.current_a - 9.0;
  CHECK(path.fuse.heat == Catch::Approx(excess));
  CHECK_FALSE(r.fuse_blown);
  power_path_step(&path, false, 1.0, 1000, &r);  // cools 5 A^2 s per second
  CHECK(path.fuse.heat == Catch::Approx(excess - 5.0));
  power_path_step(&path, true, 1.0, 1000, &r);
  power_path_step(&path, true, 1.0, 1000, &r);
  CHECK(r.fuse_blown);
  CHECK(r.current_a > 0.0);  // this step still conducted
  power_path_step(&path, true, 1.0, 10, &r);
  CHECK(r.current_a == 0.0);
  CHECK(r.fuse_blown);
  power_path_replace_fuse(&path);
  path.load.power_factor = 1.0;
  power_path_step(&path, true, 1.0, 10, &r);
  CHECK(r.current_a > 1.7);
}

TEST_CASE("Power path: open load and low supply", "[power]") {
  PowerPath path;
  power_path_init(&path);
  path.load.open = true;
  PowerReading r;
  power_path_step(&path, true, 1.0, 10, &r);
  CHECK(r.current_a == 0.0);
  CHECK_FALSE(r.tripped);
  path.load.open = false;
  path.supply.open_circuit_v = 9.8;
  power_path_step(&path, true, 1.0, 10, &r);
  CHECK(r.supply_v < 9.8);
  CHECK(r.current_a == Catch::Approx(9.8 / (12.0 * 12.0 / 21.0 + 0.07)));
}
