#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <atomic>
#include "context_swap.h"
#include "fiber.h"
#include "ws_deque.h"

/* --------------------------- WORKER STRUCT ------------------------- */

typedef struct worker {
    int       id;
    Context   sched_context;
    deque    *queue;
    fiber_t  *curr_running_fiber;
    pthread_t thread;
} worker_t;

/* --------------------------- GLOBALS ------------------------------- */

static fiber_t fiber_pool[MAX_FIBERS];
static char  (*stack_pool)[STACK_SIZE] = nullptr;
static bool    slot_used[MAX_FIBERS];

static worker_t     workers[NUM_WORKERS];
static int          num_workers = NUM_WORKERS;
static __thread int worker_id = -1;

static pthread_spinlock_t pool_lock;
static bool               pool_lock_initialized = false;

static pthread_spinlock_t counter_lock;
static bool               counter_lock_initialized = false;

static counter_t counter_pool[MAX_COUNTERS];
static bool      counter_used[MAX_COUNTERS];

// termination tracking
static std::atomic<int> total_spawned{0};
static std::atomic<int> total_done{0};

// stealing counters (exposed via header)
std::atomic<long> steal_attempts{0};
std::atomic<long> steal_successes{0};
std::atomic<long> steal_aborts{0};
bool              stealing_enabled = true;


/* --------------------------- FIBER POOL ---------------------------- */

