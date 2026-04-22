#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include "context_swap.h"
#include "fiber.h"
#include "ws_deque.h"


/* --------------------------- STRUCTS & ENUMS --------------------------- */

// worker thread struct
typedef struct worker {
    int id;
    Context sched_context;
    deque *queue;
    fiber_t *curr_running_fiber;
    pthread_t thread;
} worker_t;

// // run queue (circular buffer for now)
// typedef struct run_queue {
//     fiber_t *fib_arr[MAX_FIBERS];
//     uint32_t head;
//     uint32_t tail;
//     uint32_t size;
// } run_queue_t;

// // scheduler context
// typedef struct scheduler_ctx {
//     Context sched_context;          // scheduler's own context
//     deque *queue;             // cirucular buffer for now
//     fiber_t *curr_running_fiber;     // currently running fiber
// } scheduler_ctx_t;


/* ----------------------------------- GLOBALS ----------------------------------- */

static fiber_t fiber_pool[MAX_FIBERS];
static char stack_pool[MAX_FIBERS][STACK_SIZE];
static bool slot_used[MAX_FIBERS];

// // main scheduler and queue
// static scheduler_ctx_t scheduler;
// static deque queue;

static worker_t workers[NUM_WORKERS];
static __thread int worker_id;
static int next_worker = 0;


/* --------------------------------- DEFINITIONS --------------------------------- */

// // queues up a fiber
// int enq(scheduler_ctx_t *s, fiber_t *fib) {
//     // original checked if the queue was full and queued
//     // until we add dynamic queue resizing, this will drop if its too fast i think
//     s->queue->pushBottom(fib); 
//     return 0;
// }

// // dequeues a fiber (NULL if queue is empty)
// fiber_t *deq(scheduler_ctx_t *s) {
//     fiber_t* f = s->queue->popBottom();
//     if (f == nullptr || f == reinterpret_cast<fiber_t*>(-1)) 
//         return nullptr;
//     return f;
// }

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

// static void fiber_entry() {
//     // set the current running fiber and call the func
//     fiber_t *fib = scheduler.curr_running_fiber;
//     fib->func(fib->args);

//     // once func returns, set it as done and swap back to scheduler
//     fib->state = DONE;
//     swap_context(&fib->ctx, &scheduler.sched_context);
// }

static void fiber_entry() {
    // identify the current worker thread
    worker_t *w = &workers[worker_id];
    
    // set the current running fiber and call the func
    fiber_t *fib = w->curr_running_fiber;
    fib->func(fib->args);

    // once func returns, set it as done and swap back to scheduler
    fib->state = DONE;
    swap_context(&fib->ctx, &w->sched_context);

}

// // yield to the scheduler
// void yield() {
//     scheduler.curr_running_fiber->state = RUNNABLE;
//     swap_context(&scheduler.curr_running_fiber->ctx, &scheduler.sched_context);
// }

// yield to the scheduler
void yield() {
    // identify the current worker thread
    worker_t *w = &workers[worker_id];

    w->curr_running_fiber->state = RUNNABLE;
    swap_context(&(w->curr_running_fiber->ctx), &(w->sched_context));
}

// main worker loop
static void *worker_loop(void *arg) {
    // assign the worker ID for this thread
    int id = *(int*)arg;
    worker_id = id;

    // force a malloc call so that no malloc happens 
    // while working in the fiber's stack space
    free(malloc(1));

    worker_t *w = &workers[id];

    while (1) {

        // get a fiber from the bottom of queue
        fiber_t *fib = w->queue->popBottom();
        if (fib == NULL || fib == reinterpret_cast<fiber_t*>(-1)) {
            break;  // no stealing yet, but would add steal logic here
        }

        fib->state = RUNNING;
        w->curr_running_fiber = fib;
        swap_context(&w->sched_context, &fib->ctx);

        // if fiber isn't done, add back to queue
        if (fib->state == RUNNABLE) {
            w->queue->pushBottom(fib);

        // if done, free the fiber
        } else if (fib->state == DONE) {
            free_fiber(fib);
        }
    }

    return NULL;
}

// initialize the scheduler
void scheduler_init() {

    // initialize the workers (8 workers = 8 pthreads)
    // each worker has it's own id, deque 
    for (int i = 0; i < NUM_WORKERS; i++) {
        workers[i].id = i;
        workers[i].queue = new deque();
        workers[i].curr_running_fiber = NULL;
    }

    // reset the slot pool
    for (int i = 0; i < MAX_FIBERS; i++) {
        slot_used[i] = false;
    }

    next_worker = 0;
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

    workers[next_worker].queue->pushBottom(fib);

    // track the next worker to be handled
    next_worker = (next_worker + 1) % NUM_WORKERS;
    return 0;
}

// // main scheduler loop
// void scheduler_run() {
    
//     while (1) {
//         fiber_t *next_fib = deq(&scheduler);

//         // if no fibers left, we done
//         if (next_fib == NULL) break;

//         // set state and swap into fiber
//         next_fib->state = RUNNING;
//         scheduler.curr_running_fiber = next_fib;
//         swap_context(&scheduler.sched_context, &next_fib->ctx);

//         // once control comes back, check state
//         if (next_fib->state == RUNNABLE) {
//             enq(&scheduler, next_fib);
//         }
//         else if (next_fib->state == DONE) {
//             free_fiber(next_fib);
//         }
//     }
// }

// main scheduler
void scheduler_run() {
    // workers get their own pthreads
    for (int i = 1; i < NUM_WORKERS; i++) {
        pthread_create(&workers[i].thread, NULL, &worker_loop, &workers[i].id);
    }

    // main thread runs worker 0
    worker_loop(&workers[0].id);

    // wait for all workers
    for (int i = 1; i < NUM_WORKERS; i++) {
        pthread_join(workers[i].thread, NULL);
    }
}