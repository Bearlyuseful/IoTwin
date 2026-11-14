/*
 * elsim_bench - timing of the electrical simulation on a generated house.
 *
 * Measures what matters for real time: the first step (ordering and
 * factorization), the steady step, a switch toggled (new or cached topology),
 * a short circuit with breaker trip - and, with --dense, one dense step for
 * comparison and a check that both solvers give the same result.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "el_generate.h"
#include "elsim.h"

static double now_s(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double timed_step(ElModel* m) {
  const double start = now_s();
  if (!el_model_step(m, 0.01)) {
    fprintf(stderr, "network cannot be solved\n");
    exit(2);
  }
  return now_s() - start;
}

static void print_time(const char* what, double seconds) {
  if (seconds >= 0.1) {
    printf("  %-34s %9.1f ms\n", what, seconds * 1e3);
  } else {
    printf("  %-34s %9.1f us\n", what, seconds * 1e6);
  }
}

static void usage(const char* program) {
  printf("Usage: %s [--circuits N] [--boxes M] [--steps S] [--seed X] [--dense]\n\n"
         "Generates a house (three-phase supply, RCD groups, N circuits with M junction\n"
         "boxes each) and times the electrical simulation. --dense also times the dense\n"
         "solver and compares the results (exit code 1 if they differ).\n",
         program);
}

static bool build(ElModel* m, const ElHouseSpec* spec) {
  el_model_init(m);
  if (!el_generate_house(m, spec)) {
    fprintf(stderr, "cannot generate the house\n");
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  ElHouseSpec spec = el_house_spec_default();
  int steps = 1000;
  bool dense = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--circuits") == 0 && i + 1 < argc) {
      spec.circuits = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--boxes") == 0 && i + 1 < argc) {
      spec.boxes = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
      steps = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      spec.seed = (unsigned)strtoul(argv[++i], NULL, 10);
    } else if (strcmp(argv[i], "--dense") == 0) {
      dense = true;
    } else {
      usage(argv[0]);
      return strcmp(argv[i], "--help") == 0 ? 0 : 2;
    }
  }
  if (spec.circuits < 1 || spec.boxes < 1 || steps < 1) {
    usage(argv[0]);
    return 2;
  }

  ElModel m;
  if (!build(&m, &spec)) {
    return 2;
  }
  size_t powered = 0;
  size_t loads = 0;
  printf("House: %d circuits x %d boxes: %zu devices, %d nodes, %zu conductors\n", spec.circuits, spec.boxes,
         m.device_count, m.node_count, m.connection_count);

  /* Sparse: first step, steady state, topology changes, short circuit. */
  printf("Sparse nodal analysis (LDL^T, minimum degree):\n");
  const double first = timed_step(&m);
  for (size_t i = 0; i < m.device_count; i++) {
    loads += m.devices[i].type == EL_LOAD;
    powered += el_device_powered(&m.devices[i]);
  }
  print_time("first step (ordering + factor)", first);
  printf("  %-34s %9zu (%zu of %zu loads powered)\n", "non-zeros of L", m.circuit.fill, powered, loads);
  double total = 0.0;
  double worst = 0.0;
  for (int i = 0; i < steps; i++) {
    const double t = timed_step(&m);
    total += t;
    worst = t > worst ? t : worst;
  }
  char label[64];
  snprintf(label, sizeof(label), "steady step, mean of %d", steps);
  print_time(label, total / steps);
  print_time("steady step, worst", worst);
  const unsigned long factorizations = m.circuit.factorizations;

  el_model_set_on(&m, "s0b0", false);
  print_time("switch off (refactorization only)", timed_step(&m));
  el_model_set_on(&m, "s0b0", true);
  print_time("switch on again", timed_step(&m));
  const bool has_socket_circuit = spec.circuits > 1;
  if (has_socket_circuit) {
    el_model_add_fault(&m, "k1b0:L", "k1b0:N", 0.0, NULL);
    print_time("short circuit (new element), trip", timed_step(&m));
    ElEvent event;
    while (el_model_next_event(&m, &event)) {
      printf("  event: %s\n", event.text);
    }
    el_model_clear_faults(&m);
    el_model_reset(&m, "f1");
    timed_step(&m);
  }
  printf("  %-34s %9lu analyses, %lu factorizations (%lu in %d steady steps)\n", "solver work", m.circuit.analyses,
         m.circuit.factorizations, factorizations - 1, steps);

  int result = 0;
  if (dense) {
    ElModel reference;
    if (!build(&reference, &spec)) {
      return 2;
    }
    reference.circuit.solver = EL_SOLVER_DENSE;
    printf("Dense modified nodal analysis (reference):\n");
    print_time("one step", timed_step(&reference));
    /* Same state in both models: compare the results. */
    ElModel sparse;
    build(&sparse, &spec);
    timed_step(&sparse);
    double worst_difference = 0.0;
    for (size_t i = 0; i < sparse.device_count; i++) {
      const ElDevice* s = &sparse.devices[i];
      const ElDevice* d = &reference.devices[i];
      const double values[3][2] = {{s->voltage_v_measured, d->voltage_v_measured},
                                   {s->current_a, d->current_a},
                                   {s->power_w, d->power_w}};
      for (int v = 0; v < 3; v++) {
        const double scale = fmax(1e-3, fabs(values[v][1]));
        const double difference = fabs(values[v][0] - values[v][1]) / scale;
        worst_difference = difference > worst_difference ? difference : worst_difference;
      }
    }
    printf("  %-34s %9.2e (dense solutions: %lu)\n", "largest relative difference", worst_difference,
           reference.circuit.dense_solutions);
    if (worst_difference > 1e-6) {
      printf("  RESULTS DIFFER\n");
      result = 1;
    }
    el_model_free(&sparse);
    el_model_free(&reference);
  }
  el_model_free(&m);
  return result;
}
