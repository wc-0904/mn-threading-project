#ifndef FIBER_H
#define FIBER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <context_swap.h>

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


#endif