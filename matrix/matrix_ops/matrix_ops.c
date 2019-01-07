#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "matrix_ops.h"

#define ADD 1U
#define SUB 0U

#define ABS(x) (0 > (x) ? -(x) : (x))

#define MATRIX_INVERT_SCRATCHPAD_NUM 2
#define V 0
#define W 1

#define NERVOUS

struct _pivot {
	double pivot;
	unsigned pivot_idx;
};

struct _matrix * matrix_alloc(enum kmalloc_type type)
{
	/* to alloc a matrix use this, to free use matrix_free(matrix) */
	/* this just adjusts the dimensions, the elements pointer */
	/* is set in kmalloc and we rely on them not being changed */
	if (NxN != type && Nx1 != type) {
		fprintf(stderr, "no such matrix type "
				"to allocate in matrix_alloc\n");
		return 0;
	}
	struct _matrix * new = kmalloc(type, 0);
	if (!new) {
		fprintf(stderr, "no matrix to allocate in matrix_alloc\n");
		return 0;
	} else if (NxN == type) {
		new->dimensions[ROW] = N;
		new->dimensions[COLUMN] = N;
	} else if (Nx1 == type) {
		new->dimensions[ROW] = N;
		new->dimensions[COLUMN] = 1;
	}
	return new;
}

void matrix_free(struct _matrix * m)
{
#ifdef NERVOUS
	if (!m) {
		fprintf(stderr, "invalid matrix in matrix_free\n");
		return;
	}
#endif
	/* supporting NxN and Nx1 right now */
	unsigned nrows = m->dimensions[ROW];
	unsigned ncols = m->dimensions[COLUMN];
	if (N == nrows && N == ncols)
		kfree(m, NxN);
	else if ((1 == nrows && N == ncols) || (N == nrows && 1 == ncols))
		kfree(m, Nx1);
	else
		fprintf(stderr, "no such matrix type in matrix_free\n");
}

static unsigned
matrix_entry_offset(struct _matrix * m, struct _matrix_entry entry)
{
#ifdef NERVOUS
	if (!m) {
		fprintf(stderr, "invalid matrix in matrix_entry_offset\n");
		return 0;
	}
#endif
	return entry.row * m->dimensions[COLUMN] + entry.col;
}

double matrix_get_entry(struct _matrix * m, struct _matrix_entry entry)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_get_entry\n");
		return 0;
	}
#endif
	unsigned offset = matrix_entry_offset(m, entry);
	return *(m->elements + offset);
}

void
matrix_set_entry(struct _matrix * m,
		        struct _matrix_entry entry, double val)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_set_entry\n");
		return;
	}
#endif
	unsigned offset = matrix_entry_offset(m, entry);
	*(m->elements + offset) = val;
}

static struct _matrix_entry swap_matrix_entry(struct _matrix_entry entry)
{
	return (struct _matrix_entry) {
		.row = entry.col,
		.col = entry.row,
	};
}

static void
matrix_swap_entries(struct _matrix * m,
		    struct _matrix_entry entry1, struct _matrix_entry entry2)
{
	double val1 = matrix_get_entry(m, entry1);
	double val2 = matrix_get_entry(m, entry2);
	matrix_set_entry(m, entry1, val2);
	matrix_set_entry(m, entry2, val1);
}

#ifdef NERVOUS
static unsigned
matrix_mult_dimension_compatible(struct _matrix * prod,
				struct _matrix * a, struct _matrix * b)
{
	/* check dimensions for prod = a @ b */
	if (prod->dimensions[ROW] != a->dimensions[ROW])
		return 0;
	if (a->dimensions[COLUMN] != b->dimensions[ROW])
		return 0;
	if (prod->dimensions[COLUMN] != b->dimensions[COLUMN])
		return 0;
	return 1;
}
#endif

