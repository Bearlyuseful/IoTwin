#include "el_circuit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EL_PI 3.14159265358979323846

void el_circuit_init(ElCircuit* circuit, int nodes) {
  memset(circuit, 0, sizeof(*circuit));
  circuit->nodes = nodes > 1 ? nodes : 1;
}

void el_circuit_free(ElCircuit* circuit) {
  free(circuit->elements);
  free(circuit->voltages);
  free(circuit->source_currents);
  free(circuit->matrix);
  memset(circuit, 0, sizeof(*circuit));
}

void el_circuit_clear(ElCircuit* circuit) {
  circuit->count = 0;
  circuit->solved = false;
}

double el_abs(ElPhasor value) { return hypot(value.re, value.im); }

double el_angle_deg(ElPhasor value) { return atan2(value.im, value.re) * 180.0 / EL_PI; }

ElPhasor el_polar(double magnitude, double degrees) {
  const double radians = degrees * EL_PI / 180.0;
  return el_phasor(magnitude * cos(radians), magnitude * sin(radians));
}

static int add(ElCircuit* circuit, ElElementKind kind, int a, int b, ElPhasor value) {
  if (a < 0 || b < 0 || a >= circuit->nodes || b >= circuit->nodes) {
    return -1;
  }
  if (circuit->count == circuit->capacity) {
    const size_t capacity = circuit->capacity ? circuit->capacity * 2 : 64;
    ElElement* grown = (ElElement*)realloc(circuit->elements, capacity * sizeof(ElElement));
    if (grown == NULL) {
      return -1;
    }
    circuit->elements = grown;
    circuit->capacity = capacity;
  }
  ElElement* e = &circuit->elements[circuit->count];
  e->kind = kind;
  e->a = a;
  e->b = b;
  e->value = value;
  circuit->solved = false;
  return (int)circuit->count++;
}

int el_circuit_add_admittance(ElCircuit* circuit, int a, int b, ElPhasor admittance) {
  return add(circuit, EL_ADMITTANCE, a, b, admittance);
}

int el_circuit_add_impedance(ElCircuit* circuit, int a, int b, ElPhasor impedance) {
  /* A zero impedance would be infinite admittance: clamp to 1 micro-ohm. */
  if (el_abs(impedance) < 1e-6) {
    impedance = el_phasor(1e-6, 0.0);
  }
  return add(circuit, EL_ADMITTANCE, a, b, el_div(el_phasor(1.0, 0.0), impedance));
}

int el_circuit_add_resistance(ElCircuit* circuit, int a, int b, double ohm) {
  return el_circuit_add_impedance(circuit, a, b, el_phasor(ohm, 0.0));
}

int el_circuit_add_voltage_source(ElCircuit* circuit, int a, int b, ElPhasor voltage) {
  return add(circuit, EL_VOLTAGE_SOURCE, a, b, voltage);
}

int el_circuit_add_current_source(ElCircuit* circuit, int a, int b, ElPhasor current) {
  return add(circuit, EL_CURRENT_SOURCE, a, b, current);
}

/* Makes sure *buffer holds at least count phasors; keeps the memory between solutions. */
static bool reserve(ElPhasor** buffer, size_t* capacity, size_t count) {
  if (count <= *capacity && *buffer != NULL) {
    return true;
  }
  ElPhasor* grown = (ElPhasor*)realloc(*buffer, count * sizeof(ElPhasor));
  if (grown == NULL) {
    return false;
  }
  *buffer = grown;
  *capacity = count;
  return true;
}

