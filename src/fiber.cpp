#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <atomic>
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

static pthread_spinlock_t pool_lock;
static std::atomic<int> active_fibers{0};

// stealing counters
std::atomic<long> steal_attempts{0};
std::atomic<long> steal_successes{0};
std::atomic<long> steal_aborts{0};

// stealing enable/disable flag
bool stealing_enabled = true;
static bool pool_lock_initialized = false;

/* --------------------------------- DEFINITIONS --------------------------------- */

// we use this to allocate a fiber instead of malloc
// basically picks a pre allocated chunk from the fiber/stack
// pool, return NULL if no space available
void *alloc_fiber() {
    pthread_spin_lock(&pool_lock);
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (!slot_used[i]) {
            slot_used[i] = true;
            fiber_pool[i].user_stack = stack_pool[i];
            pthread_spin_unlock(&pool_lock);
            return &fiber_pool[i];
        }
    }
    pthread_spin_unlock(&pool_lock);
    return NULL;
}

void free_fiber(fiber_t *fib) {
    pthread_spin_lock(&pool_lock);
    int idx = fib - fiber_pool;
    slot_used[idx] = false;
    pthread_spin_unlock(&pool_lock);
}


static void fiber_entry() {
    fiber_t *fib = workers[worker_id].curr_running_fiber;
    fib->func(fib->args);

    // fiber may have migrated via stealing, re-read current worker
    worker_t *w = &workers[worker_id];
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
    if (!stealing_enabled) return nullptr;
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (i == my_id) continue;
        while (true) {
            steal_attempts.fetch_add(1);
            fiber_t* f = workers[i].queue->steal();
            if (f == reinterpret_cast<fiber_t*>(-1)) {
                steal_aborts.fetch_add(1);
                continue; // retry same worker (CAS race)
            }
            if (f != nullptr) {
                 steal_successes.fetch_add(1);
                return f; // success
            }
            break; // empty so stop trying this worker
        }
    }
    return nullptr;
}

void set_next_worker(int w) {
    next_worker = w;
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

    while (active_fibers.load() > 0) {

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
            if (fib == NULL) {
                sched_yield();  // backoff before retrying
                continue;
            }
        }

        fib->state = RUNNING;
        w->curr_running_fiber = fib;


        fib->ran_on_worker = id;
        fib->start_cycles = __builtin_ia32_rdtsc();
        swap_context(&w->sched_context, &fib->ctx);
        fib->end_cycles = __builtin_ia32_rdtsc();

        // if fiber isn't done, add back to queue
        if (fib->state == RUNNABLE) {
            w->queue->pushBottom(fib);

        // if done, free the fiber
        } else if (fib->state == DONE) {
            free_fiber(fib);
            active_fibers.fetch_sub(1);
        }
    }

    return NULL;
}

// initialize the scheduler
void scheduler_init() {
    if (!pool_lock_initialized) {
        pthread_spin_init(&pool_lock, 0);
        pool_lock_initialized = true;
    }

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
    fib->born_on_worker = next_worker;
    fib->ran_on_worker = -1;


    workers[next_worker].queue->pushBottom(fib);

    // track the next worker to be handled
    next_worker = (next_worker + 1) % NUM_WORKERS;

    active_fibers.fetch_add(1);
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

void reset_scheduler_stats() {
    steal_attempts.store(0);
    steal_successes.store(0);
    steal_aborts.store(0);
    for (int i = 0; i < MAX_FIBERS; i++) {
        fiber_pool[i].ran_on_worker = -1;
        fiber_pool[i].born_on_worker = -1;
        fiber_pool[i].start_cycles = 0;
        fiber_pool[i].end_cycles = 0;
    }
}

void get_per_worker_stats(int local_out[], int stolen_out[], long long cycles_out[]) {
    for (int i = 0; i < NUM_WORKERS; i++) {
        local_out[i] = 0;
        stolen_out[i] = 0;
        cycles_out[i] = 0;
    }
    for (int i = 0; i < MAX_FIBERS; i++) {
        fiber_t *f = &fiber_pool[i];
        if (f->ran_on_worker == -1) continue;
        long long cycles = f->end_cycles - f->start_cycles;
        cycles_out[f->ran_on_worker] += cycles;
        if (f->born_on_worker == f->ran_on_worker)
            local_out[f->ran_on_worker]++;
        else
            stolen_out[f->ran_on_worker]++;
    }
}

void print_scheduler_stats() {
    long attempts = steal_attempts.load();
    long successes = steal_successes.load();
    long aborts = steal_aborts.load();
    printf("=== Scheduler Stats ===\n");
    printf("steal attempts:  %ld\n", attempts);
    printf("steal successes: %ld\n", successes);
    printf("steal aborts:    %ld\n", aborts);
    if (attempts > 0) {
        printf("success rate:    %.1f%%\n", 100.0 * successes / attempts);
        printf("abort rate:      %.1f%%\n", 100.0 * aborts / attempts);
    }

    // cache migration analysis
    long local_count = 0, stolen_count = 0;
    long long local_cycles = 0, stolen_cycles = 0;
    for (int i = 0; i < MAX_FIBERS; i++) {
        fiber_t *f = &fiber_pool[i];
        if (f->ran_on_worker == -1) continue;
        long long cycles = f->end_cycles - f->start_cycles;
        if (f->born_on_worker == f->ran_on_worker) {
            local_count++; local_cycles += cycles;
        } else {
            stolen_count++; stolen_cycles += cycles;
        }
    }
    if (local_count > 0)
        printf("avg local fiber cycles:  %lld\n", local_cycles / local_count);
    if (stolen_count > 0)
        printf("avg stolen fiber cycles: %lld\n", stolen_cycles / stolen_count);
    printf("stolen fibers: %ld / %ld total\n", stolen_count, local_count + stolen_count);
}