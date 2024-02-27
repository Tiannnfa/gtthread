#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "gt_include.h"

#define ROWS 256
#define COLS ROWS
#define SIZE COLS

#define NUM_CPUS 2
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE / NUM_GROUPS)

#define NUM_THREADS 128 // The number of uthreads is 128
						// Per matrix size and per credit is 8 uthreads

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'.
 * T(g, t) is responsible for multiplication :
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;

typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	int size;	   // The size of matrix per uthread

} uthread_arg_t;

struct timeval tv1;
long long waitTime[NUM_THREADS];
long long executionTime[NUM_THREADS];
extern long long cpuTime[NUM_THREADS];

uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];
int m_size[4] = {32, 64, 128, 256};
int c_val[4] = {25, 50, 75, 100};
int balance_flag = 0;
int priority_flag = 0;
int credit_flag = 0;
int priority_credit = 100; // This is hardcoded to be 100 in order to be fair on CPU

static void generate_matrix(matrix_t *mat, int val)
{

	int i, j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for (i = 0; i < mat->rows; i++)
		for (j = 0; j < mat->cols; j++)
		{
			mat->m[i][j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for (i = 0; i < SIZE; i++)
	{
		for (j = 0; j < SIZE; j++)
			printf(" %d ", mat->m[i][j]);
		printf("\n");
	}

	return;
}

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int credit);

static void *uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	int size;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i = 0;
	j = 0;
	k = 0;
	size = ptr->size;

	start_row = 0;
	end_row = size;

	start_col = 0;
	end_col = size;

	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	// fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started", ptr->tid, ptr->gid, cpuid);

	for (i = start_row; i < end_row; i++)
		for (j = start_col; j < end_col; j++)
			for (k = 0; k < size; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

	gettimeofday(&tv2, NULL);

	// Try to yield the CPU to the scheduler
	gt_yield();

	executionTime[ptr->tid] = (((tv2.tv_sec - tv1.tv_sec) * 1000000) + (tv2.tv_usec - tv1.tv_usec));
	waitTime[ptr->tid] = executionTime[ptr->tid] - cpuTime[ptr->tid];

#undef ptr
	return 0;
}

matrix_t A[4], B[4], C[4];

static void init_matrices()
{

	for (int inx = 0; inx < 4; inx++)
	{
		generate_matrix(&A[inx], 1);
		generate_matrix(&B[inx], 1);
		generate_matrix(&C[inx], 0);
	}
	return;
}

void parse_args(int argc, char *argv[])
{
	int inx;

	for (inx = 0; inx < argc; inx++)
	{
		if (argv[inx][0] == '-')
		{
			if (!strcmp(&argv[inx][1], "lb"))
			{
				// TODO: add option of load balancing mechanism
				balance_flag = 1;
				printf("enable load balancing\n");
			}
			else if (!strcmp(&argv[inx][1], "s"))
			{
				// TODO: add different types of scheduler
				inx++;
				if (!strcmp(&argv[inx][0], "0"))
				{
					priority_flag = 1;
					printf("use priority scheduler\n");
				}
				else if (!strcmp(&argv[inx][0], "1"))
				{
					credit_flag = 1;
					printf("use credit scheduler\n");
				}
			}
		}
	}

	return;
}

int main(int argc, char *argv[])
{
	uthread_arg_t *uarg;
	int inx;

	parse_args(argc, argv);

	gtthread_app_init();

	init_matrices();

	gettimeofday(&tv1, NULL);

	// i stands for matrix size type
	// j stands for credit value type
	// k stands for the number of matrix having certain matrix size and credit type
	for (int i = 0; i < 4; i++)
	{
		for (int j = 3; j >= 0; j--)
		{
			for (int k = 0; k < 8; k++)
			{
				inx = i * 32 + j * 8 + k;
				uarg = &uargs[inx];
				uarg->_A = &A[i];
				uarg->_B = &B[i];
				uarg->_C = &C[i];

				uarg->tid = inx;

				uarg->gid = i;

				uarg->size = m_size[i];
				uarg->start_row = 0;

				// The default scheduler is priority scheduler
				if (credit_flag == 1)
				{
					// 4 different credits type each has 8 matrices
					uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, c_val[j]);
				}
				else // Priority scheduler
				{
					uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, priority_credit);
				}
			}
		}
	}

	gtthread_app_exit();

	// Print the time info in Markdown format
	printf("| Thread_Number | cpu_time(us) | wait_time(us) | exec_time(us)|\n");
	printf("|:------:|:------:|:------:|:------:|\n");
	for (int i = 0; i < NUM_THREADS; i++)
	{
		
		printf("| %d | %10lld| %10lld | %10lld |\n", i, cpuTime[i], waitTime[i], executionTime[i]);
	}
	for (int i = 0; i < 4; i++)
	{
		for (int j = 3; j >= 0; j--)
		{
			long long avg_cpu = 0;
			long long avg_wait = 0;
			long long avg_exec = 0;
			for (int k = 0; k < 8; k++)
			{
				avg_cpu += cpuTime[i * 32 + j * 8 + k];
				avg_wait += waitTime[i * 32 + j * 8 + k];
				avg_exec += executionTime[i * 32 + j * 8 + k];
			}
			avg_cpu /= 8;
			avg_wait /= 8;
			avg_exec /= 8;
			printf("Matrix Size: %d Credit Value: %d Average CPU Time: %10lld Average Wait Time: %10lld Average Execution Time: %10lld\n", m_size[i], c_val[j], avg_cpu, avg_wait, avg_exec);
		}
	}
	return 0;
}