static void *alloc_fiber() {
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

static void free_fiber(fiber_t *fib) {
    pthread_spin_lock(&pool_lock);
    int idx = fib - fiber_pool;
    slot_used[idx] = false;
    pthread_spin_unlock(&pool_lock);
}

/* --------------------------- COUNTER POOL -------------------------- */

counter_t *create_counter(int count) {
    pthread_spin_lock(&pool_lock);
    for (int i = 0; i < MAX_COUNTERS; i++) {
        if (!counter_used[i]) {
            counter_used[i]                = true;
            counter_pool[i].count.store(count);
            counter_pool[i].waiting_fiber  = nullptr;
            counter_pool[i].waiting_worker = -1;
            pthread_spin_unlock(&pool_lock);
            return &counter_pool[i];
        }
    }
    pthread_spin_unlock(&pool_lock);
    return nullptr;
}

void free_counter(counter_t *c) {
    pthread_spin_lock(&pool_lock);
    int idx = c - counter_pool;
    counter_used[idx] = false;
    pthread_spin_unlock(&pool_lock);
}

int get_counter_index(counter_t *c) {
    return c - counter_pool;
}
int get_worker_id() { return worker_id; }

/* --------------------------- FIBER EXECUTION ----------------------- */

static void fiber_entry() {
    fiber_t  *fib = workers[worker_id].curr_running_fiber;
    fib->func(fib->args);
    worker_t *w   = &workers[worker_id];
    fib->state    = DONE;

    if (fib->completion_counter != nullptr) {
        counter_t *c = fib->completion_counter;

        pthread_spin_lock(&counter_lock);
        int remaining = c->count.fetch_sub(1, std::memory_order_relaxed) - 1;
        fiber_t *parent = nullptr;
        if (remaining == 0) {
            parent = c->waiting_fiber;
            c->waiting_fiber = nullptr;
        }
        pthread_spin_unlock(&counter_lock);

        if (parent != nullptr) {
            // count parent as a new spawn so termination condition stays correct
            // total_spawned.fetch_add(1);
            fib->wakeup_target = parent;
        }
    }

    swap_context(&fib->ctx, &w->sched_context);
}

void yield() {
    worker_t *w = &workers[worker_id];
    w->curr_running_fiber->state = RUNNABLE;
    swap_context(&w->curr_running_fiber->ctx, &w->sched_context);
}

void wait_for_counter(counter_t *c, int value) {
    if (c->count.load(std::memory_order_acquire) == value) return;

    worker_t *w = &workers[worker_id];
    pthread_spin_lock(&counter_lock);
    if (c->count.load(std::memory_order_acquire) == value) {
        pthread_spin_unlock(&counter_lock);
        return;
    }
    c->waiting_fiber  = w->curr_running_fiber;
    c->waiting_worker = worker_id;
    w->curr_running_fiber->state = BLOCKED;
    pthread_spin_unlock(&counter_lock);

    swap_context(&w->curr_running_fiber->ctx, &w->sched_context);
}

/* --------------------------- WORK STEALING ------------------------- */

static fiber_t *try_steal(int my_id) {
    if (!stealing_enabled) return nullptr;

    int start = rand() % num_workers;
    for (int offset = 0; offset < num_workers; offset++) {
        int i = (start + offset) % num_workers;
        if (i == my_id) continue;
        while (true) {
            steal_attempts.fetch_add(1);
            fiber_t *f = workers[i].queue->steal();
            if (f == reinterpret_cast<fiber_t*>(-1)) {
                steal_aborts.fetch_add(1);
                continue;
            }
            if (f != nullptr) {
                steal_successes.fetch_add(1);
                return f;
            }
            break;
        }
    }
    return nullptr;
}

/* --------------------------- WORKER LOOP --------------------------- */

static void *worker_loop(void *arg) {
    int id = *(int*)arg;
    worker_id = id;
    free(malloc(1));

    worker_t *w = &workers[id];
    int backoff = 1;

    while (total_done.load() < total_spawned.load()) {
        fiber_t *fib;
        while (true) {
            fib = w->queue->popBottom();
            if (fib != reinterpret_cast<fiber_t*>(-1)) break;
        }

        if (fib == NULL) {
            fib = try_steal(id);
            if (fib == NULL) {
                int delay = backoff + (rand() % backoff);
                for (volatile int j = 0; j < delay * 100; j++);
                if (backoff < 1024) backoff <<= 1;
                continue;
            }
        }

        backoff = 1;

        fib->state            = RUNNING;
        w->curr_running_fiber = fib;
        fib->ran_on_worker    = id;
        fib->start_cycles     = __builtin_ia32_rdtsc();
        swap_context(&w->sched_context, &fib->ctx);
        fib->end_cycles       = __builtin_ia32_rdtsc();

        // push woken parent BEFORE decrementing for child
        fiber_t *wakeup = fib->wakeup_target;
        fib->wakeup_target = nullptr;
        if (wakeup != nullptr) {
            wakeup->state = RUNNABLE;
            w->queue->pushBottom(wakeup);
        }

        if (fib->state == RUNNABLE) {
            w->queue->pushBottom(fib);
        } else if (fib->state == DONE) {
            free_fiber(fib);
            total_done.fetch_add(1);
        }
    }

    return NULL;
}

/* --------------------------- PUBLIC API ---------------------------- */

void scheduler_init(int n_workers) {
    num_workers = n_workers;
    if (stack_pool == nullptr)
        stack_pool = new char[MAX_FIBERS][STACK_SIZE];
    if (!pool_lock_initialized) {
        pthread_spin_init(&pool_lock, 0);
        pool_lock_initialized = true;
    }
    if (!counter_lock_initialized) {
        pthread_spin_init(&counter_lock, 0);
        counter_lock_initialized = true;
    }
    for (int i = 0; i < n_workers; i++) {
        workers[i].id                 = i;
        workers[i].queue              = new deque(10);
        workers[i].curr_running_fiber = NULL;
    }
    for (int i = 0; i < MAX_FIBERS; i++)
        slot_used[i] = false;
    for (int i = 0; i < MAX_COUNTERS; i++) {
        counter_used[i]                = false;
        counter_pool[i].waiting_fiber  = nullptr;
        counter_pool[i].waiting_worker = -1;
    }

    total_spawned.store(0);
    total_done.store(0);
}

void scheduler_run(int n_workers) {
    for (int i = 1; i < n_workers; i++)
        pthread_create(&workers[i].thread, NULL, &worker_loop, &workers[i].id);
    worker_loop(&workers[0].id);
    for (int i = 1; i < n_workers; i++)
        pthread_join(workers[i].thread, NULL);
}

void scheduler_reset() {
    for (int i = 0; i < MAX_FIBERS; i++)
        slot_used[i] = false;
    for (int i = 0; i < MAX_COUNTERS; i++) {
        counter_used[i]               = false;
        counter_pool[i].waiting_fiber  = nullptr;
        counter_pool[i].waiting_worker = -1;
    }
    total_spawned.store(0);
    total_done.store(0);
}

int spawn(void (*func)(void *), void *args) {
    fiber_t *fib = (fiber_t *)alloc_fiber();
    if (fib == NULL) return -1;

    fib->ctx                = {};
    fib->ctx.rip            = (void *)&fiber_entry;
    fib->ctx.rsp            = align_stack((char *)fib->user_stack, STACK_SIZE);
    fib->state              = RUNNABLE;
    fib->func               = func;
    fib->args               = args;
    fib->ran_on_worker      = -1;
    fib->completion_counter = nullptr;
    fib->wakeup_target      = nullptr;

    total_spawned.fetch_add(1);

    int target = (worker_id >= 0) ? worker_id : 0;
    fib->born_on_worker = target;
    workers[target].queue->pushBottom(fib);

    return 0;
}

int spawn_with_counter(void (*func)(void *), void *args, counter_t *c) {
    fiber_t *fib = (fiber_t *)alloc_fiber();
    if (fib == nullptr) return -1;

    fib->ctx                = {};
    fib->ctx.rip            = (void *)&fiber_entry;
    fib->ctx.rsp            = align_stack((char *)fib->user_stack, STACK_SIZE);
    fib->state              = RUNNABLE;
    fib->func               = func;
    fib->args               = args;
    fib->ran_on_worker      = -1;
    fib->completion_counter = c;
    fib->wakeup_target      = nullptr;

    total_spawned.fetch_add(1);

    int target = (worker_id >= 0) ? worker_id : 0;
    fib->born_on_worker = target;
    workers[target].queue->pushBottom(fib);

    return 0;
}

/* --------------------------- STATS --------------------------------- */

void reset_scheduler_stats() {
    steal_attempts.store(0);
    steal_successes.store(0);
    steal_aborts.store(0);
    for (int i = 0; i < MAX_FIBERS; i++) {
        fiber_pool[i].ran_on_worker  = -1;
        fiber_pool[i].born_on_worker = -1;
        fiber_pool[i].start_cycles   = 0;
        fiber_pool[i].end_cycles     = 0;
    }
}

void get_per_worker_stats(int local_out[], int stolen_out[], long long cycles_out[]) {
    for (int i = 0; i < num_workers; i++) {
        local_out[i] = stolen_out[i] = cycles_out[i] = 0;
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
    long attempts  = steal_attempts.load();
    long successes = steal_successes.load();
    long aborts    = steal_aborts.load();
    printf("=== Scheduler Stats ===\n");
    printf("steal attempts:  %ld\n", attempts);
    printf("steal successes: %ld\n", successes);
    printf("steal aborts:    %ld\n", aborts);
    if (attempts > 0) {
        printf("success rate:    %.1f%%\n", 100.0 * successes / attempts);
        printf("abort rate:      %.1f%%\n", 100.0 * aborts / attempts);
    }
    long local_count = 0, stolen_count = 0;
    long long local_cycles = 0, stolen_cycles = 0;
    for (int i = 0; i < MAX_FIBERS; i++) {
        fiber_t *f = &fiber_pool[i];
        if (f->ran_on_worker == -1) continue;
        long long cycles = f->end_cycles - f->start_cycles;
        if (f->born_on_worker == f->ran_on_worker) {
            local_count++;  local_cycles  += cycles;
        } else {
            stolen_count++; stolen_cycles += cycles;
        }
    }
    if (local_count  > 0) printf("avg local fiber cycles:  %lld\n", local_cycles  / local_count);
    if (stolen_count > 0) printf("avg stolen fiber cycles: %lld\n", stolen_cycles / stolen_count);
    printf("stolen fibers: %ld / %ld total\n", stolen_count, local_count + stolen_count);
}