bool el_circuit_solve(ElCircuit* circuit, double gmin) {
  circuit->solved = false;
  const int nodes = circuit->nodes - 1; /* unknown node voltages (without earth) */
  int sources = 0;
  for (size_t i = 0; i < circuit->count; i++) {
    sources += circuit->elements[i].kind == EL_VOLTAGE_SOURCE;
  }
  const int n = nodes + sources;

  /* Node voltages: allocated once per node count (nodes never change between solutions). */
  if (circuit->voltages == NULL) {
    circuit->voltages = (ElPhasor*)calloc((size_t)circuit->nodes, sizeof(ElPhasor));
  }
  const size_t elements = circuit->count ? circuit->count : 1;
  if (circuit->voltages == NULL || !reserve(&circuit->source_currents, &circuit->result_capacity, elements)) {
    return false;
  }
  memset(circuit->voltages, 0, (size_t)circuit->nodes * sizeof(ElPhasor));
  memset(circuit->source_currents, 0, elements * sizeof(ElPhasor));
  if (n == 0) {
    circuit->solved = true;
    return true;
  }

  /* Augmented matrix n x (n + 1), row-major. */
  const size_t width = (size_t)n + 1;
  if (!reserve(&circuit->matrix, &circuit->matrix_capacity, (size_t)n * width)) {
    return false;
  }
  ElPhasor* m = circuit->matrix;
  memset(m, 0, (size_t)n * width * sizeof(ElPhasor));
#define M(r, c) m[(size_t)(r) * width + (size_t)(c)]
  for (int k = 0; k < nodes; k++) {
    M(k, k).re += gmin;
  }
  int source_row = nodes;
  for (size_t i = 0; i < circuit->count; i++) {
    const ElElement* e = &circuit->elements[i];
    const int a = e->a - 1; /* -1 = earth */
    const int b = e->b - 1;
    switch (e->kind) {
      case EL_ADMITTANCE:
        if (a >= 0) {
          M(a, a) = el_add(M(a, a), e->value);
        }
        if (b >= 0) {
          M(b, b) = el_add(M(b, b), e->value);
        }
        if (a >= 0 && b >= 0) {
          M(a, b) = el_sub(M(a, b), e->value);
          M(b, a) = el_sub(M(b, a), e->value);
        }
        break;
      case EL_CURRENT_SOURCE:
        /* value leaves node a and enters node b */
        if (a >= 0) {
          M(a, n) = el_sub(M(a, n), e->value);
        }
        if (b >= 0) {
          M(b, n) = el_add(M(b, n), e->value);
        }
        break;
      case EL_VOLTAGE_SOURCE: {
        const int r = source_row++;
        /* KCL: the source current j (a -> b) leaves node a and enters node b */
        if (a >= 0) {
          M(a, r).re += 1.0;
          M(r, a).re += 1.0;
        }
        if (b >= 0) {
          M(b, r).re -= 1.0;
          M(r, b).re -= 1.0;
        }
        M(r, n) = e->value;
        break;
      }
    }
  }

  /* Gaussian elimination with partial pivoting. */
  bool ok = true;
  for (int col = 0; col < n && ok; col++) {
    int pivot = col;
    double best = el_abs2(M(col, col));
    for (int r = col + 1; r < n; r++) {
      const double magnitude = el_abs2(M(r, col));
      if (magnitude > best) {
        best = magnitude;
        pivot = r;
      }
    }
    if (best < 1e-36) {
      ok = false;
      break;
    }
    if (pivot != col) {
      for (int c = col; c <= n; c++) {
        const ElPhasor t = M(col, c);
        M(col, c) = M(pivot, c);
        M(pivot, c) = t;
      }
    }
    const ElPhasor diagonal = M(col, col);
    for (int r = col + 1; r < n; r++) {
      const ElPhasor below = M(r, col);
      if (below.re == 0.0 && below.im == 0.0) {
        continue;
      }
      const ElPhasor factor = el_div(below, diagonal);
      for (int c = col; c <= n; c++) {
        M(r, c) = el_sub(M(r, c), el_mul(factor, M(col, c)));
      }
    }
  }
  if (ok) {
    for (int r = n - 1; r >= 0; r--) {
      ElPhasor sum = M(r, n);
      for (int c = r + 1; c < n; c++) {
        sum = el_sub(sum, el_mul(M(r, c), M(c, n))); /* M(c, n) holds the solution of row c */
      }
      M(r, n) = el_div(sum, M(r, r));
    }
    for (int k = 0; k < nodes; k++) {
      circuit->voltages[k + 1] = M(k, n);
    }
    source_row = nodes;
    for (size_t i = 0; i < circuit->count; i++) {
      if (circuit->elements[i].kind == EL_VOLTAGE_SOURCE) {
        circuit->source_currents[i] = M(source_row, n);
        source_row++;
      }
    }
    circuit->solved = true;
  }
#undef M
  return ok;
}

ElPhasor el_circuit_voltage(const ElCircuit* circuit, int node) {
  if (!circuit->solved || node <= 0 || node >= circuit->nodes) {
    return el_phasor(0.0, 0.0);
  }
  return circuit->voltages[node];
}

ElPhasor el_circuit_current(const ElCircuit* circuit, int element) {
  if (!circuit->solved || element < 0 || (size_t)element >= circuit->count) {
    return el_phasor(0.0, 0.0);
  }
  const ElElement* e = &circuit->elements[element];
  switch (e->kind) {
    case EL_ADMITTANCE:
      return el_mul(e->value, el_sub(el_circuit_voltage(circuit, e->a), el_circuit_voltage(circuit, e->b)));
    case EL_CURRENT_SOURCE:
      return e->value;
    case EL_VOLTAGE_SOURCE:
      return circuit->source_currents[element];
  }
  return el_phasor(0.0, 0.0);
}
