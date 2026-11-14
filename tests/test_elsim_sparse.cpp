// Electrical simulation: sparse solver, its cache and the generated house.

#include <catch2/catch_all.hpp>

#include <cmath>
#include <vector>

#include "el_circuit.h"
#include "el_generate.h"
#include "el_sparse.h"
#include "elsim.h"

using Catch::Approx;

namespace {

unsigned next(unsigned& state) {
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}

double uniform(unsigned& state, double lo, double hi) { return lo + (hi - lo) * (next(state) % 100000u) / 100000.0; }

// A random passive network: a spanning tree plus extra branches, resistive and
// reactive admittances, a few current sources and links to earth.
void random_network(ElCircuit& c, int nodes, unsigned seed) {
  unsigned r = seed;
  el_circuit_init(&c, nodes);
  for (int node = 2; node < nodes; node++) {
    const int parent = 1 + (int)(next(r) % (unsigned)(node - 1));
    el_circuit_add_impedance(&c, node, parent, el_phasor(uniform(r, 0.01, 50.0), uniform(r, -5.0, 5.0)));
  }
  for (int extra = 0; extra < nodes / 3; extra++) {
    const int a = 1 + (int)(next(r) % (unsigned)(nodes - 1));
    const int b = 1 + (int)(next(r) % (unsigned)(nodes - 1));
    el_circuit_add_impedance(&c, a, b, el_phasor(uniform(r, 0.001, 1000.0), uniform(r, 0.0, 10.0)));
  }
  el_circuit_add_resistance(&c, 1, 0, 0.5);
  for (int s = 0; s < 4; s++) {
    const int node = 1 + (int)(next(r) % (unsigned)(nodes - 1));
    el_circuit_add_current_source(&c, 0, node, el_polar(uniform(r, 0.1, 20.0), uniform(r, -180.0, 180.0)));
  }
}

double max_difference(const ElCircuit& a, const ElCircuit& b) {
  double worst = 0.0;
  for (int node = 1; node < a.nodes; node++) {
    const ElPhasor x = el_circuit_voltage(&a, node);
    const ElPhasor y = el_circuit_voltage(&b, node);
    worst = std::max(worst, el_abs(el_sub(x, y)) / std::max(1e-6, el_abs(y)));
  }
  return worst;
}

}  // namespace

TEST_CASE("Sparse solver: same voltages as dense elimination on random networks", "[elsim][sparse]") {
  for (unsigned seed : {1u, 7u, 42u, 1234u}) {
    for (int nodes : {2, 3, 10, 60, 250}) {
      ElCircuit sparse{};
      ElCircuit dense{};
      random_network(sparse, nodes, seed);
      random_network(dense, nodes, seed);
      dense.solver = EL_SOLVER_DENSE;
      REQUIRE(el_circuit_solve(&sparse, EL_GMIN));
      REQUIRE(el_circuit_solve(&dense, EL_GMIN));
      INFO("seed " << seed << ", nodes " << nodes);
      CHECK(sparse.sparse_solutions == 1);
      CHECK(dense.dense_solutions == 1);
      CHECK(max_difference(sparse, dense) < 1e-9);
      el_circuit_free(&sparse);
      el_circuit_free(&dense);
    }
  }
}

TEST_CASE("Sparse solver: ordering once per topology, factorization only when values change", "[elsim][sparse]") {
  ElCircuit c{};
  random_network(c, 40, 3);
  REQUIRE(el_circuit_solve(&c, EL_GMIN));
  CHECK(c.analyses == 1);
  CHECK(c.factorizations == 1);
  const ElPhasor before = el_circuit_voltage(&c, 20);

  // Same network again (rebuilt element by element, as a simulation step does): only a substitution.
  ElCircuit again{};
  random_network(again, 40, 3);
  c.count = 0;
  for (size_t i = 0; i < again.count; i++) {
    const ElElement& e = again.elements[i];
    if (e.kind == EL_ADMITTANCE) {
      el_circuit_add_admittance(&c, e.a, e.b, e.value);
    } else {
      el_circuit_add_current_source(&c, e.a, e.b, e.value);
    }
  }
  REQUIRE(el_circuit_solve(&c, EL_GMIN));
  CHECK(c.analyses == 1);
  CHECK(c.factorizations == 1);
  CHECK(el_abs(el_sub(el_circuit_voltage(&c, 20), before)) < 1e-12);

  // Other value, same topology: refactorized, not reordered.
  c.elements[0].value = el_scale(c.elements[0].value, 2.0);
  REQUIRE(el_circuit_solve(&c, EL_GMIN));
  CHECK(c.analyses == 1);
  CHECK(c.factorizations == 2);

  // An element less (a switch opened): new ordering; back again: from the cache.
  const ElElement removed = c.elements[c.count - 5];
  c.elements[c.count - 5] = c.elements[c.count - 1];
  c.count--;
  REQUIRE(el_circuit_solve(&c, EL_GMIN));
  CHECK(c.analyses == 2);
  c.elements[c.count] = c.elements[c.count - 4];
  c.elements[c.count - 4] = removed;
  c.count++;
  REQUIRE(el_circuit_solve(&c, EL_GMIN));
  CHECK(c.analyses == 2);
  el_circuit_free(&c);
  el_circuit_free(&again);
}

