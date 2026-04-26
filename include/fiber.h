#ifndef FIBER_H
#define FIBER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <atomic>
#include "context_swap.h"

#define MAX_FIBERS  1024
#define STACK_SIZE  8192
#define NUM_WORKERS 8

/* --------------------------- FIBER STATE --------------------------- */

typedef enum fiber_state {
    RUNNING  = 0,
    RUNNABLE = 1,
    BLOCKED  = 2,
    DONE     = 3,
} fiber_state;

/* --------------------------- FIBER STRUCT -------------------------- */

typedef struct fiber {
    Context       ctx;
    fiber_state   state;
    void         *user_stack;
    void        (*func)(void *);
    void         *args;
    int           born_on_worker;
    int           ran_on_worker;
    long long     start_cycles;
    long long     end_cycles;
} fiber_t;

/* --------------------------- SCHEDULER API ------------------------- */

// initialize the scheduler with n_workers kernel threads
void scheduler_init(int n_workers = NUM_WORKERS);

// run until all spawned fibers complete
void scheduler_run(int n_workers = NUM_WORKERS);

// spawn a fiber onto the next worker's queue
// returns 0 on success, -1 if pool is full
int spawn(void (*func)(void *), void *args);

// yield current fiber back to scheduler
void yield();

/* --------------------------- SCHEDULER STATS ----------------------- */

extern std::atomic<long> steal_attempts;
extern std::atomic<long> steal_successes;
extern std::atomic<long> steal_aborts;
extern bool stealing_enabled;

void set_next_worker(int w);
void reset_scheduler_stats();
void print_scheduler_stats();
void get_per_worker_stats(int local_out[], int stolen_out[], long long cycles_out[]);

#endif