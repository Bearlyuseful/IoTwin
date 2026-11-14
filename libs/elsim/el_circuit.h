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
 * pivoting - networks of a house have a few hundred nodes at most.
 */

#include <stdbool.h>
#include <stddef.h>

/* C99 complex; in C++ (tests) the same type through the GCC keyword _Complex. */
typedef double _Complex ElPhasor;

#ifdef __cplusplus
extern "C" {
#endif

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
  bool solved;
} ElCircuit;

#define EL_GMIN 1e-9 /**< Siemens from every node to earth (1 GOhm). */

void el_circuit_init(ElCircuit* circuit, int nodes);
void el_circuit_free(ElCircuit* circuit);

/** @brief Removes all elements, keeps the node count. */
void el_circuit_clear(ElCircuit* circuit);

/** @return Element index, -1 if out of memory or a node is out of range. */
int el_circuit_add_admittance(ElCircuit* circuit, int a, int b, ElPhasor admittance);
int el_circuit_add_impedance(ElCircuit* circuit, int a, int b, ElPhasor impedance);
int el_circuit_add_voltage_source(ElCircuit* circuit, int a, int b, ElPhasor voltage);
int el_circuit_add_current_source(ElCircuit* circuit, int a, int b, ElPhasor current);

/** @brief Solves the network. @return false if it is singular even with gmin. */
bool el_circuit_solve(ElCircuit* circuit, double gmin);

ElPhasor el_circuit_voltage(const ElCircuit* circuit, int node);

/** @brief Current through an element from a to b. */
ElPhasor el_circuit_current(const ElCircuit* circuit, int element);

/** @brief Phasor from RMS magnitude and angle in degrees. */
ElPhasor el_polar(double magnitude, double degrees);

/** @name Phasor helpers (usable from C++) */
/**@{*/
ElPhasor el_phasor(double real, double imaginary);
double el_abs(ElPhasor value);
double el_real(ElPhasor value);
double el_imag(ElPhasor value);
double el_angle_deg(ElPhasor value);
/**@}*/

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_EL_CIRCUIT_H */
