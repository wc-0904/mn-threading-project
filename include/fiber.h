#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <context_swap.h>

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
    void *user_stack;   // for now void *, may chage to interrupt stack frame
} fiber_t;