#ifdef NERVOUS
static unsigned
matrix_sum_dimension_compatible(struct _matrix * sum,
				struct _matrix * a, struct _matrix * b)
{
	unsigned dimension_ok = 0;
	for (unsigned i = ROW; COLUMN >= i; i++) {
		dimension_ok =
			sum->dimensions[i] == a->dimensions[i]
			&& a->dimensions[i] == b->dimensions[i];
		if (!dimension_ok)
			return 0;
	}
	return 1;
}
#endif

static void
matrix_scalar_mult_intern(struct _matrix * a, struct _matrix * b, double s)
{
	/* a_ij = s * b_ij, assumes a and b have same dimensions */
#ifdef NERVOUS
	/* might want to check carefully who called if error occurs ! */
	if (!a || !a->elements) {
		fprintf(stderr, "invalid matrix a in "
				"matrix_scalar_mult_intern\n");
		return;
	}
	if (!b || !b->elements) {
		fprintf(stderr, "invalid matrix b in "
				"matrix_scalar_mult_intern\n");
		return;
	}
	if (!matrix_sum_dimension_compatible(a, a, b)) {
		fprintf(stderr, "incompatible matrix "
				"dimensions in matrix_scalar_mult_intern\n");
		return;
	}
#endif

	double tmp = 0;
	unsigned nrows = a->dimensions[ROW];
	unsigned ncols = a->dimensions[COLUMN];
	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = 0; ncols > j; j++) {
			tmp = s * matrix_get_entry(b, ME(i, j));
			matrix_set_entry(a, ME(i, j), tmp);
		}
}

void matrix_scalar_mult(struct _matrix * m, double s)
{
	/* m_ij = s * m_ij */
	matrix_scalar_mult_intern(m, m, s);
}

void matrix_copy(struct _matrix * a, struct _matrix * b)
{
	/* a_ij = b_ij */
	matrix_scalar_mult_intern(a, b, 1);
}

void matrix_zero_up(struct _matrix * m)
{
	/* m_ij = 0 */
	matrix_scalar_mult_intern(m, m, 0);
}

void matrix_neg(struct _matrix * m)
{
	/* m_ij = - m_ij */
	matrix_scalar_mult_intern(m, m, -1);
}

void matrix_mult(struct _matrix * prod,
		 struct _matrix * a, struct _matrix * b)
{
#ifdef NERVOUS
	if (!prod || !prod->elements) {
		fprintf(stderr, "invalid matrix prod in matrix_mult\n");
		return;
	}
	if (!a || !a->elements) {
		fprintf(stderr, "invalid matrix a in matrix_mult\n");
		return;
	}
	if (!b || !b->elements) {
		fprintf(stderr, "invalid matrix b in matrix_mult\n");
		return;
	}
	if (!matrix_mult_dimension_compatible(prod, a, b)) {
		fprintf(stderr, "incompatible matrix "
				"dimensions in matrix_mult\n");
		return;
	}
#endif

	double acc = 0;
	unsigned nrows = prod->dimensions[ROW];
	unsigned ncols = prod->dimensions[COLUMN];
	unsigned k_run = a->dimensions[COLUMN];
	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = 0; ncols > j; j++) {
			acc = 0;
			for (unsigned k = 0; k_run > k; k++) {
				acc += matrix_get_entry(a, ME(i, k))
				       * matrix_get_entry(b, ME(k, j));
			}
			matrix_set_entry(prod, ME(i, j), acc);
		}
}

