# Project 1 Description

**Due Date - Feb 11, 11:59 PM**

## 0. Reminder
- Start to work this project as early as possible.
- Write code by yourself and be honest.
- Ask questions through Piazza or come to office hours.
- Test program on [VM cluster](./vm_userguide.md) before making compression and [submission](./project_1_submission.md) it.

## 1. Introduction
The goal of the project is to understand and implement a credit-based scheduler in a user-level threads library.
Please refer to existed codecase and implement the credit scheduler design.

To get full score, students will need to finish the program and write a complete report.
The implementation details are listed at [section 2](#2-implementation);
the requirment of report is listed at [section 3](#3-report).

### GTThreads Library

GTThreads is a user-level thread library. Some of the features of the library are as follows:

* Multi-Processor support

   The user-level threads (uthreads) are run on all the processors available on the system.

* Local runqueue

   Each CPU has its own runqueue. The uthreads are assigned to one of these run queues at the time of creation. Part of the work in the project might involve using some metrics before assigning these uthreads the processor and/or run-time runqueue balancing.

* O(1) priority scheduler and co-scheduler

   The library includes these two scheduling algorithms.
The code can be reused for reference. For example, the priority hash tables in the library can be reused for the purpose of the credit scheduler.
In particular, look at the functions ``sched_find_best_uthread_group`` and ``sched_find_best_uthread``.


## 2. Implementation
There are three main components to implement: the credit scheduler, yield function, and load balancing.
These components should be configured via input arguments.
Enable two input arguements: (i) choosing the running scheduler, by flag ``-s``, with option ``0`` for O(1) priority scheduler and ``1`` for credit scheduler. (ii) choosing to run load balancing or not, by flag ``-lb``.

E.g. ``$ ./bin/matrix -s 1 -lb`` this command means using credit scheduler with load balancing mechanism.

 * We will test your program with the exact arguments mentioned above. Please make sure these arguements work with your `$ ./bin/matrix`.
 * Update ``Makefile`` and ``README.txt`` if you have special compile steps or instructions to run your program.

### 2.1. The Credit Scheduler
  - You should modify matrix multiplication first. In original codebase, uthreads across CPUs run single matrix multiplication with separating the matrix by rows and columns. Students are asked to change it into that **each uthread works its own matrix multiplication**.
  - The credit scheduler is a proportional fair share CPU scheduler built from the ground up to be work conserving on SMP hosts.
  - There are four different credit groups: **{25, 50, 75, 100}**. As a uthread runs, its credit is reduced by the used CPU cycles.
  - Please take a look at the following resources for more details of the credit scheduler:
    - [XenWiki for Credit Scheduler](http://wiki.xenproject.org/wiki/CreditScheduler)
    - [The introduction slides of Xen Credit CPU Scheduler](https://web.archive.org/web/20211019034445/http://www-archive.xenproject.org/files/summit_3/sched.pdf)
    - [Comparison of the Three CPU Schedulers in Xen](https://web.archive.org/web/20120714081759/http://www.xen.org/files/xensummit_4/3schedulers-xen-summit_Cherkosova.pdf)
  - To show the correctness, *students should print the queue of uthread credits*.



### 2.2. CPU Yield Function - `gt_yield`
  - When an uthread executes this function, it should yield the CPU to the scheduler, which then schedules the next thread (per its scheduling scheme).
  - On voluntary preemption by `gt_yield()`, the thread should be charged credits only for the actual CPU cycles used.
  - For this, students need to implement a library function for voluntary preemption `gt_yield()`.
  - `gt_yield` function is a pluggable module, it is recommended to add an input arguement to enable/disable it.
  - To show the correctness, *students should print credit before/after yield with the queue of uthread credits*.


### 2.3. Load Balancing
  - Load balancing across kthreads are done through the migration of uthreads between the kthreads when there is a **idle** kthread
  - Implement uthread migration if a kthread is idle (no uthread in the run queue).
  - To show the correctness, *students should print queue status after load balancing*.


## 3. Report

There is no specific style for the report, but the page limit of the report except references and appendicies is up to 6 pages.
Make sure to cover at least following content:

  - Present your understanding of GTThreads package, including the O(1) priority scheduler with a overall flow chart as **Appendix A**.
  - Present briefly how the credit scheduler works, try to explain its basic algorithm and rules.
  - Sketch your design of credit scheduler. Show how do you implement it with the provided package.
  - Clearly show when does your design add or deduct credit, when does it call gt\_yield, and how to do load balancing, and what happened to the scheduling process.
  - Experiments setup

    Using this setup to evaluate credit scheduler's performance:
    - Run 128 uthreads with 4 credit groups and 4 matrix sizes.
    - Each uthread will work on a matrix of its own.
    - Turn off `gt_yield` function
    - Credits groups are {25, 50, 75, 100}.
    - Matrix sizes are ranging in {32, 64, 128, 256}.
    - So, there are 16 possible combinations of 4 credit groups and 4 matrix sizes.

      Since there will be 128 uthreads, there will be 8 uthreads for each combination. (It isn't always interesting to parallelize matrix multiplication. Here multiple sets of threads need to be running over your scheduler with different workloads and priorities).
      - ex) 8 uthreads with credit 25 and matrix size 32, 8 uthreads with credit 50 and matrix size 32 ...

    - Collect the time taken (to the accuracy of micro-seconds) by each uthread, from the time it was created, to the time it completed its task. Also measure the CPU time that each uthread spent running (i.e., excluding the time that it spent waiting to be scheduled) every time it was scheduled.

  - Experiment results
    - You will run two experiments using the previously mentioned setup: credit scheduling without load balancing, and credit scheduling with loadbalancing.
    - Collect the following numbers for each uthread during the experiment run:
      -  CPU Time: time spent by a uthread running every time it was scheduled
      -  Wait Time: time spent by a uthread waiting in the queue, to be scheduled
      -  Execution Time: CPU Time + Wait Time
    - Collect the output of each experiment run in a file with the following format:

    |group_name|thread_number|cpu_time(us)|wait_time(us)|exec_time(us)|
    |----------|----------|----------|----------|----------|
    |c_25_m_32|0| | | |
    |c_25_m_32|1| | | |
    |c_25_m_32|2| | | |
    - This file should have 128 entries (one for each uthread), and it should be named Detailed_output.csv
    - You will also provide another file that summarizes the above data on a per group basis with the following format:

    |group_name|mean_cpu_time|mean_wait_time|mean_exec_time|
    |----------|----------|----------|----------|
    |c_25_m_32| | | |
    |c_25_m_64| | | |
    |c_25_m_128| | | |
    - This file will have 16 entries (one for each group), and it should be named Cummulative_output.csv
    - Take the Cummulative_output.csv dataset, and produce four different bar charts, one for each matrix size. The barcharts should plot mean execution time vs. group name (ordered by increasing credits), and each mean execution time bar should be split into two stacked bars, mean cpu time on bottom, and mean wait time on top.
    - Students need to create **figures of graphs for 2 experiments (credit scheduler with and without load balancing)**.
    - In the report, group the 4 plots for a given experiment into a 2x2 matrix and label the whole figure accordingly.
    - While showing the results, please try to explain your findings:
      - Does credit scheduler work as expect?
      - Does the load balancing mechanism improve the performance?
      - Does credit scheduler perform better than O(1) priority scheduler?
  - **Appendix B**: screenshot of the printed credit before/after yield with the queue of uthread credits.
    - It is fine to select uthreads of any credit group in any matrix size to call yield.
  - **Appendix C**: screenshot of the printed queue of uthread credits with load-balancing.

  - Mention implementation issues if any
    - You might want to point out some inefficiencies in your code.
If there is any minor issues, like performance, don't spend too much time trying to fix it,
but make sure you list ways in which you might improve it.


## 4. Delivering Suggestion

In order to successfully complete the project, it is recommended that during **the first week** you are able
(i) to understand the GTThreads library, (ii) the mechanisms and algorithm of the credit scheduler,
and (iii) to have a solid design of the implementation plan you will pursue.
It will be helpful to create diagrams illustrating the control flow of the GTThread library.

The major goal is implementing your credit scheduler design,
you can follow the item ordering in [section 2](#2-implementation) for implementation, the one listed earlier is more important (more credit for sure).

Don't wait until last minute to write report.
As mentioned at [section 3](#3-report), some requirements related to your understanding and design ideas.
Starting the write-up earlier not only helps you have better overview of the project, but gives you a more complete report to submit.

