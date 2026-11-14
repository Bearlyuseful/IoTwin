// Electrical simulation core: solver, installation model, protection, measurements, loader.

#include <catch2/catch_all.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "el_circuit.h"
#include "el_install.h"
#include "elsim.h"

using Catch::Approx;

namespace {

// A small single-phase installation built in code:
//   supply -> [rcd] -> breaker -> cable -> load (with PE)
struct Installation {
  ElModel m{};
  explicit Installation(bool tt = false, bool with_rcd = true, double load_w = 2000.0) {
    el_model_init(&m);
    const int s = el_model_add_device(&m, "supply", EL_SUPPLY);
    if (tt) {
      m.devices[s].earthing = EL_TT;
      m.devices[s].earth_electrode_ohm = 30.0;
    }
    if (with_rcd) {
      el_model_add_device(&m, "rcd", EL_RCD);
    }
    el_model_add_device(&m, "f1", EL_BREAKER);
    const int l = el_model_add_device(&m, "load", EL_LOAD);
    m.devices[l].rated_power_w = load_w;
    el_model_add_cable(&m, "w1", "NYM-J 3x2.5", 2.5, 15.0);
    if (with_rcd) {
      REQUIRE(el_model_connect(&m, "a", "supply:L1", "rcd:L_in", "L", nullptr) >= 0);
      REQUIRE(el_model_connect(&m, "b", "supply:N", "rcd:N_in", "N", nullptr) >= 0);
      REQUIRE(el_model_connect(&m, "c", "rcd:L_out", "f1:in", "L", nullptr) >= 0);
      REQUIRE(el_model_connect(&m, "n", "rcd:N_out", "load:N", "N", "w1") >= 0);
    } else {
      REQUIRE(el_model_connect(&m, "c", "supply:L1", "f1:in", "L", nullptr) >= 0);
      REQUIRE(el_model_connect(&m, "n", "supply:N", "load:N", "N", "w1") >= 0);
    }
    REQUIRE(el_model_connect(&m, "l", "f1:out", "load:L", "L", "w1") >= 0);
    REQUIRE(el_model_connect(&m, "pe", "supply:PE", "load:PE", "PE", "w1") >= 0);
  }
  ~Installation() { el_model_free(&m); }

  ElDevice& dev(const char* id) { return m.devices[el_model_find_device(&m, id)]; }
  void run(double seconds, double dt = 0.01) {
    for (double t = 0; t < seconds - 1e-9; t += dt) {
      REQUIRE(el_model_step(&m, dt));
    }
  }
  std::vector<std::string> events() {
    std::vector<std::string> out;
    ElEvent e;
    while (el_model_next_event(&m, &e)) {
      out.emplace_back(e.text);
    }
    return out;
  }
  double volts(const char* a, const char* b) {
    ElPhasor u;
    REQUIRE(el_model_measure_voltage(&m, a, b, &u));
    return el_abs(u);
  }
};

}  // namespace

TEST_CASE("Circuit solver: divider, current source and three-phase star", "[elsim]") {
  ElCircuit c;
  el_circuit_init(&c, 3);
  el_circuit_add_voltage_source(&c, 1, 0, el_phasor(10, 0));
  el_circuit_add_impedance(&c, 1, 2, el_phasor(3, 0));
  const int r2 = el_circuit_add_impedance(&c, 2, 0, el_phasor(2, 0));
  REQUIRE(el_circuit_solve(&c, 0.0));
  CHECK(el_real(el_circuit_voltage(&c, 2)) == Approx(4.0));
  CHECK(el_real(el_circuit_current(&c, r2)) == Approx(2.0));
  el_circuit_free(&c);

  // 1 A into node 1 through 5 Ohm to earth: 5 V
  el_circuit_init(&c, 2);
  el_circuit_add_impedance(&c, 1, 0, el_phasor(5, 0));
  el_circuit_add_current_source(&c, 0, 1, el_phasor(1, 0));
  REQUIRE(el_circuit_solve(&c, 0.0));
  CHECK(el_real(el_circuit_voltage(&c, 1)) == Approx(5.0));
  el_circuit_free(&c);

  // Symmetric three-phase load in star: no current in the neutral.
  el_circuit_init(&c, 5);  // 1..3 phases, 4 load star point
  for (int p = 0; p < 3; p++) {
    el_circuit_add_voltage_source(&c, 1 + p, 0, el_polar(230, -120.0 * p));
    el_circuit_add_impedance(&c, 1 + p, 4, el_phasor(23, 0));
  }
  const int neutral = el_circuit_add_impedance(&c, 4, 0, el_phasor(0.1, 0));
  REQUIRE(el_circuit_solve(&c, 0.0));
  CHECK(el_abs(el_circuit_current(&c, neutral)) < 1e-6);
  CHECK(el_abs(el_sub(el_circuit_voltage(&c, 1), el_circuit_voltage(&c, 2))) == Approx(398.4).margin(0.1));  // 400 V
  el_circuit_free(&c);
}

