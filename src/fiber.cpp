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


/* ----------------------------------- GLOBALS ----------------------------------- */

static fiber_t fiber_pool[MAX_FIBERS];
static char stack_pool[MAX_FIBERS][STACK_SIZE];
static bool slot_used[MAX_FIBERS];

static worker_t workers[NUM_WORKERS];
static __thread int worker_id;
static int next_worker = 0;


/* --------------------------------- DEFINITIONS --------------------------------- */

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
    // identify the current worker thread
    worker_t *w = &workers[worker_id];
    
    // set the current running fiber and call the func
    fiber_t *fib = w->curr_running_fiber;
    fib->func(fib->args);

    // once func returns, set it as done and swap back to scheduler
    fib->state = DONE;
    swap_context(&fib->ctx, &w->sched_context);

}

// yield to the scheduler
void yield() {
    // identify the current worker thread
    worker_t *w = &workers[worker_id];
    w->curr_running_fiber->state = RUNNABLE;
    swap_context(&(w->curr_running_fiber->ctx), &(w->sched_context));
}

// steal work from another words non empty queue
fiber_t* try_steal(int my_id) {
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (i == my_id) continue;
        while (true) {
            fiber_t* f = workers[i].queue->steal();
            if (f == reinterpret_cast<fiber_t*>(-1)) {
                continue; // retry same worker (CAS race)
            }
            if (f != nullptr) {
                return f; // success
            }
            break; // empty so stop trying this worker
        }
    }
    return nullptr;
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
        fiber_t *fib;
        while (true) {
            fib = w->queue->popBottom();
            if (fib == reinterpret_cast<fiber_t*>(-1)) { // abort
                continue; // retry (CAS race)
            }
            break;
        }

        if (fib == NULL) {
            fib = try_steal(id);
            if (fib == nullptr) {
                return NULL;
            }
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