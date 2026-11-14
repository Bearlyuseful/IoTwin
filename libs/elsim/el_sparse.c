#include "el_sparse.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Small growable int lists for the elimination graph                         */
/* ------------------------------------------------------------------------- */

typedef struct {
  int* items;
  int count;
  int capacity;
} IntList;

static bool list_push(IntList* list, int value) {
  if (list->count == list->capacity) {
    const int capacity = list->capacity ? list->capacity * 2 : 4;
    int* grown = (int*)realloc(list->items, (size_t)capacity * sizeof(int));
    if (grown == NULL) {
      return false;
    }
    list->items = grown;
    list->capacity = capacity;
  }
  list->items[list->count++] = value;
  return true;
}

static void list_remove(IntList* list, int value) {
  for (int i = 0; i < list->count; i++) {
    if (list->items[i] == value) {
      list->items[i] = list->items[--list->count];
      return;
    }
  }
}

/* Binary min-heap of (degree, node) with lazy deletion: stale items are skipped when popped. */
typedef struct {
  int degree;
  int node;
} HeapItem;

typedef struct {
  HeapItem* items;
  size_t count;
  size_t capacity;
} Heap;

static bool heap_less(HeapItem a, HeapItem b) { return a.degree < b.degree || (a.degree == b.degree && a.node < b.node); }

static bool heap_push(Heap* h, int degree, int node) {
  if (h->count == h->capacity) {
    const size_t capacity = h->capacity ? h->capacity * 2 : 64;
    HeapItem* grown = (HeapItem*)realloc(h->items, capacity * sizeof(HeapItem));
    if (grown == NULL) {
      return false;
    }
    h->items = grown;
    h->capacity = capacity;
  }
  size_t i = h->count++;
  const HeapItem item = {degree, node};
  while (i > 0 && heap_less(item, h->items[(i - 1) / 2])) {
    h->items[i] = h->items[(i - 1) / 2];
    i = (i - 1) / 2;
  }
  h->items[i] = item;
  return true;
}

static HeapItem heap_pop(Heap* h) {
  const HeapItem top = h->items[0];
  const HeapItem last = h->items[--h->count];
  size_t i = 0;
  for (;;) {
    size_t child = 2 * i + 1;
    if (child >= h->count) {
      break;
    }
    if (child + 1 < h->count && heap_less(h->items[child + 1], h->items[child])) {
      child++;
    }
    if (!heap_less(h->items[child], last)) {
      break;
    }
    h->items[i] = h->items[child];
    i = child;
  }
  if (h->count > 0) {
    h->items[i] = last;
  }
  return top;
}

static int compare_int(const void* a, const void* b) {
  const int x = *(const int*)a;
  const int y = *(const int*)b;
  return (x > y) - (x < y);
}

void el_sparse_free(ElSparse* s) {
  if (s == NULL) {
    return;
  }
  free(s->perm);
  free(s->iperm);
  free(s->col_start);
  free(s->row_index);
  free(s->values);
  free(s->diag);
  free(s->row_start);
  free(s->row_entry);
  free(s->row_col);
  free(s->pair_pos);
  free(s->work);
  free(s);
}

/*
 * Minimum-degree ordering on the explicit elimination graph: eliminate the
 * node with the fewest neighbours, connect its neighbours to each other (the
 * fill) and record them as the pattern of its column of L. Networks of a
 * house are close to trees, so the fill stays small. The node with the
 * smallest degree comes from a heap (ties: lowest index, so the ordering is
 * reproducible); done once per topology.
 */
