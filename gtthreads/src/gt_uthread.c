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

#include "gt_include.h"
/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/

/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID 0

// Timer info
#define MILL 1000000
#define NUM_THREADS 128
extern int balance_flag;
extern long long cpuTime[NUM_THREADS] = {0};
extern long long beginTime[NUM_THREADS] = {0};

extern void gt_yield();
static void credit_update(uthread_struct_t **u);
extern void ksched_balance();

/**********************************************************************/
/* uthread scheduling */
static void
uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);

/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit);

/**********************************************************************/
/** DEFNITIONS **/
/**********************************************************************/

/**********************************************************************/
/* uthread scheduling */

/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gt_spin_lock(&(ksched_shared_info.uthread_init_lock));

	/* Register a signal(SIGUSR2) for alternate stack */
	act.sa_handler = uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if (sigaction(SIGUSR2, &act, &oldact))
	{
		fprintf(stderr, "uthread sigusr2 install failed !!");
		return -1;
	}

	/* Install alternate signal stack (for SIGUSR2) */
	if (sigaltstack(&(u_new->uthread_stack), &oldstack))
	{
		fprintf(stderr, "uthread sigaltstack install failed.");
		return -1;
	}

	/* Unblock the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);

	/* SIGUSR2 handler expects kthread_runq->cur_uthread
	 * to point to the newly created thread. We will temporarily
	 * change cur_uthread, before entering the synchronous call
	 * to SIGUSR2. */

	/* kthread_runq is made to point to this new thread
	 * in the caller. Raise the signal(SIGUSR2) synchronously */
#if 0
	raise(SIGUSR2);
#endif
	syscall(__NR_tkill, kthread_cpu_map[kthread_apic_id()]->tid, SIGUSR2);

	/* Block the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if (sigaction(SIGUSR2, &oldact, NULL))
	{
		fprintf(stderr, "uthread sigusr2 revert failed !!");
		return -1;
	}

	/* Disable the stack for signal(SIGUSR2) handling */
	u_new->uthread_stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if (sigaltstack(&oldstack, NULL))
	{
		fprintf(stderr, "uthread sigaltstack revert failed.");
		return -1;
	}

	gt_spin_unlock(&(ksched_shared_info.uthread_init_lock));
	return 0;
}

extern void uthread_schedule(uthread_struct_t *(*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);

#if 0
	fprintf(stderr, "uthread_schedule invoked !!\n");
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);

	// If this kthread yielded, remove the flag
	if (k_ctx->yield_flag)
	{
		k_ctx->yield_flag = 0;
	}

	if ((u_obj = kthread_runq->cur_uthread))
	{

		// credit_update(&u_obj);
		// credit information update each uthread_schedule
		struct timeval currTime, lastTime;

		lastTime = ((&u_obj->credits)->latest);
		gettimeofday(&currTime, NULL);

#if 0
		printf("%s %d\n", "u_obj before:", u_obj->credits.usec_executed);
#endif
		u_obj->credits.usec_executed += ((currTime.tv_sec * MILL) + currTime.tv_usec) - ((lastTime.tv_sec * MILL) + lastTime.tv_usec);
#if 0
		printf("%s %d\n", "u_obj after:", u_obj->credits.usec_executed);
#endif
		/* long long itval = (((currTime.tv_sec * MILL) + currTime.tv_usec) - ((lastTime.tv_sec * MILL) + lastTime.tv_usec)) / 1000; */
		long long itval = (((currTime.tv_sec - lastTime.tv_sec)* MILL) + (currTime.tv_usec- lastTime.tv_usec)) / 1000;

		u_obj->credits.remaining_credit -= itval;
#if 0
		printf("uThread %d has %d credits left and consumes %lld\n", u_obj->uthread_tid, u_obj->credits.remaining_credit, itval);
#endif

		/*Go through the runq and schedule the next thread to run */
		kthread_runq->cur_uthread = NULL;

		if (u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED))
		{
			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */
			uthread_head_t *kthread_zhead = &(kthread_runq->zombie_uthreads);
			gt_spin_lock(&(kthread_runq->kthread_runqlock));
			kthread_runq->kthread_runqlock.holder = 0x01;
			TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));

			{

				ksched_shared_info_t *ksched_info = &ksched_shared_info;
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);

				// Write down the cpuTime and beginTime
				cpuTime[u_obj->uthread_tid] = beginTime[u_obj->uthread_tid] = 0;
				cpuTime[u_obj->uthread_tid] = u_obj->credits.usec_executed;
				beginTime[u_obj->uthread_tid] = u_obj->credits.started.tv_usec + (u_obj->credits.started.tv_sec * MILL);
#if 0
				printf("\nuthread (id:%d) created at %lus %lu\n", u_obj->uthread_tid,
					   u_obj->credits.started.tv_sec,
					   u_obj->credits.started.tv_usec);
