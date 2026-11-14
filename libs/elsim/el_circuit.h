#ifndef IOTWIN_EL_CIRCUIT_H
#define IOTWIN_EL_CIRCUIT_H

/**
 * @file el_circuit.h
 * @brief Linear AC network solver: modified nodal analysis with complex phasors.
 *
 * Node 0 is the reference (earth). Elements:
 *  - admittance between two nodes,
 *  - ideal voltage source: V(a) - V(b) = value,
 *  - ideal current source: value flows from node a to node b through the source.
 * The current of every element is reported in the direction a -> b.
 *
 * A small conductance gmin from every node to earth keeps floating parts of
 * the network (open breakers, unconnected terminals) solvable; it also models
 * a very high insulation resistance. Dense Gaussian elimination with partial
 * pivoting.
 *
 * Portable C99 without C99 complex numbers (MSVC has no complex arithmetic),
 * so the same sources compile with GCC, Clang and MSVC, also inside the
 * Unreal visualizer (ADR 0008). After the first solution of a network size the
 * solver does not allocate memory any more.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Complex phasor: RMS value with phase (real and imaginary part). */
typedef struct {
  double re;
  double im;
} ElPhasor;

#if defined(_MSC_VER) && !defined(__cplusplus)
#define EL_INLINE static __inline
#else
#define EL_INLINE static inline
#endif

/** @name Phasor arithmetic */
/**@{*/
EL_INLINE ElPhasor el_phasor(double re, double im) {
  ElPhasor z;
  z.re = re;
  z.im = im;
  return z;
}
EL_INLINE ElPhasor el_add(ElPhasor a, ElPhasor b) { return el_phasor(a.re + b.re, a.im + b.im); }
EL_INLINE ElPhasor el_sub(ElPhasor a, ElPhasor b) { return el_phasor(a.re - b.re, a.im - b.im); }
EL_INLINE ElPhasor el_mul(ElPhasor a, ElPhasor b) {
  return el_phasor(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}
EL_INLINE ElPhasor el_div(ElPhasor a, ElPhasor b) {
  const double d = b.re * b.re + b.im * b.im;
  return el_phasor((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
EL_INLINE ElPhasor el_scale(ElPhasor a, double factor) { return el_phasor(a.re * factor, a.im * factor); }
EL_INLINE ElPhasor el_conj(ElPhasor a) { return el_phasor(a.re, -a.im); }
EL_INLINE double el_real(ElPhasor a) { return a.re; }
EL_INLINE double el_imag(ElPhasor a) { return a.im; }
EL_INLINE double el_abs2(ElPhasor a) { return a.re * a.re + a.im * a.im; }
/**@}*/

/** @brief Magnitude |z|, overflow-safe. */
double el_abs(ElPhasor value);
/** @brief Phase angle in degrees. */
double el_angle_deg(ElPhasor value);
/** @brief Phasor from RMS magnitude and angle in degrees. */
ElPhasor el_polar(double magnitude, double degrees);

typedef enum { EL_ADMITTANCE, EL_VOLTAGE_SOURCE, EL_CURRENT_SOURCE } ElElementKind;

typedef struct {
  ElElementKind kind;
  int a;
  int b;
  ElPhasor value;
} ElElement;

typedef struct {
  int nodes;              /**< Including node 0 (earth). */
  ElElement* elements;
  size_t count;
  size_t capacity;
  ElPhasor* voltages;     /**< Result per node (voltages[0] = 0). */
  ElPhasor* source_currents; /**< Result per element (only voltage sources use it). */
  size_t result_capacity; /**< Size of source_currents. */
  ElPhasor* matrix;       /**< Work matrix, reused between solutions. */
  size_t matrix_capacity;
  bool solved;
} ElCircuit;

#define EL_GMIN 1e-9 /**< Siemens from every node to earth (1 GOhm). */

void el_circuit_init(ElCircuit* circuit, int nodes);
void el_circuit_free(ElCircuit* circuit);

/** @brief Removes all elements, keeps the node count and the allocated memory. */
void el_circuit_clear(ElCircuit* circuit);

/** @return Element index, -1 if out of memory or a node is out of range. */
int el_circuit_add_admittance(ElCircuit* circuit, int a, int b, ElPhasor admittance);
int el_circuit_add_impedance(ElCircuit* circuit, int a, int b, ElPhasor impedance);
/** @brief Resistive impedance (convenience for el_circuit_add_impedance). */
int el_circuit_add_resistance(ElCircuit* circuit, int a, int b, double ohm);
int el_circuit_add_voltage_source(ElCircuit* circuit, int a, int b, ElPhasor voltage);
int el_circuit_add_current_source(ElCircuit* circuit, int a, int b, ElPhasor current);

/** @brief Solves the network. @return false if it is singular even with gmin. */
bool el_circuit_solve(ElCircuit* circuit, double gmin);

ElPhasor el_circuit_voltage(const ElCircuit* circuit, int node);

/** @brief Current through an element from a to b. */
ElPhasor el_circuit_current(const ElCircuit* circuit, int element);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_EL_CIRCUIT_H */