double matrix_scalar_prod(struct _matrix * a, struct _matrix * b)
{
#ifdef NERVOUS
	if (!a || !a->elements) {
		fprintf(stderr, "invalid matrix a in matrix_scalar_prod\n");
		return 0;
	}
	if (!b || !b->elements) {
		fprintf(stderr, "invalid matrix b in matrix_scalar_prod\n");
		return 0;
	}
#endif
	/* computes the sum of a_k * b_k over k as long as a and b */
	/* are of dimension Nx1 Nx1, 1xN 1xN, Nx1 1xN , 1xN, Nx1 */
	double acc = 0;
	unsigned a_row = a->dimensions[ROW];
	unsigned b_row = b->dimensions[ROW];
	unsigned a_col = a->dimensions[COLUMN];
	unsigned b_col = b->dimensions[COLUMN];
	if (1 < a_row && 1 == a_col
	    && 1 < b_row && 1 == b_col
	               && a_row == b_row) {
		for (unsigned k = 0; a_row > k; k++)
			acc += matrix_get_entry(a, ME(k, 0))
			       * matrix_get_entry(b, ME(k, 0));
	} else if (1 == a_row && 1 < a_col
		    && 1 == b_row && 1 < b_col
	                       && a_col == b_col) {
		for (unsigned k = 0; a_col > k; k++)
			acc += matrix_get_entry(a, ME(0, k))
			       * matrix_get_entry(b, ME(0, k));
	} else if (1 < a_row && 1 == a_col
		    && 1 == b_row && 1 < b_col
	                       && a_row == b_col) {
		for (unsigned k = 0; a_row > k; k++)
			acc += matrix_get_entry(a, ME(k, 0))
			       * matrix_get_entry(b, ME(0, k));
	} else if (1 == a_row && 1 < a_col
		    && 1 < b_row && 1 == b_col
	                       && a_col == b_row) {
		for (unsigned k = 0; a_col > k; k++)
			acc += matrix_get_entry(a, ME(0, k))
			       * matrix_get_entry(b, ME(k, 0));
	} else {
		fprintf(stderr, "invalid arguments in matrix_scalar_prod"
				"no combination of dimensions matches\n");
		return 0;
	}

	return acc;
}

static void
matrix_add_sub(struct _matrix * sum,
		struct _matrix * a, struct _matrix * b, unsigned add_sub)
{
#ifdef NERVOUS
	/* might want to check carefully who called if error occurs ! */
	if (!sum || !sum->elements) {
		fprintf(stderr, "invalid matrix sum in matrix_add_sub\n");
		return;
	}
	if (!a || !a->elements) {
		fprintf(stderr, "invalid matrix a in matrix_add_sub\n");
		return;
	}
	if (!b || !b->elements) {
		fprintf(stderr, "invalid matrix b in matrix_add_sub\n");
		return;
	}
	if (!matrix_sum_dimension_compatible(sum, a, b)) {
		fprintf(stderr, "incompatible matrix dimensions in "
				"matrix_%s\n", ADD == add_sub ? "add" : "sub");
		return;
	}
#endif

	unsigned nrows = sum->dimensions[ROW];
	unsigned ncols = sum->dimensions[COLUMN];
	double aa = 0;
	double bb = 0;
	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = 0; ncols > j; j++) {
			aa = matrix_get_entry(a, ME(i, j));
			bb = matrix_get_entry(b, ME(i, j));
			matrix_set_entry(sum, ME(i, j),
					ADD == add_sub ? aa + bb : aa - bb);
		}
}

void matrix_add(struct _matrix * sum,
		struct _matrix * a, struct _matrix * b)
{
	matrix_add_sub(sum, a, b, ADD);
}

void matrix_sub(struct _matrix * sum,
		struct _matrix * a, struct _matrix * b)
{
	matrix_add_sub(sum, a, b, SUB);
}

void matrix_trans(struct _matrix * m)
{
	unsigned nrows = m->dimensions[ROW];
	unsigned ncols = m->dimensions[COLUMN];

	/* swap dimensions */
	m->dimensions[ROW] = ncols;
	m->dimensions[COLUMN] = nrows;

	/* only swap entries around if both
	   dimensions are different from 1 */
	if (1U == nrows || 1U == ncols)
		return;

	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = i + 1; ncols > j; j++) {
			matrix_swap_entries(m, ME(i, j),
					swap_matrix_entry(ME(i, j)));
		}
}

static void
permutation_swap(unsigned * permutation, unsigned i, unsigned j)
{
#ifdef NERVOUS
	if (!permutation) {
		fprintf(stderr, "invalid permutation in permutation_swap");
		return;
	}
#endif

	double tmp = permutation[i];
	permutation[i] = permutation[j];
	permutation[j] = tmp;
}

