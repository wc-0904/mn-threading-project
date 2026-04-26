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
static char    stack_pool[MAX_FIBERS][STACK_SIZE];
static bool    slot_used[MAX_FIBERS];

static worker_t workers[NUM_WORKERS];
static int      num_workers = NUM_WORKERS;
static int      next_worker = 0;

static __thread int worker_id;

static pthread_spinlock_t pool_lock;
static bool               pool_lock_initialized = false;
static std::atomic<int>   active_fibers{0};

// stealing state
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

/* --------------------------- FIBER EXECUTION ----------------------- */

static void fiber_entry() {
    fiber_t *fib = workers[worker_id].curr_running_fiber;
    fib->func(fib->args);
    worker_t *w = &workers[worker_id];
    fib->state = DONE;
    swap_context(&fib->ctx, &w->sched_context);
}

void yield() {
    worker_t *w = &workers[worker_id];
    w->curr_running_fiber->state = RUNNABLE;
    swap_context(&w->curr_running_fiber->ctx, &w->sched_context);
}

/* --------------------------- WORK STEALING ------------------------- */

static fiber_t *try_steal(int my_id) {
    if (!stealing_enabled) return nullptr;

    // random victim selection — avoids thundering herd on worker 0
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
    int backoff = 1;  // exponential backoff counter, resets on work found

    while (active_fibers.load() > 0) {
        // pop from local queue
        fiber_t *fib;
        while (true) {
            fib = w->queue->popBottom();
            if (fib != reinterpret_cast<fiber_t*>(-1)) break;
        }

        // steal if local queue empty
        if (fib == NULL) {
            fib = try_steal(id);
            if (fib == NULL) {
                int delay = backoff + (rand() % backoff);
                for (volatile int j = 0; j < delay * 100; j++);
                if (backoff < 1024) backoff <<= 1;
                continue;
            }
        }

        // got work — reset backoff
        backoff = 1;

        // run the fiber
        fib->state = RUNNING;
        w->curr_running_fiber = fib;
        fib->ran_on_worker = id;
        fib->start_cycles = __builtin_ia32_rdtsc();
        swap_context(&w->sched_context, &fib->ctx);
        fib->end_cycles = __builtin_ia32_rdtsc();

        if (fib->state == RUNNABLE) {
            w->queue->pushBottom(fib);
        } else if (fib->state == DONE) {
            free_fiber(fib);
            active_fibers.fetch_sub(1);
        }
    }

    return NULL;
}

/* --------------------------- PUBLIC API ---------------------------- */

void scheduler_init(int n_workers) {
    num_workers = n_workers;
    if (!pool_lock_initialized) {
        pthread_spin_init(&pool_lock, 0);
        pool_lock_initialized = true;
    }
    for (int i = 0; i < n_workers; i++) {
        workers[i].id = i;
        workers[i].queue = new deque();
        workers[i].curr_running_fiber = NULL;
    }
    for (int i = 0; i < MAX_FIBERS; i++)
        slot_used[i] = false;
    next_worker = 0;
}

void scheduler_run(int n_workers) {
    for (int i = 1; i < n_workers; i++)
        pthread_create(&workers[i].thread, NULL, &worker_loop, &workers[i].id);
    worker_loop(&workers[0].id);
    for (int i = 1; i < n_workers; i++)
        pthread_join(workers[i].thread, NULL);
}

int spawn(void (*func)(void *), void *args) {
    fiber_t *fib = (fiber_t *)alloc_fiber();
    if (fib == NULL) return -1;

    fib->ctx            = {};
    fib->ctx.rip        = (void *)&fiber_entry;
    fib->ctx.rsp        = align_stack((char *)fib->user_stack, STACK_SIZE);
    fib->state          = RUNNABLE;
    fib->func           = func;
    fib->args           = args;
    fib->born_on_worker = next_worker;
    fib->ran_on_worker  = -1;

    workers[next_worker].queue->pushBottom(fib);
    next_worker = (next_worker + 1) % num_workers;
    active_fibers.fetch_add(1);
    return 0;
}

void set_next_worker(int w) {
    next_worker = w;
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
        local_out[i]  = 0;
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
    if (local_count > 0)
        printf("avg local fiber cycles:  %lld\n", local_cycles / local_count);
    if (stolen_count > 0)
        printf("avg stolen fiber cycles: %lld\n", stolen_cycles / stolen_count);
    printf("stolen fibers: %ld / %ld total\n", stolen_count, local_count + stolen_count);
}
