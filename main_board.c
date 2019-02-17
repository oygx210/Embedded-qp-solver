/***************************************************************************/ /**
 * @file
 * @brief A very simple demonstration of different power modes.
 * @version 5.6.1
 *******************************************************************************
 * # License
 * <b>Copyright 2015 Silicon Labs, Inc. http://www.silabs.com</b>
 *******************************************************************************
 *
 * This file is licensed under the Silabs License Agreement. See the file
 * "Silabs_License_Agreement.txt" for details. Before using this software for
 * any purpose, you must agree to the terms of that agreement.
 *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_wdog.h"
#include "rtcdriver.h"
#include "bsp.h"
#include "bsp_trace.h"
#include "matrix_ops.h"
#include "qp.h"
#include "qp_solvers.h"

#define TEST_PRECISION 1e-6
#define NUM_INVERSION_TEST_RUNS 8

#define P_RAND_ENTRY_MIN -1e3
#define P_RAND_ENTRY_MAX 1e3
#define Q_RAND_ENTRY_MIN -1e3
#define Q_RAND_ENTRY_MAX 1e3
#define X_RAND_ENTRY_MIN -1e3
#define X_RAND_ENTRY_MAX 1e3

#define NUM_OPT_TEST_RUNS 32

#define GRAD_ITERATIONS 1e4
#define HESS_ITERATIONS 1e1
#define ADMM_ITERATIONS 1e4

/*switch between 3 algorithm*/
//#define GRAD_TEST
#define HESS_TEST
//#define ADMM_TEST

#define ABS(x) (x < 0 ? -x : x)

//double result = 0;
/* Timer used for bringing the system back to EM0. */
RTCDRV_TimerID_t xTimerForWakeUp;

static unsigned almost_identity(struct _matrix *m, double precision)
{
	double x = 0;
	unsigned nrows = MATRIX_GET_ROW(m);
	for (unsigned i = 0; nrows > i; i++)
		for (unsigned j = 0; nrows > j; j++)
		{
			x = matrix_get_entry(m, ME(i, j));
			if (isnan(x))
			{
				fprintf(stderr,
								"entry (%u, %u) is nan\n", i, j);
				return 0;
			}
			else if (i == j && precision < ABS(1 - x))
			{
				fprintf(stderr, "entry (%u, %u) is %.12lf "
												"but should be 1\n",
								i, j, x);
				return 0;
			}
			else if (i != j && precision < ABS(x))
			{
				fprintf(stderr, "entry (%u, %u) is %.12lf "
												"but should be 0\n",
								i, j, x);
				return 0;
			}
		}
	return 1U;
}

void inversion_test(void)
{
	unsigned i = 0;
	for (; NUM_INVERSION_TEST_RUNS > i; i++)
	{
		struct _matrix *a = matrix_alloc(NxN);
		struct _matrix *b = matrix_alloc(NxN);
		struct _matrix *c = matrix_alloc(NxN);

		matirx_random_pos_def(a, P_RAND_ENTRY_MIN, P_RAND_ENTRY_MAX);
		matrix_copy(b, a);
		matrix_invert(a);
		matrix_mult(c, b, a);
		if (!almost_identity(c, TEST_PRECISION))
			fprintf(stderr, "error\n");
		matrix_free(a);
		matrix_free(b);
		matrix_free(c);
	}

	printf("%u inversion tests completed\n", i);
}

void scalar_prod_test(void)
{
	struct _matrix *a = matrix_alloc(NxN);
	for (unsigned i = 0; N > i; i++)
		matrix_set_entry(a, ME(i, 0), i);

	struct _matrix *b = matrix_alloc(Nx1);
	matrix_set_entry(b, ME(0, 0), 1);

	struct _matrix *c = matrix_alloc(Nx1);
	matrix_mult(c, a, b);

	struct _matrix *d = matrix_alloc(Nx1);
	for (unsigned i = 0; N > i; i++)
		matrix_set_entry(d, ME(i, 0), i);
	matrix_trans(c);
	matrix_trans(d);

	double should_be = (N * (N + 1) * (2 * N + 1) / 6) - N * N;
	if (should_be != matrix_scalar_prod(d, c))
		fprintf(stderr, "error in mult test\n");

	matrix_free(a);
	matrix_free(b);
	matrix_free(c);
	matrix_free(d);

	printf("scalar_prod test completed\n");
}

void test_optimizer(struct _quadratic_form *qf, unsigned iterations,
										struct _matrix *x0, const char *opt_name,
										struct _matrix *(*opt)(struct _matrix *,
																					 unsigned,
																					 struct _quadratic_form *))
{
	//time_t start = time(0);
	struct _matrix *x = opt(x0, iterations, qf);
	//time_t end = time(0);
	//result = quadratic_form_eval(qf, x);
	fflush(stdout);

	matrix_free(x);
}

/***************************************************************************/ /**
 * @brief  Main function
 ******************************************************************************/
int main(void)
{
	/* Chip revision alignment and errata fixes */
	CHIP_Init();

	/* If first word of user data page is non-zero, enable Energy Profiler trace */
	BSP_TraceProfilerSetup();
	//led init
	//BSP_LedsInit();
	//BSP_LedClear(0);
	/* Initialize RTC timer. */
	RTCDRV_Init();
	RTCDRV_AllocateTimer(&xTimerForWakeUp);

	/* important */
	kmalloc_init();
	srand(time(0));
	/* some test for matrix lib */
	//inversion_test();
	//scalar_prod_test();

	/* prepare quadratic cost function */
	struct _matrix *p = matrix_alloc(NxN);
	struct _matrix *q = matrix_alloc(Nx1);
	double r = 0;
	struct _quadratic_form *qf = quadratic_form_alloc(p, q, r);
	if (!qf)
	{

		return EXIT_FAILURE;
	}

	/* to hold initial state */
	struct _matrix *x0 = matrix_alloc(Nx1);
	//2 seconds of sleep mode before starting
	RTCDRV_StartTimer(xTimerForWakeUp, rtcdrvTimerTypeOneshot, 2000, NULL, NULL);
	EMU_EnterEM3(true);

	//task period 10 seconds, generate interrupt periodically to wake the device up from EM3
	RTCDRV_StartTimer(xTimerForWakeUp, rtcdrvTimerTypePeriodic, 10000, NULL, NULL);
	for (unsigned i = 0; NUM_OPT_TEST_RUNS > i; i++)
	{

		matirx_random_pos_def(p, P_RAND_ENTRY_MIN, P_RAND_ENTRY_MAX);
		matrix_random(q, Q_RAND_ENTRY_MIN, Q_RAND_ENTRY_MAX);
		matrix_random(x0, X_RAND_ENTRY_MIN, X_RAND_ENTRY_MAX);

#ifdef GRAD_TEST
		test_optimizer(qf, GRAD_ITERATIONS, x0,
									 "gradient_descent_with_line_search",
									 gradient_descent_with_line_search);
#endif
#ifdef HESS_TEST
		test_optimizer(qf, HESS_ITERATIONS, x0,
									 "newton_method_with_line_search",
									 newton_method_with_line_search);
#endif
#ifdef ADMM_TEST
		test_optimizer(qf, ADMM_ITERATIONS, x0, "admm", admm);
#endif

		EMU_EnterEM3(true);
	}

	matrix_free(x0);
	matrix_free(p);
	matrix_free(q);
	quadratic_form_free(qf);
	while (1)
		EMU_EnterEM3(true);
	return 0;
}
