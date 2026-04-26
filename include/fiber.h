#ifndef FIBER_H
#define FIBER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <atomic>
#include "context_swap.h"

#define MAX_FIBERS   1024
#define STACK_SIZE   65536
#define NUM_WORKERS  8
#define MAX_COUNTERS 256

/* --------------------------- FIBER STATE --------------------------- */

typedef enum fiber_state {
    RUNNING  = 0,
    RUNNABLE = 1,
    BLOCKED  = 2,
    DONE     = 3,
} fiber_state;

/* --------------------------- COUNTER STRUCT ------------------------ */

typedef struct counter {
    std::atomic<int>  count;
    struct fiber     *waiting_fiber;
    int               waiting_worker;
} counter_t;

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
    counter_t    *completion_counter;  // decremented when fiber finishes
    struct fiber *wakeup_target;  
} fiber_t;

/* --------------------------- SCHEDULER API ------------------------- */

void scheduler_init(int n_workers = NUM_WORKERS);
void scheduler_run(int n_workers = NUM_WORKERS);

// spawn an independent fiber
int spawn(void (*func)(void *), void *args);

// spawn a fiber associated with a counter
// counter is decremented when fiber finishes
int spawn_with_counter(void (*func)(void *), void *args, counter_t *c);

// yield current fiber back to scheduler
void yield();

// suspend current fiber until counter reaches value
void wait_for_counter(counter_t *c, int value);

int get_counter_index(counter_t *c);

/* --------------------------- COUNTER API --------------------------- */

// allocate a counter initialized to count
counter_t *create_counter(int count);

// free a counter back to the pool
void free_counter(counter_t *c);

/* --------------------------- SCHEDULER STATS ----------------------- */

extern std::atomic<long> steal_attempts;
extern std::atomic<long> steal_successes;
extern std::atomic<long> steal_aborts;
extern bool stealing_enabled;

void reset_scheduler_stats();
void print_scheduler_stats();
void get_per_worker_stats(int local_out[], int stolen_out[], long long cycles_out[]);

#endif