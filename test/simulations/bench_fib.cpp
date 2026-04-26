#include <stdio.h>
#include <time.h>
#include <atomic>
#include "fiber.h"

/* ---- configuration ---- */
#define FIB_N          40    // compute fib(40), answer is 102334155
#define SEQ_CUTOFF     20    // below this, compute sequentially
#define MAX_FIB_TASKS  50000 // max concurrent fib tasks in pool

/* ---- arg pool to avoid malloc ---- */
typedef struct {
    int  n;
    long result;
} fib_arg_t;

static fib_arg_t    fib_arg_pool[MAX_FIB_TASKS];
static std::atomic<int> fib_pool_idx{0};

static fib_arg_t *alloc_fib_arg(int n) {
    int idx = fib_pool_idx.fetch_add(1);
    fib_arg_pool[idx].n      = n;
    fib_arg_pool[idx].result = 0;
    return &fib_arg_pool[idx];
}

/* ---- sequential fibonacci for cutoff ---- */
static long seq_fib(int n) {
    if (n <= 1) return n;
    long a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        long tmp = a + b;
        a = b;
        b = tmp;
    }
    // add artificial work so leaf tasks take measurable time
    volatile long sink = 0;
    for (long i = 0; i < 500000L; i++) sink += i;
    return b;
}
/* ---- parallel fibonacci fiber ---- */
void par_fib(void *arg) {
    fib_arg_t *a = (fib_arg_t*)arg;

    if (a->n <= SEQ_CUTOFF) {
        a->result = seq_fib(a->n);
        return;
    }

    fib_arg_t *left  = alloc_fib_arg(a->n - 1);
    fib_arg_t *right = alloc_fib_arg(a->n - 2);

    counter_t *c = create_counter(2);
    spawn_with_counter(par_fib, left,  c);
    spawn_with_counter(par_fib, right, c);
    wait_for_counter(c, 0);
    free_counter(c);

    a->result = left->result + right->result;
}

/* ---- root spawner ---- */
static fib_arg_t root_arg;

void fib_spawner(void *arg) {
    root_arg.n      = FIB_N;
    root_arg.result = 0;
    par_fib(&root_arg);
}

/* ---- timing ---- */
static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- sequential baseline ---- */
static void run_sequential() {
    long long t0 = now_ns();
    long result = seq_fib(FIB_N);
    long long t1 = now_ns();
    double ms = (t1 - t0) / 1e6;
    printf("SEQUENTIAL         time=%.2f ms  result=%ld\n", ms, result);
}

/* ---- parallel run ---- */
static void run_parallel(int n_workers) {
    fib_pool_idx.store(0);
    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    spawn(fib_spawner, nullptr);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    double ms = (t1 - t0) / 1e6;
    printf("PARALLEL workers=%-2d time=%.2f ms  result=%ld  steals=%ld\n",
           n_workers, ms, root_arg.result, steal_successes.load());
}

int main() {
    printf("=== Parallel Fibonacci Benchmark ===\n");
    printf("fib(%d) with sequential cutoff at %d\n\n", FIB_N, SEQ_CUTOFF);

    // verify correctness first
    long expected = seq_fib(FIB_N);
    printf("Expected result: %ld\n\n", expected);

    run_sequential();
    printf("\n");

    int worker_counts[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++)
        run_parallel(worker_counts[i]);

    printf("\n");

    // verify result
    if (root_arg.result == expected)
        printf("  PASS: result correct\n");
    else
        printf("  FAIL: got %ld expected %ld\n", root_arg.result, expected);

    return 0;
}