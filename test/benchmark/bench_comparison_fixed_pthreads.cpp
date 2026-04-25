#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <atomic>
#include "fiber.h"

#define NUM_TASKS       1024
#define WORK_PER_TASK   10000
#define NUM_RUNS        5

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
    long long partial = 0;
    for (int i = 0; i < WORK_PER_TASK; i++) {
        partial += id * WORK_PER_TASK + i;
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
   NAIVE PTHREAD VERSION (one pthread per task)
   ══════════════════════════════════════════════════════════════════════ */

static void *naive_pthread_task(void *arg) {
    int id = *(int*)arg;
    long long partial = 0;
    for (int i = 0; i < WORK_PER_TASK; i++) {
        partial += id * WORK_PER_TASK + i;
    }
    global_sum.fetch_add(partial);
    return NULL;
}

static uint64_t run_naive_pthread_bench() {
    global_sum.store(0);

    static int ids[NUM_TASKS];
    pthread_t threads[NUM_TASKS];

    uint64_t start = now_ns();
    for (int i = 0; i < NUM_TASKS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, naive_pthread_task, &ids[i]);
    }
    for (int i = 0; i < NUM_TASKS; i++) {
        pthread_join(threads[i], NULL);
    }
    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   PTHREAD POOL VERSION (fixed NUM_WORKERS threads, shared work queue)
   ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    int *task_ids;
    int  num_tasks;
    std::atomic<int> next_task;
} task_queue_t;

static task_queue_t pool_queue;

static void *pool_worker(void *arg) {
    while (true) {
        int idx = pool_queue.next_task.fetch_add(1);
        if (idx >= pool_queue.num_tasks) break;

        int id = pool_queue.task_ids[idx];
        long long partial = 0;
        for (int i = 0; i < WORK_PER_TASK; i++) {
            partial += id * WORK_PER_TASK + i;
        }
        global_sum.fetch_add(partial);
    }
    return NULL;
}

static uint64_t run_pool_pthread_bench() {
    global_sum.store(0);

    static int ids[NUM_TASKS];
    for (int i = 0; i < NUM_TASKS; i++) ids[i] = i;

    pool_queue.task_ids  = ids;
    pool_queue.num_tasks = NUM_TASKS;
    pool_queue.next_task.store(0);

    pthread_t workers[NUM_WORKERS];

    uint64_t start = now_ns();
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_create(&workers[i], NULL, pool_worker, NULL);
    }
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }
    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   MAIN
   ══════════════════════════════════════════════════════════════════════ */

int main() {
    printf("Benchmark: %d tasks, %d work iterations each\n", NUM_TASKS, WORK_PER_TASK);
    printf("Workers: %d | Runs averaged: %d\n\n", NUM_WORKERS, NUM_RUNS);

    uint64_t fiber_total = 0, naive_total = 0, pool_total = 0;
    long long fiber_sum = 0, naive_sum = 0, pool_sum = 0;

    printf("--- Fiber (M:N, %d workers) ---\n", NUM_WORKERS);
    for (int r = 0; r < NUM_RUNS; r++) {
        uint64_t t = run_fiber_bench();
        fiber_sum = global_sum.load();
        fiber_total += t;
        printf("  run %d: %llu ms\n", r, (unsigned long long)t / 1000000);
    }

    printf("\n--- Naive pthreads (1 thread per task) ---\n");
    for (int r = 0; r < NUM_RUNS; r++) {
        uint64_t t = run_naive_pthread_bench();
        naive_sum = global_sum.load();
        naive_total += t;
        printf("  run %d: %llu ms\n", r, (unsigned long long)t / 1000000);
    }

    printf("\n--- Pthread pool (%d workers, shared queue) ---\n", NUM_WORKERS);
    for (int r = 0; r < NUM_RUNS; r++) {
        uint64_t t = run_pool_pthread_bench();
        pool_sum = global_sum.load();
        pool_total += t;
        printf("  run %d: %llu ms\n", r, (unsigned long long)t / 1000000);
    }

    uint64_t fiber_avg = fiber_total / NUM_RUNS;
    uint64_t naive_avg = naive_total / NUM_RUNS;
    uint64_t pool_avg  = pool_total  / NUM_RUNS;

    printf("\nResult check:\n");
    printf("  fiber=%lld  naive=%lld  pool=%lld\n", fiber_sum, naive_sum, pool_sum);
    printf("  all match: %s\n",
        (fiber_sum == naive_sum && naive_sum == pool_sum) ? "YES" : "NO");

    printf("\n════════════════════════════════════════\n");
    printf("  Fiber (M:N)      avg: %6llu ms\n", (unsigned long long)fiber_avg / 1000000);
    printf("  Naive pthreads   avg: %6llu ms\n", (unsigned long long)naive_avg / 1000000);
    printf("  Pthread pool     avg: %6llu ms\n", (unsigned long long)pool_avg  / 1000000);
    printf("────────────────────────────────────────\n");
    printf("  Fiber vs naive:  %.2fx speedup\n", (double)naive_avg / (double)fiber_avg);
    printf("  Fiber vs pool:   %.2fx speedup\n", (double)pool_avg  / (double)fiber_avg);
    printf("════════════════════════════════════════\n");

    return 0;
}