TEST_CASE("Sparse solver: a network held only by gmin stays finite", "[elsim][sparse]") {
  // Two floating islands of low-ohmic links (an open circuit) next to a supplied part.
  ElCircuit c{};
  el_circuit_init(&c, 8);
  el_circuit_add_resistance(&c, 1, 0, 1.0);
  el_circuit_add_current_source(&c, 0, 1, el_phasor(10.0, 0.0));
  for (int node = 2; node < 4; node++) {
    el_circuit_add_resistance(&c, node, node + 1, 1e-6);
  }
  el_circuit_add_resistance(&c, 5, 6, 1e-3);
  el_circuit_add_resistance(&c, 6, 7, 1e-3);
  REQUIRE(el_circuit_solve(&c, EL_GMIN));
  CHECK(el_real(el_circuit_voltage(&c, 1)) == Approx(10.0));
  for (int node = 2; node < 8; node++) {
    CHECK(std::isfinite(el_abs(el_circuit_voltage(&c, node))));
    CHECK(el_abs(el_circuit_voltage(&c, node)) < 1e-6);
  }
  el_circuit_free(&c);
}

TEST_CASE("Generated house: supplied, solved sparse and the same as dense", "[elsim][sparse]") {
  ElHouseSpec spec = el_house_spec_default();
  spec.circuits = 6;
  spec.boxes = 3;
  ElModel sparse;
  ElModel dense;
  el_model_init(&sparse);
  el_model_init(&dense);
  REQUIRE(el_generate_house(&sparse, &spec));
  REQUIRE(el_generate_house(&dense, &spec));
  dense.circuit.solver = EL_SOLVER_DENSE;

  for (int i = 0; i < 50; i++) {
    REQUIRE(el_model_step(&sparse, 0.01));
    REQUIRE(el_model_step(&dense, 0.01));
  }
  size_t loads = 0;
  for (size_t i = 0; i < sparse.device_count; i++) {
    const ElDevice& d = sparse.devices[i];
    if (d.type == EL_LOAD) {
      loads++;
      CHECK(el_device_powered(&d));
    }
    CHECK_FALSE(d.tripped);
    CHECK(d.voltage_v_measured == Approx(dense.devices[i].voltage_v_measured).epsilon(1e-9));
    CHECK(d.current_a == Approx(dense.devices[i].current_a).epsilon(1e-9).margin(1e-9));
  }
  CHECK(loads >= 9);
  CHECK(sparse.circuit.dense_solutions == 0);
  CHECK(dense.circuit.dense_solutions >= 50);  // the reference really is dense
  CHECK(dense.circuit.sparse_solutions == 0);
  CHECK(sparse.circuit.analyses == 1);
  CHECK(sparse.circuit.factorizations == 1);  // 50 steps, nothing changed: one factorization

  // Short circuit in a socket circuit: the breaker trips, in both solvers alike.
  REQUIRE(el_model_add_fault(&sparse, "k1b0:L", "k1b0:N", 0.0, nullptr) >= 0);
  REQUIRE(el_model_add_fault(&dense, "k1b0:L", "k1b0:N", 0.0, nullptr) >= 0);
  REQUIRE(el_model_step(&sparse, 0.01));
  REQUIRE(el_model_step(&dense, 0.01));
  const ElDevice& f1 = sparse.devices[el_model_find_device(&sparse, "f1")];
  CHECK(f1.tripped);
  CHECK(dense.devices[el_model_find_device(&dense, "f1")].tripped);
  ElEvent event;
  REQUIRE(el_model_next_event(&sparse, &event));
  ElEvent reference;
  REQUIRE(el_model_next_event(&dense, &reference));
  CHECK(std::string(event.text) == std::string(reference.text));
  el_model_free(&sparse);
  el_model_free(&dense);
}

TEST_CASE("Generated house: same seed, same house", "[elsim][sparse]") {
  ElHouseSpec spec = el_house_spec_default();
  ElModel a;
  ElModel b;
  el_model_init(&a);
  el_model_init(&b);
  REQUIRE(el_generate_house(&a, &spec));
  REQUIRE(el_generate_house(&b, &spec));
  CHECK(a.node_count == b.node_count);
  CHECK(a.node_count > 1000);  // the benchmark size
  REQUIRE(a.cable_count == b.cable_count);
  for (size_t i = 0; i < a.cable_count; i++) {
    CHECK(a.cables[i].length_m == b.cables[i].length_m);
  }
  el_model_free(&a);
  el_model_free(&b);
}
