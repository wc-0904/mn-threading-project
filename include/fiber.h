#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <context_swap.h>

#define MAX_FIBERS 14                   // we can change this later

// enum for fiber state
typedef enum fiber_state {
    RUNNING = 0,
    RUNNABLE = 1,
    BLOCKED = 2,
    WAITING = 3,
} fiber_state;

// general fiber struct
typedef struct fiber {
    Context ctx;
    fiber_state state;
    void *user_stack;                   // for now void *, may chage to interrupt stack frame *
} fiber_t;

// scheduler context
typedef struct scheduler {
    Context sched_context;               // scheduler's own context
    fiber_t *queue[MAX_FIBERS];         // simple array for now
    uint32_t head, tail, count;
    fiber_t *curr_running_fiber;        // currently running fiber
} scheduler_ctx;