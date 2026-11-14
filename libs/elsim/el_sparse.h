#ifndef IOTWIN_EL_SPARSE_H
#define IOTWIN_EL_SPARSE_H

/**
 * @file el_sparse.h
 * @brief Sparse LDL^T factorization of complex symmetric matrices (nodal admittance matrices).
 *
 * The nodal admittance matrix Y of a passive network (admittances between
 * nodes, gmin to earth) is complex symmetric (Y = Y^T) and its Hermitian part
 * - the conductance matrix - is positive definite. Such a matrix has an
 * LDL^T factorization for every symmetric ordering without pivoting, so the
 * ordering can be chosen for sparsity alone:
 *
 *  1. el_sparse_analyze(): minimum-degree ordering and the pattern of L
 *     (once per network topology),
 *  2. el_sparse_factor(): numeric factorization (when values change),
 *  3. el_sparse_solve(): forward/back substitution (every solution).
 *
 * The effort grows with the fill of L, not with n^3 as for dense elimination.
 * Pivots whose real part is not positive (only possible through rounding in
 * floating parts of the network, which are held by gmin alone) are replaced
 * by a small positive value and counted.
 */

#include <stdbool.h>
#include <stddef.h>

#include "el_circuit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int n;            /**< unknowns */
  int* perm;        /**< perm[k] = unknown eliminated in step k */
  int* iperm;       /**< iperm[i] = step in which unknown i is eliminated */
  int* col_start;   /**< n + 1: strictly lower L, column-wise, permuted numbering */
  int* row_index;   /**< rows of L, ascending per column */
  ElPhasor* values; /**< L (input: strictly lower part of the permuted matrix) */
  ElPhasor* diag;   /**< D (input: diagonal of the permuted matrix) */
  int* row_start;   /**< n + 1: for each row j the columns k < j with L(j,k) != 0 */
  int* row_entry;   /**< index of L(j,k) in values */
  int* row_col;     /**< column k */
  int* pair_pos;    /**< per input pair: index of its entry in values */
  size_t pairs;
  ElPhasor* work;   /**< n */
  size_t perturbed; /**< pivots replaced in the last factorization */
} ElSparse;

/**
 * @brief Orders the unknowns and computes the pattern of L.
 * @param n      number of unknowns (0-based)
 * @param pair_a, pair_b  off-diagonal pattern: unique pairs with a < b
 * @return NULL if memory runs out.
 */
ElSparse* el_sparse_analyze(int n, const int* pair_a, const int* pair_b, size_t pairs);
void el_sparse_free(ElSparse* sparse);

/** @brief Clears the values before a new matrix is entered. */
void el_sparse_clear(ElSparse* sparse);
/** @brief Adds to the diagonal entry of unknown i. */
void el_sparse_add_diagonal(ElSparse* sparse, int i, ElPhasor value);
/** @brief Adds to the off-diagonal entry (both triangles) of input pair p. */
void el_sparse_add_pair(ElSparse* sparse, size_t p, ElPhasor value);

/**
 * @brief Factorizes the entered matrix in place.
 * @param min_pivot positive real value that replaces non-positive pivots (e.g. gmin)
 * @return false if a value is not finite.
 */
bool el_sparse_factor(ElSparse* sparse, double min_pivot);

/** @brief Solves Y x = b; @p x holds b on entry (original numbering). */
void el_sparse_solve(ElSparse* sparse, ElPhasor* x);

/** @brief Non-zeros of L (fill included). */
size_t el_sparse_nonzeros(const ElSparse* sparse);

#ifdef __cplusplus
}
#endif

#endif /* IOTWIN_EL_SPARSE_H */
