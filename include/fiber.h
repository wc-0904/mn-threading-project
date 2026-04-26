#ifndef FIBER_H
#define FIBER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <context_swap.h>
#include <atomic>

#define MAX_FIBERS 1024             // we can change this later
#define STACK_SIZE 8192
#define NUM_WORKERS 8

/* --------------------------- STRUCTS & ENUMS --------------------------- */

// enum for fiber state
typedef enum fiber_state {
    RUNNING = 0,
    RUNNABLE = 1,
    BLOCKED = 2,
    DONE = 3,
} fiber_state;

// general fiber struct
typedef struct fiber {
    Context ctx;
    fiber_state state;
    void *user_stack;               // for now void *, may chage to interrupt stack frame *
    void (*func)(void *);
    void *args;
    int born_on_worker;
    int ran_on_worker;
    long long start_cycles;
    long long end_cycles;
} fiber_t;

/* ------------------------------- FUNCTIONS ------------------------------- */

// creates and enqueues a fiber
// returns 0 on sucess, -1 on failure
int spawn(void (*func)(void *), void *args);

// yields the fiber to the scheduler
void yield();

// initializes the scheduler
void scheduler_init();

// starts the scheduler
void scheduler_run();

extern std::atomic<long> steal_attempts;
extern std::atomic<long> steal_successes;
extern std::atomic<long> steal_aborts;

extern bool stealing_enabled;
void print_scheduler_stats();
void reset_scheduler_stats();

// force next spawn target
void set_next_worker(int w);

// stats functions
void print_scheduler_stats();
void reset_scheduler_stats();
void get_per_worker_stats(int local_out[], int stolen_out[], long long cycles_out[]);



#endif