TEST_CASE("Installation: a load is supplied, current and power follow Ohm's law", "[elsim]") {
  Installation i;
  i.run(0.1);
  const ElDevice& load = i.dev("load");
  // 26.45 Ohm load + 0.35 Ohm source + 2 x 0.107 Ohm cable + contacts
  CHECK(load.current_a == Approx(230.0 / 27.03).epsilon(0.005));
  CHECK(load.power_w == Approx(load.current_a * load.current_a * 26.45).epsilon(0.001));
  CHECK(el_device_powered(&load));
  CHECK(i.dev("f1").current_a == Approx(load.current_a).epsilon(0.001));
  CHECK(i.dev("rcd").residual_a < 1e-6);
  CHECK(i.volts("load:L", "load:N") < 230.0);
  CHECK(i.volts("load:L", "load:N") > 220.0);
  CHECK(i.events().empty());
}

TEST_CASE("Installation: short circuit trips the breaker magnetically, not the RCD", "[elsim]") {
  Installation i;
  i.run(0.05);
  REQUIRE(el_model_add_fault(&i.m, "load:L", "load:N", 0.0, "short circuit") >= 0);
  i.run(0.01);
  CHECK(i.dev("f1").tripped);
  CHECK_FALSE(i.dev("rcd").tripped);
  CHECK_FALSE(el_device_powered(&i.dev("load")));
  const auto events = i.events();
  REQUIRE(events.size() == 1);
  CHECK(events[0].rfind("f1 tripped (short circuit", 0) == 0);

  el_model_clear_faults(&i.m);
  CHECK(el_model_reset(&i.m, "f1"));
  i.run(0.05);
  CHECK(el_device_powered(&i.dev("load")));
}

TEST_CASE("Installation: overload trips thermally after tens of seconds", "[elsim]") {
  Installation i(false, true, 6000.0);  // 26 A on a 16 A breaker
  i.run(20.0, 0.1);
  CHECK_FALSE(i.dev("f1").tripped);
  i.run(30.0, 0.1);
  CHECK(i.dev("f1").tripped);
  const auto events = i.events();
  REQUIRE_FALSE(events.empty());
  CHECK(events[0].find("overload") != std::string::npos);
}

TEST_CASE("Installation: insulation fault to PE trips the RCD", "[elsim]") {
  Installation i;
  i.run(0.05);
  REQUIRE(el_model_add_fault(&i.m, "load:L", "load:PE", 2000.0, "insulation fault") >= 0);
  i.run(0.01);
  CHECK(i.dev("rcd").tripped);  // about 115 mA residual current
  CHECK_FALSE(i.dev("f1").tripped);
  CHECK(i.events()[0].rfind("rcd tripped (residual current", 0) == 0);
  CHECK_FALSE(el_model_reset(&i.m, "load"));
  el_model_clear_faults(&i.m);
  CHECK(el_model_set_on(&i.m, "rcd", true));
  i.run(0.05);
  CHECK(el_device_powered(&i.dev("load")));
}

TEST_CASE("Installation: RCD test button", "[elsim]") {
  Installation i;
  i.run(0.05);
  CHECK(el_model_press_test(&i.m, "rcd"));
  i.run(0.01);
  CHECK(i.dev("rcd").tripped);
  CHECK(i.events()[0].find("test button") != std::string::npos);
  CHECK_FALSE(el_model_press_test(&i.m, "f1"));
}

TEST_CASE("Installation: in a TT system only the RCD clears an earth fault", "[elsim]") {
  {
    Installation no_rcd(true, false);
    no_rcd.run(0.05);
    REQUIRE(el_model_add_fault(&no_rcd.m, "load:L", "load:PE", 0.0, "earth fault") >= 0);
    no_rcd.run(1.0);
    // 230 V / (30 + 1 Ohm earth electrodes) = 7 A: far below 5 x 16 A, the breaker holds ...
    CHECK_FALSE(no_rcd.dev("f1").tripped);
    // ... and the enclosure of the load is at a deadly touch voltage.
    CHECK(no_rcd.volts("load:PE", "supply:N") > 150.0);
  }
  Installation with_rcd(true, true);
  with_rcd.run(0.05);
  REQUIRE(el_model_add_fault(&with_rcd.m, "load:L", "load:PE", 0.0, "earth fault") >= 0);
  with_rcd.run(0.01);
  CHECK(with_rcd.dev("rcd").tripped);
}

TEST_CASE("Installation: interrupted neutral makes N dangerous", "[elsim]") {
  Installation i;
  i.run(0.05);
  CHECK(el_model_set_connection_open(&i.m, "n", true));
  i.run(0.05);
  CHECK_FALSE(el_device_powered(&i.dev("load")));
  CHECK(i.volts("load:N", "load:PE") > 200.0);  // N takes the line potential through the load
  CHECK_FALSE(el_model_set_connection_open(&i.m, "missing", true));
}

