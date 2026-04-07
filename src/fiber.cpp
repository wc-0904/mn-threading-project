#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "context_swap.h"
#include "fiber.h"
#include "ws_deque.h"


/* --------------------------- STRUCTS & ENUMS --------------------------- */

// run queue (circular buffer for now)
typedef struct run_queue {
    fiber_t *fib_arr[MAX_FIBERS];
    uint32_t head;
    uint32_t tail;
    uint32_t size;
} run_queue_t;

// scheduler context
typedef struct scheduler_ctx {
    Context sched_context;          // scheduler's own context
    deque *queue;             // cirucular buffer for now
    fiber_t *curr_running_fiber;     // currently running fiber
} scheduler_ctx_t;


/* ----------------------------------- GLOBALS ----------------------------------- */

static fiber_t fiber_pool[MAX_FIBERS];
static char stack_pool[MAX_FIBERS][STACK_SIZE];
static bool slot_used[MAX_FIBERS];

// main scheduler and queue
static scheduler_ctx_t scheduler;
static deque queue;


/* --------------------------------- DEFINITIONS --------------------------------- */

// queues up a fiber
int enq(scheduler_ctx_t *s, fiber_t *fib) {
    // original checked if the queue was full and queued
    // until we add dynamic queue resizing, this will drop if its too fast i think
    s->queue->pushBottom(fib); 
    return 0;
}

// dequeues a fiber (NULL if queue is empty)
fiber_t *deq(scheduler_ctx_t *s) {
    fiber_t* f = s->queue->popBottom();
    if (f == nullptr || f == reinterpret_cast<fiber_t*>(-1)) 
        return nullptr;
    return f;
}

// we use this to allocate a fiber instead of malloc
// basically picks a pre allocated chunk from the fiber/stack
// pool, return NULL if no space available
void *alloc_fiber() {
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (!slot_used[i]) {
            slot_used[i] = true;
            fiber_pool[i].user_stack = stack_pool[i];
            return &fiber_pool[i];
        }
    }
    return NULL;
}

void free_fiber(fiber_t *fib) {
    int idx = fib - fiber_pool;
    slot_used[idx] = false;
}

static void fiber_entry() {
    // set the current running fiber and call the func
    fiber_t *fib = scheduler.curr_running_fiber;
    fib->func(fib->args);

    // once func returns, set it as done and swap back to scheduler
    fib->state = DONE;
    swap_context(&fib->ctx, &scheduler.sched_context);
}

// initialize the scheduler (simple for now)
void scheduler_init() {
    scheduler = {};
    scheduler.queue = new deque();
}

// spawns a single fiber
int spawn(void (*func)(void *), void *args) {

    // allocate a fiber
    fiber_t *fib = (fiber_t *)alloc_fiber();
    if (fib == NULL) return -1;

    // setup context
    fib->ctx = {};
    fib->ctx.rip = (void *)&fiber_entry;
    fib->ctx.rsp = align_stack((char *)fib->user_stack, STACK_SIZE);
    fib->state = RUNNABLE;
    fib->func = func;
    fib->args = args;

    enq(&scheduler, fib);
    return 0;
}

// yield to the scheduler
void yield() {
    scheduler.curr_running_fiber->state = RUNNABLE;
    swap_context(&scheduler.curr_running_fiber->ctx, &scheduler.sched_context);
}

// main scheduler loop
void scheduler_run() {
    
    while (1) {
        fiber_t *next_fib = deq(&scheduler);

        // if no fibers left, we done
        if (next_fib == NULL) break;

        // set state and swap into fiber
        next_fib->state = RUNNING;
        scheduler.curr_running_fiber = next_fib;
        swap_context(&scheduler.sched_context, &next_fib->ctx);

        // once control comes back, check state
        if (next_fib->state == RUNNABLE) {
            enq(&scheduler, next_fib);
        }
        else if (next_fib->state == DONE) {
            free_fiber(next_fib);
        }
    }
}