ElSparse* el_sparse_analyze(int n, const int* pair_a, const int* pair_b, size_t pairs) {
  ElSparse* s = (ElSparse*)calloc(1, sizeof(ElSparse));
  if (s == NULL) {
    return NULL;
  }
  s->n = n;
  s->pairs = pairs;
  const size_t un = (size_t)(n > 0 ? n : 1);
  IntList* adjacency = (IntList*)calloc(un, sizeof(IntList));
  IntList* columns = (IntList*)calloc(un, sizeof(IntList)); /* original indices */
  unsigned char* eliminated = (unsigned char*)calloc(un, 1);
  int* mark = (int*)malloc(un * sizeof(int));
  s->perm = (int*)malloc(un * sizeof(int));
  s->iperm = (int*)malloc(un * sizeof(int));
  bool ok = adjacency && columns && eliminated && mark && s->perm && s->iperm;

  for (size_t p = 0; ok && p < pairs; p++) {
    ok = list_push(&adjacency[pair_a[p]], pair_b[p]) && list_push(&adjacency[pair_b[p]], pair_a[p]);
  }
  Heap heap = {NULL, 0, 0};
  for (int i = 0; ok && i < n; i++) {
    mark[i] = -1;
    ok = heap_push(&heap, adjacency[i].count, i);
  }
  for (int k = 0; ok && k < n; k++) {
    int v = -1;
    while (v < 0 && heap.count > 0) {
      const HeapItem item = heap_pop(&heap);
      if (!eliminated[item.node] && item.degree == adjacency[item.node].count) {
        v = item.node;
      }
    }
    if (v < 0) {
      ok = false;
      break;
    }
    eliminated[v] = 1;
    s->perm[k] = v;
    s->iperm[v] = k;
    const IntList* neighbours = &adjacency[v];
    for (int a = 0; ok && a < neighbours->count; a++) {
      const int u = neighbours->items[a];
      ok = list_push(&columns[v], u);
      list_remove(&adjacency[u], v);
      /* u gets every other neighbour of v (fill) */
      for (int b = 0; b < adjacency[u].count; b++) {
        mark[adjacency[u].items[b]] = u;
      }
      for (int b = 0; ok && b < neighbours->count; b++) {
        const int w = neighbours->items[b];
        if (w != u && mark[w] != u) {
          mark[w] = u;
          ok = list_push(&adjacency[u], w);
        }
      }
      ok = ok && heap_push(&heap, adjacency[u].count, u); /* its degree changed */
    }
    free(adjacency[v].items);
    memset(&adjacency[v], 0, sizeof(IntList));
  }
  free(heap.items);

  /* Column pattern in permuted numbering, rows ascending. */
  size_t nonzeros = 0;
  for (int i = 0; ok && i < n; i++) {
    nonzeros += (size_t)columns[i].count;
  }
  const size_t unz = nonzeros ? nonzeros : 1;
  if (ok) {
    s->col_start = (int*)malloc((un + 1) * sizeof(int));
    s->row_index = (int*)malloc(unz * sizeof(int));
    s->values = (ElPhasor*)calloc(unz, sizeof(ElPhasor));
    s->diag = (ElPhasor*)calloc(un, sizeof(ElPhasor));
    s->work = (ElPhasor*)calloc(un, sizeof(ElPhasor));
    s->row_start = (int*)calloc(un + 1, sizeof(int));
    s->row_entry = (int*)malloc(unz * sizeof(int));
    s->row_col = (int*)malloc(unz * sizeof(int));
    s->pair_pos = (int*)malloc((pairs ? pairs : 1) * sizeof(int));
    ok = s->col_start && s->row_index && s->values && s->diag && s->work && s->row_start && s->row_entry &&
         s->row_col && s->pair_pos;
  }
  if (ok) {
    int position = 0;
    for (int k = 0; k < n; k++) {
      const IntList* column = &columns[s->perm[k]];
      s->col_start[k] = position;
      for (int e = 0; e < column->count; e++) {
        s->row_index[position + e] = s->iperm[column->items[e]];
      }
      qsort(s->row_index + position, (size_t)column->count, sizeof(int), compare_int);
      position += column->count;
    }
    s->col_start[n] = position;

    /* Row lists: for row j the entries L(j,k), k < j (columns in ascending order). */
    for (int p = 0; p < position; p++) {
      s->row_start[s->row_index[p] + 1]++;
    }
    for (int j = 0; j < n; j++) {
      s->row_start[j + 1] += s->row_start[j];
    }
    for (int j = 0; j < n; j++) {
      mark[j] = s->row_start[j];
    }
    for (int k = 0; k < n; k++) {
      for (int p = s->col_start[k]; p < s->col_start[k + 1]; p++) {
        const int j = s->row_index[p];
        s->row_entry[mark[j]] = p;
        s->row_col[mark[j]] = k;
        mark[j]++;
      }
    }

    /* Where every input pair lands in L. */
    for (size_t p = 0; p < pairs; p++) {
      const int ia = s->iperm[pair_a[p]];
      const int ib = s->iperm[pair_b[p]];
      const int column = ia < ib ? ia : ib;
      const int row = ia < ib ? ib : ia;
      const int* first = s->row_index + s->col_start[column];
      const int* found = (const int*)bsearch(&row, first, (size_t)(s->col_start[column + 1] - s->col_start[column]),
                                             sizeof(int), compare_int);
      s->pair_pos[p] = found ? (int)(found - s->row_index) : -1; /* always found: A is part of L's pattern */
    }
  }

  for (int i = 0; adjacency && i < n; i++) {
    free(adjacency[i].items);
  }
  for (int i = 0; columns && i < n; i++) {
    free(columns[i].items);
  }
  free(adjacency);
  free(columns);
  free(eliminated);
  free(mark);
  if (!ok) {
    el_sparse_free(s);
    return NULL;
  }
  return s;
}

