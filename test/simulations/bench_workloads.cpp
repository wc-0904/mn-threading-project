#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fiber.h"

#define NUM_FIBERS 1024
#define BALANCED_ITERS   100000L
#define HEAVY_ITERS     1000000L
#define LIGHT_ITERS       10000L

/* ---- workload functions ---- */

void balanced_work(void *arg) {
    volatile long sum = 0;
    for (long i = 0; i < BALANCED_ITERS; i++) sum += i;
}

typedef struct {
    int complexity;  // multiplier
} work_arg_t;

void heterogeneous_work(void *arg) {
    work_arg_t *w = (work_arg_t *)arg;
    volatile long sum = 0;
    long iters = w->complexity ? HEAVY_ITERS : LIGHT_ITERS;
    for (long i = 0; i < iters; i++) sum += i;
}

/* ---- benchmark helpers ---- */

static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void run_balanced(int n_workers, bool steal) {
    stealing_enabled = steal;
    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    for (int i = 0; i < NUM_FIBERS; i++)
        spawn(balanced_work, NULL);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    double ms = (t1 - t0) / 1e6;
    double throughput = NUM_FIBERS / (ms / 1000.0);
    printf("BALANCED  workers=%-2d stealing=%-5s  time=%.1f ms  throughput=%.0f fibers/s  ",
           n_workers, steal ? "on" : "off", ms, throughput);
    printf("steals=%ld aborts=%ld abort_rate=%.1f%%\n",
           steal_successes.load(), steal_aborts.load(),
           steal_attempts.load() > 0 ? 100.0 * steal_aborts / steal_attempts : 0.0);
}

static work_arg_t imbalanced_args[NUM_FIBERS];

static void run_imbalanced(int n_workers, bool steal) {

    for (int i = 0; i < NUM_FIBERS; i++) {
        imbalanced_args[i].complexity = (i % 10 == 0) ? 1 : 0;
    }

    // print workload composition
    int heavy = 0, light = 0;
    for (int i = 0; i < NUM_FIBERS; i++) {
        if (imbalanced_args[i].complexity) heavy++;
        else light++;
    }
    printf("workload: %d heavy fibers, %d light fibers\n", heavy, light);
    printf("heavy iters: %ld, light iters: %ld\n", HEAVY_ITERS, LIGHT_ITERS);

    stealing_enabled = steal;
    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    for (int i = 0; i < NUM_FIBERS; i++) {
        set_next_worker(0);
        spawn(heterogeneous_work, &imbalanced_args[i]);
    }
    scheduler_run(n_workers);
    long long t1 = now_ns();

    int per_worker_local[NUM_WORKERS] = {};
    int per_worker_stolen[NUM_WORKERS] = {};
    long long per_worker_cycles[NUM_WORKERS] = {};
    get_per_worker_stats(per_worker_local, per_worker_stolen, per_worker_cycles);

    for (int i = 0; i < NUM_WORKERS; i++) {
        printf("worker %d: local=%-4d stolen=%-4d cycles=%lld\n",
            i, per_worker_local[i], per_worker_stolen[i], per_worker_cycles[i]);
    }

    double ms = (t1 - t0) / 1e6;
    double throughput = NUM_FIBERS / (ms / 1000.0);
    printf("IMBALANCED workers=%-2d stealing=%-5s  time=%.1f ms  throughput=%.0f fibers/s  ",
           n_workers, steal ? "on" : "off", ms, throughput);
    printf("steals=%ld aborts=%ld abort_rate=%.1f%%\n",
           steal_successes.load(), steal_aborts.load(),
           steal_attempts.load() > 0 ? 100.0 * steal_aborts / steal_attempts : 0.0);
    print_scheduler_stats();
}

static void run_migration_test(int n_workers) {
    printf("\n--- Cache migration test (uniform work) ---\n");
    stealing_enabled = true;
    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    for (int i = 0; i < NUM_FIBERS; i++) {
        set_next_worker(0);  // all on worker 0 to force stealing
        spawn(balanced_work, NULL);  // uniform work
    }
    scheduler_run(n_workers);
    long long t1 = now_ns();

    print_scheduler_stats();
}

int main() {
    printf("=== Benchmark Suite ===\n");
    printf("NUM_FIBERS=%d  MAX_WORKERS=%d\n\n", NUM_FIBERS, NUM_WORKERS);

    int worker_counts[] = {1, 2, 4, 8};
    int num_counts = 4;

    printf("--- Balanced workload scaling ---\n");
    for (int i = 0; i < num_counts; i++) {
        run_balanced(worker_counts[i], true);
        run_balanced(worker_counts[i], false);
        printf("\n");
    }

    printf("--- Imbalanced workload scaling ---\n");
    for (int i = 0; i < num_counts; i++) {
        run_imbalanced(worker_counts[i], true);
        printf("\n");
    }

    printf("--- Cache migration test ---\n");
    run_migration_test(8);

    return 0;
}