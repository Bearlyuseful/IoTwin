#include "el_circuit.h"

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void el_circuit_init(ElCircuit* circuit, int nodes) {
  memset(circuit, 0, sizeof(*circuit));
  circuit->nodes = nodes > 1 ? nodes : 1;
}

void el_circuit_free(ElCircuit* circuit) {
  free(circuit->elements);
  free(circuit->voltages);
  free(circuit->source_currents);
  memset(circuit, 0, sizeof(*circuit));
}

void el_circuit_clear(ElCircuit* circuit) {
  circuit->count = 0;
  circuit->solved = false;
}

ElPhasor el_polar(double magnitude, double degrees) {
  const double radians = degrees * M_PI / 180.0;
  return magnitude * cos(radians) + I * magnitude * sin(radians);
}

ElPhasor el_phasor(double real, double imaginary) { return real + I * imaginary; }
double el_abs(ElPhasor value) { return cabs(value); }
double el_real(ElPhasor value) { return creal(value); }
double el_imag(ElPhasor value) { return cimag(value); }
double el_angle_deg(ElPhasor value) { return carg(value) * 180.0 / M_PI; }

static int add(ElCircuit* circuit, ElElementKind kind, int a, int b, ElPhasor value) {
  if (a < 0 || b < 0 || a >= circuit->nodes || b >= circuit->nodes) {
    return -1;
  }
  if (circuit->count == circuit->capacity) {
    const size_t capacity = circuit->capacity ? circuit->capacity * 2 : 64;
    ElElement* grown = realloc(circuit->elements, capacity * sizeof(ElElement));
    if (grown == NULL) {
      return -1;
    }
    circuit->elements = grown;
    circuit->capacity = capacity;
  }
  circuit->elements[circuit->count] = (ElElement){kind, a, b, value};
  circuit->solved = false;
  return (int)circuit->count++;
}

int el_circuit_add_admittance(ElCircuit* circuit, int a, int b, ElPhasor admittance) {
  return add(circuit, EL_ADMITTANCE, a, b, admittance);
}

int el_circuit_add_impedance(ElCircuit* circuit, int a, int b, ElPhasor impedance) {
  /* A zero impedance would be infinite admittance: clamp to 1 micro-ohm. */
  if (cabs(impedance) < 1e-6) {
    impedance = 1e-6;
  }
  return add(circuit, EL_ADMITTANCE, a, b, 1.0 / impedance);
}

int el_circuit_add_voltage_source(ElCircuit* circuit, int a, int b, ElPhasor voltage) {
  return add(circuit, EL_VOLTAGE_SOURCE, a, b, voltage);
}

int el_circuit_add_current_source(ElCircuit* circuit, int a, int b, ElPhasor current) {
  return add(circuit, EL_CURRENT_SOURCE, a, b, current);
}

bool el_circuit_solve(ElCircuit* circuit, double gmin) {
  circuit->solved = false;
  const int nodes = circuit->nodes - 1; /* unknown node voltages (without earth) */
  int sources = 0;
  for (size_t i = 0; i < circuit->count; i++) {
    sources += circuit->elements[i].kind == EL_VOLTAGE_SOURCE;
  }
  const int n = nodes + sources;

  free(circuit->voltages);
  free(circuit->source_currents);
  circuit->voltages = calloc((size_t)circuit->nodes, sizeof(ElPhasor));
  circuit->source_currents = calloc(circuit->count ? circuit->count : 1, sizeof(ElPhasor));
  if (circuit->voltages == NULL || circuit->source_currents == NULL) {
    return false;
  }
  if (n == 0) {
    circuit->solved = true;
    return true;
  }

  /* Augmented matrix n x (n + 1), row-major. */
  ElPhasor* m = calloc((size_t)n * (size_t)(n + 1), sizeof(ElPhasor));
  if (m == NULL) {
    return false;
  }
#define M(r, c) m[(size_t)(r) * (size_t)(n + 1) + (size_t)(c)]
  for (int k = 0; k < nodes; k++) {
    M(k, k) += gmin;
  }
  int source_row = nodes;
  for (size_t i = 0; i < circuit->count; i++) {
    const ElElement* e = &circuit->elements[i];
    const int a = e->a - 1; /* -1 = earth */
    const int b = e->b - 1;
    switch (e->kind) {
      case EL_ADMITTANCE:
        if (a >= 0) {
          M(a, a) += e->value;
        }
        if (b >= 0) {
          M(b, b) += e->value;
        }
        if (a >= 0 && b >= 0) {
          M(a, b) -= e->value;
          M(b, a) -= e->value;
        }
        break;
      case EL_CURRENT_SOURCE:
        /* value leaves node a and enters node b */
        if (a >= 0) {
          M(a, n) -= e->value;
        }
        if (b >= 0) {
          M(b, n) += e->value;
        }
        break;
      case EL_VOLTAGE_SOURCE: {
        const int r = source_row++;
        /* KCL: the source current j (a -> b) leaves node a and enters node b */
        if (a >= 0) {
          M(a, r) += 1.0;
          M(r, a) += 1.0;
        }
        if (b >= 0) {
          M(b, r) -= 1.0;
          M(r, b) -= 1.0;
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
    double best = cabs(M(col, col));
    for (int r = col + 1; r < n; r++) {
      const double magnitude = cabs(M(r, col));
      if (magnitude > best) {
        best = magnitude;
        pivot = r;
      }
    }
    if (best < 1e-18) {
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
    for (int r = col + 1; r < n; r++) {
      const ElPhasor factor = M(r, col) / M(col, col);
      if (factor == 0) {
        continue;
      }
      for (int c = col; c <= n; c++) {
        M(r, c) -= factor * M(col, c);
      }
    }
  }
  if (ok) {
    for (int r = n - 1; r >= 0; r--) {
      ElPhasor sum = M(r, n);
      for (int c = r + 1; c < n; c++) {
        sum -= M(r, c) * M(c, n); /* M(c, n) holds the solution of row c */
      }
      M(r, n) = sum / M(r, r);
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
  free(m);
  return ok;
}

ElPhasor el_circuit_voltage(const ElCircuit* circuit, int node) {
  if (!circuit->solved || node <= 0 || node >= circuit->nodes) {
    return 0;
  }
  return circuit->voltages[node];
}

ElPhasor el_circuit_current(const ElCircuit* circuit, int element) {
  if (!circuit->solved || element < 0 || (size_t)element >= circuit->count) {
    return 0;
  }
  const ElElement* e = &circuit->elements[element];
  switch (e->kind) {
    case EL_ADMITTANCE:
      return e->value * (el_circuit_voltage(circuit, e->a) - el_circuit_voltage(circuit, e->b));
    case EL_CURRENT_SOURCE:
      return e->value;
    case EL_VOLTAGE_SOURCE:
      return circuit->source_currents[element];
  }
  return 0;
}