static struct _pivot
matrix_lup_pivot(struct _matrix * m, unsigned k)
{
	double pivot = 0;
	unsigned pivot_idx = 0;

	double tmp = 0;
	unsigned nrows = m->dimensions[ROW];
	for (unsigned i = k; nrows > i; i++) {
		tmp = matrix_get_entry(m, ME(i, k));
		tmp = ABS(tmp);
		if(tmp > pivot) {
			pivot = tmp;
			pivot_idx = i;
		}
	}

	return (struct _pivot) {
		.pivot = pivot,
		.pivot_idx = pivot_idx,
	};
}

static void
matrix_row_permute(struct _matrix * m, unsigned row1, unsigned row2)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_row_permute\n");
		return;
	}
#endif

	unsigned ncols = m->dimensions[COLUMN];
	for (unsigned j = 0; ncols > j; j++)
		matrix_swap_entries(m, ME(row1, j), ME(row2, j));
}

static void matrix_lup_decompose(struct _matrix * m, unsigned * permutation)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_lup_decompose\n");
		return;
	}
	if (m->dimensions[ROW] != m->dimensions[COLUMN]) {
		fprintf(stderr, "invalid matrix "
				"in matrix_lup_decompose "
				"expecting square matrix\n");
		return;
	}
	if (!permutation) {
		fprintf(stderr, "invalid permutation in matrix_lup_decompose");
		return;
	}
#endif

	unsigned nrows = m->dimensions[ROW];
	for (unsigned k = 0; nrows > k + 1; k++) {

		/* get pivot */
		struct _pivot pivot = matrix_lup_pivot(m, k);
		if(!pivot.pivot) {
			fprintf(stderr, "singular matrix "
					"in matrix_lup_decompose\n");
			return;
		}

		/* remember permutation */
		permutation_swap(permutation, pivot.pivot_idx, k);
		/* permute according to pivot */
		matrix_row_permute(m, pivot.pivot_idx, k);

		/* solve for entries, one after another */
		for (unsigned i = k + 1; nrows > i; i++) {
			double tmp = matrix_get_entry(m, ME(i, k));
			tmp /= matrix_get_entry(m, ME(k, k));
			matrix_set_entry(m, ME(i, k), tmp);
			for (unsigned j = k + 1; nrows > j; j++) {
				tmp = matrix_get_entry(m, ME(i, j));
				tmp -= matrix_get_entry(m, ME(i, k))
				       * matrix_get_entry(m, ME(k, j));
				matrix_set_entry(m, ME(i, j), tmp);
			}
		}
	}
	return;
}

static void matrix_zero_row(struct _matrix * m, unsigned row)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_zero_row");
		return;
	}
#endif
	unsigned ncols = m->dimensions[COLUMN];
	for(unsigned j = 0; ncols > j; j++)
		matrix_set_entry(m, ME(row, j), 0);
}

void matrix_invert(struct _matrix * m)
{
	/* only supporting NxN right now (not MxM where M != N) */
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_invert");
		return;
	}
	if ((N != m->dimensions[ROW]) || (N != m->dimensions[COLUMN])) {
		fprintf(stderr, "invalid matrix "
				"in matrix_invert "
				"expecting NxN matrix\n");
		return;
	}
	if (m->dimensions[ROW] != m->dimensions[COLUMN]) {
		fprintf(stderr, "invalid matrix "
				"in matrix_invert "
				"expecting square matrix\n");
		return;
	}