TEST_CASE("Measurements: loop impedance and insulation resistance", "[elsim]") {
  Installation i;
  i.run(0.05);
  ElPhasor z;
  REQUIRE(el_model_measure_impedance(&i.m, "load:L", "load:PE", &z));
  // Ze 0.35 + L and PE conductors 2 x 0.107 + contacts, the load path in parallel lowers it slightly
  CHECK(el_abs(z) == Approx(0.567).epsilon(0.02));
  // The prospective fault current 230 V / Zs is far above 5 x 16 A: the breaker protects.
  CHECK(230.0 / el_abs(z) > 80.0);

  double ohm = 0;
  CHECK_FALSE(el_model_measure_insulation(&i.m, "load:L", "load:N", &ohm));  // live: tester refuses
  REQUIRE(el_model_set_on(&i.m, "f1", false));
  i.run(0.01);
  REQUIRE(el_model_measure_insulation(&i.m, "load:L", "load:N", &ohm));
  CHECK(ohm == Approx(26.45 + 0.21).epsilon(0.01));  // the load is still connected
  REQUIRE(el_model_set_on(&i.m, "load", false));     // switch the appliance off
  REQUIRE(el_model_measure_insulation(&i.m, "load:L", "load:N", &ohm));
  CHECK(ohm > 100e6);
}

TEST_CASE("Installation building rejects wrong references", "[elsim]") {
  ElModel m;
  el_model_init(&m);
  CHECK(el_model_add_device(&m, "s", EL_SUPPLY) == 0);
  CHECK(el_model_add_device(&m, "s", EL_LOAD) == -1);  // duplicate id
  CHECK(el_model_add_device(&m, "l", EL_LOAD) == 1);
  CHECK(el_model_connect(&m, nullptr, "s:L1", "l:L", "L", nullptr) == 0);
  CHECK(std::string(m.connections[0].id) == "c1");
  CHECK(el_model_connect(&m, nullptr, "s:L9", "l:L", "L", nullptr) == -1);
  CHECK(el_model_connect(&m, nullptr, "x:L1", "l:L", "L", nullptr) == -1);
  CHECK(el_model_connect(&m, nullptr, "s:N", "l:N", "N", "nocable") == -1);
  CHECK(el_model_connect(&m, nullptr, "l:L", "l:L", "L", nullptr) == -1);
  CHECK(el_model_terminal_node(&m, "l:PE") == m.devices[1].first_node + 2);
  CHECK(el_model_add_fault(&m, "l:L", "l:Q", 1.0, nullptr) == -1);
  CHECK_FALSE(el_model_plug(&m, "s", "l"));
  ElDeviceType type;
  CHECK(el_device_type_from_name("rcd", &type));
  CHECK(type == EL_RCD);
  CHECK_FALSE(el_device_type_from_name("toaster", &type));
  CHECK(std::string(el_terminal_name(EL_RCD, 2)) == "L_out");
  el_model_free(&m);
}

TEST_CASE("Loader: the demo flat from data/ with components", "[elsim]") {
  ElModel m;
  el_model_init(&m);
  char error[256] = "";
  REQUIRE(el_load_installation(&m, IOTWIN_DATA_DIR "/installations/demo-flat.json", IOTWIN_DATA_DIR "/components",
                               error, sizeof(error)));
  INFO(error);
  for (int k = 0; k < 10; k++) {
    REQUIRE(el_model_step(&m, 0.01));
  }
  const ElDevice& f1 = m.devices[el_model_find_device(&m, "f1")];
  const ElDevice& lamp = m.devices[el_model_find_device(&m, "lamp")];
  const ElDevice& kettle = m.devices[el_model_find_device(&m, "kettle")];
  CHECK(f1.rated_current_a == 10.0);  // from generic.mcb-b10
  CHECK(lamp.rated_power_w == 60.0);
  CHECK(el_device_powered(&lamp));
  CHECK(lamp.power_w == Approx(59).margin(1.5));
  CHECK(kettle.power_w == Approx(1915).margin(25));
  CHECK(m.devices[el_model_find_device(&m, "rcd")].residual_rated_a == Approx(0.03));
  el_model_free(&m);

  el_model_init(&m);
  CHECK_FALSE(el_load_installation_text(&m, R"({"id": "x", "devices": [{"id": "a", "type": "toaster"}]})",
                                        nullptr, error, sizeof(error)));
  CHECK(std::string(error).find("unknown type 'toaster'") != std::string::npos);
  el_model_free(&m);
  el_model_init(&m);
  CHECK_FALSE(el_load_installation_text(&m, R"({"id": "x", "devices": [{"id": "a", "type": "breaker",
      "component": "nope"}]})", IOTWIN_DATA_DIR "/components", error, sizeof(error)));
  CHECK(std::string(error).find("component 'nope' not found") != std::string::npos);
  el_model_free(&m);
}
