#include "el_circuit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "el_sparse.h"

#define EL_PI 3.14159265358979323846

/* ------------------------------------------------------------------------- */
/* Cache of sparse orderings and factorizations                              */
/* ------------------------------------------------------------------------- */

/*
 * A few topologies are in use at the same time (the network itself, a dimmer
 * measuring its surroundings, a measurement), so several analyses are kept.
 * An entry is identified by its admittance elements (nodes in order); it
 * keeps the values of its last factorization to skip refactorizing.
 */
#define SPARSE_CACHE_ENTRIES 4

typedef struct {
  size_t count;     /* admittance elements */
  int* nodes;       /* a, b per admittance element */
  int* pair;        /* pair index per admittance element, -1 = no off-diagonal entry */
  ElPhasor* values; /* values of the last factorization */
  double gmin;
  bool factored;
  ElSparse* sparse;
  unsigned long used;
} SparseEntry;

typedef struct {
  SparseEntry entries[SPARSE_CACHE_ENTRIES];
  unsigned long clock;
  ElPhasor* rhs;
  size_t rhs_capacity;
} SparseCache;

typedef struct {
  int lo;
  int hi;
  int element; /* index among the admittance elements */
} PairKey;

static void entry_free(SparseEntry* e) {
  free(e->nodes);
  free(e->pair);
  free(e->values);
  el_sparse_free(e->sparse);
  memset(e, 0, sizeof(*e));
}

static void cache_free(ElCircuit* circuit) {
  SparseCache* cache = (SparseCache*)circuit->sparse_cache;
  if (cache == NULL) {
    return;
  }
  for (int i = 0; i < SPARSE_CACHE_ENTRIES; i++) {
    entry_free(&cache->entries[i]);
  }
  free(cache->rhs);
  free(cache);
  circuit->sparse_cache = NULL;
}

static int compare_pair(const void* a, const void* b) {
  const PairKey* x = (const PairKey*)a;
  const PairKey* y = (const PairKey*)b;
  if (x->lo != y->lo) {
    return (x->lo > y->lo) - (x->lo < y->lo);
  }
  return (x->hi > y->hi) - (x->hi < y->hi);
}

static bool same_topology(const SparseEntry* e, const ElCircuit* circuit, size_t admittances) {
  if (e->sparse == NULL || e->count != admittances) {
    return false;
  }
  size_t j = 0;
  for (size_t i = 0; i < circuit->count; i++) {
    const ElElement* element = &circuit->elements[i];
    if (element->kind != EL_ADMITTANCE) {
      continue;
    }
    if (e->nodes[2 * j] != element->a || e->nodes[2 * j + 1] != element->b) {
      return false;
    }
    j++;
  }
  return true;
}

/* New ordering for the topology of the circuit (allocates; only on topology changes). */
static bool entry_build(SparseEntry* e, const ElCircuit* circuit, size_t admittances) {
  entry_free(e);
  const int n = circuit->nodes - 1;
  const size_t count = admittances ? admittances : 1;
  e->nodes = (int*)malloc(2 * count * sizeof(int));
  e->pair = (int*)malloc(count * sizeof(int));
  e->values = (ElPhasor*)malloc(count * sizeof(ElPhasor));
  PairKey* keys = (PairKey*)malloc(count * sizeof(PairKey));
  int* pair_a = (int*)malloc(count * sizeof(int));
  int* pair_b = (int*)malloc(count * sizeof(int));
  bool ok = e->nodes && e->pair && e->values && keys && pair_a && pair_b;
  size_t key_count = 0;
  size_t j = 0;
  for (size_t i = 0; ok && i < circuit->count; i++) {
    const ElElement* element = &circuit->elements[i];
    if (element->kind != EL_ADMITTANCE) {
      continue;
    }
    e->nodes[2 * j] = element->a;
    e->nodes[2 * j + 1] = element->b;
    e->pair[j] = -1;
    if (element->a > 0 && element->b > 0 && element->a != element->b) {
      const int a = element->a - 1;
      const int b = element->b - 1;
      keys[key_count].lo = a < b ? a : b;
      keys[key_count].hi = a < b ? b : a;
      keys[key_count].element = (int)j;
      key_count++;
    }
    j++;
  }
  size_t pairs = 0;
  if (ok) {
    qsort(keys, key_count, sizeof(PairKey), compare_pair);
    for (size_t k = 0; k < key_count; k++) {
      if (pairs == 0 || keys[k].lo != pair_a[pairs - 1] || keys[k].hi != pair_b[pairs - 1]) {
        pair_a[pairs] = keys[k].lo;
        pair_b[pairs] = keys[k].hi;
        pairs++;
      }
      e->pair[keys[k].element] = (int)pairs - 1;
    }
    e->sparse = el_sparse_analyze(n, pair_a, pair_b, pairs);
    ok = e->sparse != NULL;
  }
  free(keys);
  free(pair_a);
  free(pair_b);
  if (!ok) {
    entry_free(e);
    return false;
  }
  e->count = admittances;
  return true;
}