#endif

	/* get temporary scratchpads */
	unsigned nrows = m->dimensions[ROW];
	double scratchpad[MATRIX_INVERT_SCRATCHPAD_NUM][N];
	unsigned permutation[N];
	for (unsigned k = 0; N > k; k++) {
		scratchpad[V][k] = 0;
		scratchpad[W][k] = 0;
		permutation[k] = k;
	}

	/* do the decomposition, m permuted will contian l and u afterwards */
	matrix_lup_decompose(m, permutation);

	/* get scratchpad matrix */
	struct _matrix * tmp = matrix_alloc(NxN);
	if (!tmp) {
		fprintf(stderr, "out of NxN matices in matrix_invert\n");
		return;
	}

	/* do the inversion */
	for (unsigned i = 0; nrows > i; i++) {
		matrix_zero_row(tmp, i);
		matrix_set_entry(tmp,
				ME(i, i) , 1);

		for (unsigned n = 0; nrows > n; n++) {
			double t = 0;
			for (unsigned k = 0; k + 1 <= n; k++)
				t += matrix_get_entry(m, ME(n, k))
					                * scratchpad[W][k];
			scratchpad[W][n] = matrix_get_entry(tmp,
			                         ME(i, permutation[n])) - t;
		}

		for (unsigned n = nrows - 1; 1 <= n + 1; n--) {
			double t = 0;
			for(unsigned k = n + 1; nrows > k; k++)
				t += matrix_get_entry(m, ME(n, k))
					                * scratchpad[V][k];
			scratchpad[V][n] = (scratchpad[W][n] - t)
				        / matrix_get_entry(m, ME(n, n));
		}

		for (unsigned j = 0; nrows > j; j++)
			matrix_set_entry(tmp, ME(i, j), scratchpad[V][j]);
	}

	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = 0; nrows > j; j++) {
			double t = matrix_get_entry(tmp, ME(j, i));
			matrix_set_entry(m, ME(i, j), t);
		}

	matrix_free(tmp);

	return;
}

double matrix_norm(struct _matrix * m)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_norm");
		return 0;
	}
	if (N != m->dimensions[ROW] && 1 != m->dimensions[COLUMN]) {
		fprintf(stderr, "invalid matrix in "
				"matrix_norm, only Nx1 supported");
		return 0;
	}
#endif

	/* only Nx1 supported so far */
	double acc = 0;
	double tmp = 0;
	for (unsigned k = 0; N > k; k++) {
		tmp = matrix_get_entry(m, ME(k, 0));
		tmp *= tmp;
		acc += tmp;
	}

	return sqrt(acc);
}

void matrix_print(struct _matrix * m)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_print");
		return;
	}
#endif

	unsigned nrows = m->dimensions[ROW];
	unsigned ncols = m->dimensions[COLUMN];
	for (unsigned i = 0; nrows > i; i++) {
		for (unsigned j = 0; ncols > j; j++)
			printf("%e ", matrix_get_entry(m, ME(i, j)));
		printf("\n");
	}
	printf("\n");
}

static double random_number(double min, double max)
{
	double r = rand();
	return min + r * (max - min) / (double)RAND_MAX;
}

void matrix_random(struct _matrix * m, double min, double max)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_random");
		return;
	}
#endif
	srand((unsigned)time(0));
	unsigned nrows = m->dimensions[ROW];
	unsigned ncols = m->dimensions[COLUMN];
	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = 0; ncols > j; j++)
			matrix_set_entry(m, ME(i, j),
					 random_number(min, max));
}

void matirx_random_pos_def(struct _matrix * m, double min, double max)
{
#ifdef NERVOUS
	if (!m || !m->elements) {
		fprintf(stderr, "invalid matrix in matrix_random_pos_def");
		return;
	}
#endif

	/* TODO check rank */
	struct _matrix * b = matrix_alloc(NxN);
	if (!b) {
		fprintf(stderr, "out of NxN matices "
				"in matrix_random_pos_def\n");
		return;
	}
	matrix_random(b, min, max);

	struct _matrix * c = matrix_alloc(NxN);
	if (!c) {
		fprintf(stderr, "out of NxN matices "
				"in matrix_random_pos_def\n");
		return;
	}
	matrix_copy(c, b);

	matrix_trans(b);
	matrix_mult(m, b, c);

	matrix_free(b);
	matrix_free(c);
}