#endif
			}
		}
		else
		{
			/* XXX: Apply uthread_group_penalty before insertion */
			u_obj->uthread_state = UTHREAD_RUNNABLE;
			if (u_obj->credits.remaining_credit < 0)
			{
				u_obj->credits.remaining_credit = u_obj->credits.default_credit;
				add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			}
			else
			{
				add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_obj);
			}

			/* XXX: Save the context (signal mask not saved) */
			if (sigsetjmp(u_obj->uthread_env, 0))
				return;
		}
	}

	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	if (!(u_obj = kthread_best_sched_uthread(kthread_runq)))
	{

		// Load balance part
		kthread_context_t *tmp_k_ctx;
		if (kthread_apic_id() && balance_flag)
		{
			for (int i = 1; i < GT_MAX_KTHREADS; i++)
			{

				if ((tmp_k_ctx = kthread_cpu_map[i]) && (tmp_k_ctx != k_ctx))
				{
					if (tmp_k_ctx->kthread_flags & KTHREAD_DONE)
						continue;
					if (u_obj = kthread_best_sched_uthread(&(tmp_k_ctx->krunqueue)))
					{
						add_to_runqueue(k_ctx->krunqueue.active_runq, &(k_ctx->krunqueue.kthread_runqlock), u_obj);
						printf("balance uthread has been added from CPU %d to CPU %d\n", kthread_apic_id(), i);
						break;
					}
				}
			}
		}

		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if (ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}

		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}

	kthread_runq->cur_uthread = u_obj;
	if ((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}

	u_obj->uthread_state = UTHREAD_RUNNING;
	gettimeofday(&(u_obj->credits.latest), NULL);

	// Reset the timer due to change made by gt_yield()
	kthread_init_vtalrm_timeslice();

	/* Re-install the scheduling signal handlers */
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);

	/* kthread_unblock_signal(SIGVTALRM);
	kthread_unblock_signal(SIGUSR1); */

	/* Jump to the selected uthread context */
	siglongjmp(u_obj->uthread_env, 1);

	return;
}

/* For uthreads, we obtain a seperate stack by registering an alternate
 * stack for SIGUSR2 signal. Once the context is saved, we turn this
 * into a regular stack for uthread (by using SS_DISABLE). */
static void uthread_context_func(int signo)
{
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

	// printf("..... uthread_context_func .....\n");
	/* kthread->cur_uthread points to newly created uthread */
	if (!sigsetjmp(kthread_runq->cur_uthread->uthread_env, 0))
	{
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		assert(kthread_runq->cur_uthread->uthread_state == UTHREAD_INIT);
		kthread_runq->cur_uthread->uthread_state = UTHREAD_RUNNABLE;
		return;
	}

	/* UTHREAD_RUNNING : siglongjmp was executed. */
	cur_uthread = kthread_runq->cur_uthread;
	assert(cur_uthread->uthread_state == UTHREAD_RUNNING);
	/* Execute the uthread task */
	cur_uthread->uthread_func(cur_uthread->uthread_arg);
	cur_uthread->uthread_state = UTHREAD_DONE;

	uthread_schedule(&sched_find_best_uthread);
	return;
}

/**********************************************************************/
/* uthread creation */

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit)
{
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_new;

	/* Signals used for cpu_thread scheduling */
	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);

	/* create a new uthread structure and fill it */
	if (!(u_new = (uthread_struct_t *)MALLOCZ_SAFE(sizeof(uthread_struct_t))))
	{
		fprintf(stderr, "uthread mem alloc failure !!");
		exit(0);
	}
	gettimeofday(&(u_new->credits.started), NULL);
	u_new->uthread_state = UTHREAD_INIT;
	u_new->uthread_priority = DEFAULT_UTHREAD_PRIORITY;
	u_new->uthread_gid = u_gid;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;
	u_new->credits.remaining_credit = credit;
	u_new->credits.default_credit = credit;
	u_new->credits.usec_executed = 0;

	/* Allocate new stack for uthread */
	u_new->uthread_stack.ss_flags = 0; /* Stack enabled for signal handling */
	if (!(u_new->uthread_stack.ss_sp = (void *)MALLOC_SAFE(UTHREAD_DEFAULT_SSIZE)))
	{
		fprintf(stderr, "uthread stack mem alloc failure !!");
		return -1;
	}
	u_new->uthread_stack.ss_size = UTHREAD_DEFAULT_SSIZE;

	{
		ksched_shared_info_t *ksched_info = &ksched_shared_info;

		gt_spin_lock(&ksched_info->ksched_lock);
		u_new->uthread_tid = ksched_info->kthread_tot_uthreads++;
		ksched_info->kthread_cur_uthreads++;
		gt_spin_unlock(&ksched_info->ksched_lock);
	}

	/* XXX: ksched_find_target should be a function pointer */
	kthread_runq = ksched_find_target(u_new);

	*u_tid = u_new->uthread_tid;
	/* Queue the uthread for target-cpu. Let target-cpu take care of initialization. */
	add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_new);

	/* WARNING : DONOT USE u_new WITHOUT A LOCK, ONCE IT IS ENQUEUED. */

	/* Resume with the old thread (with all signals enabled) */
	kthread_unblock_signal(SIGVTALRM);
	kthread_unblock_signal(SIGUSR1);

	return 0;
}

static void credit_update(uthread_struct_t **u)
{
	uthread_struct_t *u_obj = *u;
	struct timeval currTime, lastTime;

	lastTime = ((&u_obj->credits)->latest);
	gettimeofday(&currTime, NULL);

	u_obj->credits.usec_executed += ((currTime.tv_sec * MILL) + currTime.tv_usec) - ((lastTime.tv_sec * MILL) + lastTime.tv_usec);
	long long itval = (((currTime.tv_sec * MILL) + currTime.tv_usec) - ((lastTime.tv_sec * MILL) + lastTime.tv_usec)) / 1000;
	u_obj->credits.remaining_credit -= itval;

	*u = u_obj;
}

extern void gt_yield()
{
	struct itimerval yield_itval;

	kthread_block_signal(SIGVTALRM);
	kthread_block_signal(SIGUSR1);

	kthread_context_t *k_ctx;
	k_ctx = kthread_cpu_map[kthread_apic_id()];

	yield_itval.it_interval.tv_sec = 0;
	yield_itval.it_interval.tv_usec = 0;
	yield_itval.it_value.tv_sec = 0;
	yield_itval.it_value.tv_usec = 1000;

	setitimer(ITIMER_VIRTUAL, &yield_itval, NULL);
	kthread_unblock_signal(SIGVTALRM);
	kthread_unblock_signal(SIGUSR1);
}