static bool values_unchanged(const SparseEntry* e, const ElCircuit* circuit, double gmin) {
  if (!e->factored || e->gmin != gmin) {
    return false;
  }
  size_t j = 0;
  for (size_t i = 0; i < circuit->count; i++) {
    const ElElement* element = &circuit->elements[i];
    if (element->kind != EL_ADMITTANCE) {
      continue;
    }
    if (e->values[j].re != element->value.re || e->values[j].im != element->value.im) {
      return false;
    }
    j++;
  }
  return true;
}

static bool entry_factor(SparseEntry* e, ElCircuit* circuit, double gmin) {
  ElSparse* s = e->sparse;
  el_sparse_clear(s);
  for (int k = 0; k < s->n; k++) {
    el_sparse_add_diagonal(s, k, el_phasor(gmin, 0.0));
  }
  size_t j = 0;
  for (size_t i = 0; i < circuit->count; i++) {
    const ElElement* element = &circuit->elements[i];
    if (element->kind != EL_ADMITTANCE) {
      continue;
    }
    const ElPhasor y = element->value;
    e->values[j] = y;
    j++;
    if (element->a == element->b) {
      continue; /* from a node to itself: no effect */
    }
    if (element->a > 0) {
      el_sparse_add_diagonal(s, element->a - 1, y);
    }
    if (element->b > 0) {
      el_sparse_add_diagonal(s, element->b - 1, y);
    }
    if (e->pair[j - 1] >= 0) {
      el_sparse_add_pair(s, (size_t)e->pair[j - 1], el_scale(y, -1.0));
    }
  }
  e->gmin = gmin;
  e->factored = el_sparse_factor(s, gmin > 0.0 ? gmin : 1e-12);
  circuit->factorizations++;
  circuit->perturbed_pivots = s->perturbed;
  return e->factored;
}

/* Nodal analysis with the sparse solver; false if it cannot be used (the caller falls back to dense). */
static bool solve_sparse(ElCircuit* circuit, double gmin) {
  const int n = circuit->nodes - 1;
  SparseCache* cache = (SparseCache*)circuit->sparse_cache;
  if (cache == NULL) {
    cache = (SparseCache*)calloc(1, sizeof(SparseCache));
    if (cache == NULL) {
      return false;
    }
    circuit->sparse_cache = cache;
  }
  size_t admittances = 0;
  for (size_t i = 0; i < circuit->count; i++) {
    admittances += circuit->elements[i].kind == EL_ADMITTANCE;
  }

  SparseEntry* e = NULL;
  SparseEntry* oldest = &cache->entries[0];
  for (int i = 0; i < SPARSE_CACHE_ENTRIES; i++) {
    SparseEntry* candidate = &cache->entries[i];
    if (same_topology(candidate, circuit, admittances)) {
      e = candidate;
      break;
    }
    if (candidate->used < oldest->used) {
      oldest = candidate;
    }
  }
  if (e == NULL) {
    e = oldest;
    if (!entry_build(e, circuit, admittances)) {
      return false;
    }
    circuit->analyses++;
  }
  e->used = ++cache->clock;
  if (!values_unchanged(e, circuit, gmin) && !entry_factor(e, circuit, gmin)) {
    return false;
  }

  const size_t size = (size_t)(n > 0 ? n : 1);
  if (size > cache->rhs_capacity || cache->rhs == NULL) {
    ElPhasor* grown = (ElPhasor*)realloc(cache->rhs, size * sizeof(ElPhasor));
    if (grown == NULL) {
      return false;
    }
    cache->rhs = grown;
    cache->rhs_capacity = size;
  }
  ElPhasor* x = cache->rhs;
  memset(x, 0, size * sizeof(ElPhasor));
  for (size_t i = 0; i < circuit->count; i++) {
    const ElElement* element = &circuit->elements[i];
    if (element->kind == EL_CURRENT_SOURCE) {
      /* value leaves node a and enters node b */
      if (element->a > 0) {
        x[element->a - 1] = el_sub(x[element->a - 1], element->value);
      }
      if (element->b > 0) {
        x[element->b - 1] = el_add(x[element->b - 1], element->value);
      }
    }
  }
  el_sparse_solve(e->sparse, x);
  for (int k = 0; k < n; k++) {
    circuit->voltages[k + 1] = x[k];
  }
  circuit->fill = el_sparse_nonzeros(e->sparse);
  circuit->sparse_solutions++;
  return true;
}

void el_circuit_init(ElCircuit* circuit, int nodes) {
  memset(circuit, 0, sizeof(*circuit));
  circuit->nodes = nodes > 1 ? nodes : 1;
}

void el_circuit_free(ElCircuit* circuit) {
  cache_free(circuit);
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
  if (sources == 0 && circuit->solver == EL_SOLVER_AUTO && solve_sparse(circuit, gmin)) {
    circuit->solved = true;
    return true;
  }
  circuit->dense_solutions++;

  /* Dense modified nodal analysis. Augmented matrix n x (n + 1), row-major. */
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
