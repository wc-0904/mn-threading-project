#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <atomic>
#include "fiber.h"

#define NUM_TASKS       1024
#define WORK_PER_TASK   10000   // iterations of fake work per task
#define NUM_RUNS        5       // average over multiple runs

/* ─────────────────────────── timing helper ─────────────────────────── */

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* ─────────────────────────── shared results ─────────────────────────── */

static std::atomic<long long> global_sum{0};

/* ══════════════════════════════════════════════════════════════════════
   FIBER VERSION
   ══════════════════════════════════════════════════════════════════════ */

static void fiber_task(void *arg) {
    int id = *(int*)arg;

    // do some actual work — compute a partial sum
    long long partial = 0;
    for (int i = 0; i < WORK_PER_TASK; i++) {
        partial += id * WORK_PER_TASK + i;
        // yield every 1000 iterations to simulate cooperative multitasking
        if (i % 1000 == 0) yield();
    }
    global_sum.fetch_add(partial);
}

static uint64_t run_fiber_bench() {
    global_sum.store(0);
    scheduler_init();

    static int ids[NUM_TASKS];
    for (int i = 0; i < NUM_TASKS; i++) {
        ids[i] = i;
        spawn(fiber_task, &ids[i]);
    }

    uint64_t start = now_ns();
    scheduler_run();
    uint64_t end = now_ns();

    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   PTHREAD VERSION
   ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    int id;
} pthread_arg_t;

static void *pthread_task(void *arg) {
    int id = ((pthread_arg_t*)arg)->id;

    long long partial = 0;
    for (int i = 0; i < WORK_PER_TASK; i++) {
        partial += id * WORK_PER_TASK + i;
    }
    global_sum.fetch_add(partial);
    return NULL;
}

static uint64_t run_pthread_bench() {
    global_sum.store(0);

    // spawn NUM_TASKS pthreads (same workload, but kernel threads)
    pthread_t threads[NUM_TASKS];
    pthread_arg_t args[NUM_TASKS];

    uint64_t start = now_ns();

    for (int i = 0; i < NUM_TASKS; i++) {
        args[i].id = i;
        pthread_create(&threads[i], NULL, pthread_task, &args[i]);
    }
    for (int i = 0; i < NUM_TASKS; i++) {
        pthread_join(threads[i], NULL);
    }

    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   MAIN
   ══════════════════════════════════════════════════════════════════════ */

int main() {
    printf("Benchmark: %d tasks, %d work iterations each\n", NUM_TASKS, WORK_PER_TASK);
    printf("Fiber workers: %d | Runs: %d\n\n", NUM_WORKERS, NUM_RUNS);

    // ── fiber runs ──
    uint64_t fiber_total = 0;
    long long fiber_sum = 0;
    for (int r = 0; r < NUM_RUNS; r++) {
        uint64_t t = run_fiber_bench();
        fiber_sum = global_sum.load();
        fiber_total += t;
        printf("  fiber run %d: %llu ms\n", r, (unsigned long long)t / 1000000);
    }
    uint64_t fiber_avg = fiber_total / NUM_RUNS;

    // ── pthread runs ──
    uint64_t pthread_total = 0;
    long long pthread_sum = 0;
    for (int r = 0; r < NUM_RUNS; r++) {
        uint64_t t = run_pthread_bench();
        pthread_sum = global_sum.load();
        pthread_total += t;
        printf("  pthread run %d: %llu ms\n", r, (unsigned long long)t / 1000000);
    }
    uint64_t pthread_avg = pthread_total / NUM_RUNS;

    // sanity check — both should compute the same total
    printf("\nResult check: fiber_sum=%lld  pthread_sum=%lld  match=%s\n",
           fiber_sum, pthread_sum, (fiber_sum == pthread_sum) ? "YES" : "NO");

    printf("\n─────────────────────────────────────\n");
    printf("  Fiber   avg: %llu ms\n", (unsigned long long)fiber_avg / 1000000);
    printf("  Pthread avg: %llu ms\n", (unsigned long long)pthread_avg / 1000000);
    printf("  Speedup:     %.2fx\n", (double)pthread_avg / (double)fiber_avg);
    printf("─────────────────────────────────────\n");

    return 0;
}