void el_sparse_clear(ElSparse* s) {
  const size_t nonzeros = el_sparse_nonzeros(s);
  memset(s->values, 0, (nonzeros ? nonzeros : 1) * sizeof(ElPhasor));
  memset(s->diag, 0, (size_t)(s->n > 0 ? s->n : 1) * sizeof(ElPhasor));
}

void el_sparse_add_diagonal(ElSparse* s, int i, ElPhasor value) {
  ElPhasor* d = &s->diag[s->iperm[i]];
  *d = el_add(*d, value);
}

void el_sparse_add_pair(ElSparse* s, size_t p, ElPhasor value) {
  if (p < s->pairs && s->pair_pos[p] >= 0) {
    ElPhasor* v = &s->values[s->pair_pos[p]];
    *v = el_add(*v, value);
  }
}

size_t el_sparse_nonzeros(const ElSparse* s) { return s->n > 0 ? (size_t)s->col_start[s->n] : 0; }

/*
 * Left-looking LDL^T in place: column j is gathered into the dense work
 * vector, updated with every earlier column k that has an entry in row j
 * (w -= L(:,k) L(j,k) D(k)), then divided by its pivot D(j). The filled
 * pattern guarantees that every updated row is part of column j.
 */
bool el_sparse_factor(ElSparse* s, double min_pivot) {
  ElPhasor* w = s->work;
  s->perturbed = 0;
  for (int j = 0; j < s->n; j++) {
    w[j] = s->diag[j];
    for (int p = s->col_start[j]; p < s->col_start[j + 1]; p++) {
      w[s->row_index[p]] = s->values[p];
    }
    for (int q = s->row_start[j]; q < s->row_start[j + 1]; q++) {
      const int k = s->row_col[q];
      const int entry = s->row_entry[q]; /* L(j,k); the rows after it in column k are > j */
      const ElPhasor factor = el_mul(s->values[entry], s->diag[k]);
      for (int p = entry; p < s->col_start[k + 1]; p++) {
        ElPhasor* target = &w[s->row_index[p]];
        *target = el_sub(*target, el_mul(s->values[p], factor));
      }
    }
    ElPhasor pivot = w[j];
    if (!isfinite(pivot.re) || !isfinite(pivot.im)) {
      return false;
    }
    if (!(pivot.re > 0.0)) {
      pivot = el_phasor(min_pivot, 0.0);
      s->perturbed++;
    }
    s->diag[j] = pivot;
    w[j] = el_phasor(0.0, 0.0);
    for (int p = s->col_start[j]; p < s->col_start[j + 1]; p++) {
      const int i = s->row_index[p];
      s->values[p] = el_div(w[i], pivot);
      w[i] = el_phasor(0.0, 0.0);
    }
  }
  return true;
}

void el_sparse_solve(ElSparse* s, ElPhasor* x) {
  ElPhasor* y = s->work;
  const int n = s->n;
  for (int k = 0; k < n; k++) {
    y[k] = x[s->perm[k]];
  }
  for (int k = 0; k < n; k++) { /* L z = b */
    const ElPhasor yk = y[k];
    if (yk.re == 0.0 && yk.im == 0.0) {
      continue;
    }
    for (int p = s->col_start[k]; p < s->col_start[k + 1]; p++) {
      ElPhasor* target = &y[s->row_index[p]];
      *target = el_sub(*target, el_mul(s->values[p], yk));
    }
  }
  for (int k = 0; k < n; k++) { /* D */
    y[k] = el_div(y[k], s->diag[k]);
  }
  for (int k = n - 1; k >= 0; k--) { /* L^T x = y */
    ElPhasor sum = y[k];
    for (int p = s->col_start[k]; p < s->col_start[k + 1]; p++) {
      sum = el_sub(sum, el_mul(s->values[p], y[s->row_index[p]]));
    }
    y[k] = sum;
  }
  for (int k = 0; k < n; k++) {
    x[s->perm[k]] = y[k];
    y[k] = el_phasor(0.0, 0.0); /* the work vector is zero between uses */
  }
}
