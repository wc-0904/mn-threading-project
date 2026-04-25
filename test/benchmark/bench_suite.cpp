#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <atomic>
#include <unistd.h>
#include "fiber.h"

#define NUM_RUNS 5

static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void print_result(const char *label, uint64_t avg_ns) {
    printf("  %-30s %6llu ms  (%llu ns/task)\n",
        label,
        (unsigned long long)avg_ns / 1000000,
        (unsigned long long)avg_ns);
}

/* ══════════════════════════════════════════════════════════════════════
   SCENARIO 1: Ping-pong context switching
   Two tasks alternate control as fast as possible.
   Directly measures raw context switch cost.
   ══════════════════════════════════════════════════════════════════════ */

#define PINGPONG_ITERS 100000

static std::atomic<int> ping_turn{0};
static std::atomic<long long> pingpong_counter{0};

static void ping_fiber(void *arg) {
    for (int i = 0; i < PINGPONG_ITERS; i++) {
        while (ping_turn.load() != 0) yield();
        pingpong_counter.fetch_add(1);
        ping_turn.store(1);
        yield();
    }
}

static void pong_fiber(void *arg) {
    for (int i = 0; i < PINGPONG_ITERS; i++) {
        while (ping_turn.load() != 1) yield();
        pingpong_counter.fetch_add(1);
        ping_turn.store(0);
        yield();
    }
}

static uint64_t bench_pingpong_fiber() {
    ping_turn.store(0);
    pingpong_counter.store(0);
    scheduler_init();
    spawn(ping_fiber, NULL);
    spawn(pong_fiber, NULL);
    uint64_t start = now_ns();
    scheduler_run();
    uint64_t end = now_ns();
    return end - start;
}

static std::atomic<int> pthread_ping_turn{0};
static std::atomic<long long> pthread_pingpong_counter{0};

static void *pthread_ping(void *arg) {
    for (int i = 0; i < PINGPONG_ITERS; i++) {
        while (pthread_ping_turn.load() != 0) sched_yield();
        pthread_pingpong_counter.fetch_add(1);
        pthread_ping_turn.store(1);
    }
    return NULL;
}

static void *pthread_pong(void *arg) {
    for (int i = 0; i < PINGPONG_ITERS; i++) {
        while (pthread_ping_turn.load() != 1) sched_yield();
        pthread_pingpong_counter.fetch_add(1);
        pthread_ping_turn.store(0);
    }
    return NULL;
}

static uint64_t bench_pingpong_pthread() {
    pthread_ping_turn.store(0);
    pthread_pingpong_counter.store(0);
    pthread_t t1, t2;
    uint64_t start = now_ns();
    pthread_create(&t1, NULL, pthread_ping, NULL);
    pthread_create(&t2, NULL, pthread_pong, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   SCENARIO 2: Fan-out / fan-in
   One task spawns many short subtasks, waits for all to finish.
   Models real workloads like web servers, parallel RPC calls, etc.
   ══════════════════════════════════════════════════════════════════════ */

#define FANOUT_TASKS    512
#define FANOUT_YIELDS   20    // simulate "waiting" on IO with yields

static std::atomic<int> fanout_done{0};

static void fanout_fiber(void *arg) {
    // simulate IO-bound work: yield repeatedly instead of blocking
    for (int i = 0; i < FANOUT_YIELDS; i++) {
        yield();
    }
    fanout_done.fetch_add(1);
}

static uint64_t bench_fanout_fiber() {
    fanout_done.store(0);
    scheduler_init();
    static int dummy = 0;
    for (int i = 0; i < FANOUT_TASKS; i++) {
        spawn(fanout_fiber, &dummy);
    }
    uint64_t start = now_ns();
    scheduler_run();
    uint64_t end = now_ns();
    return end - start;
}

static std::atomic<int> pthread_fanout_done{0};

// pthread version: simulate same "IO wait" with sched_yield
static void *fanout_pthread(void *arg) {
    for (int i = 0; i < FANOUT_YIELDS; i++) {
        sched_yield();
    }
    pthread_fanout_done.fetch_add(1);
    return NULL;
}

static uint64_t bench_fanout_pthread_naive() {
    pthread_fanout_done.store(0);
    pthread_t threads[FANOUT_TASKS];
    uint64_t start = now_ns();
    for (int i = 0; i < FANOUT_TASKS; i++) {
        pthread_create(&threads[i], NULL, fanout_pthread, NULL);
    }
    for (int i = 0; i < FANOUT_TASKS; i++) {
        pthread_join(threads[i], NULL);
    }
    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   SCENARIO 3: Many short-lived tasks
   Each task does a tiny amount of work and exits.
   Tests scheduling throughput — how fast can you create and run tasks.
   ══════════════════════════════════════════════════════════════════════ */

#define SHORT_TASKS     1024
#define SHORT_WORK      50    // very little work per task

static std::atomic<long long> short_sum{0};

static void short_fiber(void *arg) {
    int id = *(int*)arg;
    long long partial = 0;
    for (int i = 0; i < SHORT_WORK; i++) {
        partial += id + i;
    }
    short_sum.fetch_add(partial);
}

static uint64_t bench_short_fiber() {
    short_sum.store(0);
    scheduler_init();
    static int ids[SHORT_TASKS];
    for (int i = 0; i < SHORT_TASKS; i++) {
        ids[i] = i;
        spawn(short_fiber, &ids[i]);
    }
    uint64_t start = now_ns();
    scheduler_run();
    uint64_t end = now_ns();
    return end - start;
}

static void *short_pthread(void *arg) {
    int id = *(int*)arg;
    long long partial = 0;
    for (int i = 0; i < SHORT_WORK; i++) {
        partial += id + i;
    }
    short_sum.fetch_add(partial);
    return NULL;
}

static uint64_t bench_short_pthread_naive() {
    short_sum.store(0);
    static int ids[SHORT_TASKS];
    pthread_t threads[SHORT_TASKS];
    uint64_t start = now_ns();
    for (int i = 0; i < SHORT_TASKS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, short_pthread, &ids[i]);
    }
    for (int i = 0; i < SHORT_TASKS; i++) {
        pthread_join(threads[i], NULL);
    }
    uint64_t end = now_ns();
    return end - start;
}

// pool version for short tasks
static std::atomic<int> short_pool_next{0};
static int short_pool_ids[SHORT_TASKS];

static void *short_pool_worker(void *arg) {
    while (true) {
        int idx = short_pool_next.fetch_add(1);
        if (idx >= SHORT_TASKS) break;
        int id = short_pool_ids[idx];
        long long partial = 0;
        for (int i = 0; i < SHORT_WORK; i++) partial += id + i;
        short_sum.fetch_add(partial);
    }
    return NULL;
}

static uint64_t bench_short_pthread_pool() {
    short_sum.store(0);
    short_pool_next.store(0);
    for (int i = 0; i < SHORT_TASKS; i++) short_pool_ids[i] = i;
    pthread_t workers[NUM_WORKERS];
    uint64_t start = now_ns();
    for (int i = 0; i < NUM_WORKERS; i++)
        pthread_create(&workers[i], NULL, short_pool_worker, NULL);
    for (int i = 0; i < NUM_WORKERS; i++)
        pthread_join(workers[i], NULL);
    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   SCENARIO 4: High-yield workload (simulated IO-bound concurrency)
   Tasks yield frequently — simulates async IO where threads would
   otherwise block. This is the fiber sweet spot.
   ══════════════════════════════════════════════════════════════════════ */

#define HIGHYIELD_TASKS     256
#define HIGHYIELD_ROUNDS    100   // yield between each round
#define HIGHYIELD_WORK      10    // work per round

static std::atomic<long long> highyield_sum{0};

static void highyield_fiber(void *arg) {
    int id = *(int*)arg;
    long long partial = 0;
    for (int r = 0; r < HIGHYIELD_ROUNDS; r++) {
        for (int i = 0; i < HIGHYIELD_WORK; i++) {
            partial += id + i;
        }
        yield();   // simulate waiting on IO between rounds
    }
    highyield_sum.fetch_add(partial);
}

static uint64_t bench_highyield_fiber() {
    highyield_sum.store(0);
    scheduler_init();
    static int ids[HIGHYIELD_TASKS];
    for (int i = 0; i < HIGHYIELD_TASKS; i++) {
        ids[i] = i;
        spawn(highyield_fiber, &ids[i]);
    }
    uint64_t start = now_ns();
    scheduler_run();
    uint64_t end = now_ns();
    return end - start;
}

// pthread pool version of the same workload
static std::atomic<int> highyield_pool_next{0};
static int highyield_pool_ids[HIGHYIELD_TASKS];

static void *highyield_pool_worker(void *arg) {
    while (true) {
        int idx = highyield_pool_next.fetch_add(1);
        if (idx >= HIGHYIELD_TASKS) break;
        int id = highyield_pool_ids[idx];
        long long partial = 0;
        for (int r = 0; r < HIGHYIELD_ROUNDS; r++) {
            for (int i = 0; i < HIGHYIELD_WORK; i++) partial += id + i;
            sched_yield();
        }
        highyield_sum.fetch_add(partial);
    }
    return NULL;
}

static uint64_t bench_highyield_pthread_pool() {
    highyield_sum.store(0);
    highyield_pool_next.store(0);
    for (int i = 0; i < HIGHYIELD_TASKS; i++) highyield_pool_ids[i] = i;
    pthread_t workers[NUM_WORKERS];
    uint64_t start = now_ns();
    for (int i = 0; i < NUM_WORKERS; i++)
        pthread_create(&workers[i], NULL, highyield_pool_worker, NULL);
    for (int i = 0; i < NUM_WORKERS; i++)
        pthread_join(workers[i], NULL);
    uint64_t end = now_ns();
    return end - start;
}

/* ══════════════════════════════════════════════════════════════════════
   RUNNER
   ══════════════════════════════════════════════════════════════════════ */

static uint64_t avg_runs(uint64_t (*bench)(), int runs) {
    uint64_t total = 0;
    for (int i = 0; i < runs; i++) total += bench();
    return total / runs;
}

int main() {
    printf("M:N Fiber Runtime vs Pthreads — Benchmark Suite\n");
    printf("Workers: %d  |  Runs averaged: %d\n\n", NUM_WORKERS, NUM_RUNS);

    /* ── Scenario 1 ── */
    printf("[ Scenario 1: Ping-pong context switching (%d alternations) ]\n",
           PINGPONG_ITERS * 2);
    printf("  Measures raw scheduling overhead per context switch.\n");
    uint64_t pp_fiber   = avg_runs(bench_pingpong_fiber,   NUM_RUNS);
    uint64_t pp_pthread = avg_runs(bench_pingpong_pthread, NUM_RUNS);
    print_result("Fiber M:N",      pp_fiber);
    print_result("Pthreads+sched_yield", pp_pthread);
    printf("  Fiber speedup: %.2fx\n\n", (double)pp_pthread / (double)pp_fiber);

    /* ── Scenario 2 ── */
    printf("[ Scenario 2: Fan-out (%d tasks x %d yields each) ]\n",
           FANOUT_TASKS, FANOUT_YIELDS);
    printf("  Simulates spawning many IO-bound subtasks (e.g. parallel RPC).\n");
    uint64_t fo_fiber  = avg_runs(bench_fanout_fiber,         NUM_RUNS);
    uint64_t fo_naive  = avg_runs(bench_fanout_pthread_naive, NUM_RUNS);
    print_result("Fiber M:N",         fo_fiber);
    print_result("Naive pthreads",    fo_naive);
    printf("  Fiber vs naive: %.2fx\n\n", (double)fo_naive / (double)fo_fiber);

    /* ── Scenario 3 ── */
    printf("[ Scenario 3: Short-lived tasks (%d tasks x %d work units) ]\n",
           SHORT_TASKS, SHORT_WORK);
    printf("  Measures task creation and teardown throughput.\n");
    uint64_t st_fiber  = avg_runs(bench_short_fiber,         NUM_RUNS);
    uint64_t st_naive  = avg_runs(bench_short_pthread_naive, NUM_RUNS);
    uint64_t st_pool   = avg_runs(bench_short_pthread_pool,  NUM_RUNS);
    print_result("Fiber M:N",      st_fiber);
    print_result("Naive pthreads", st_naive);
    print_result("Pthread pool",   st_pool);
    printf("  Fiber vs naive: %.2fx  |  Fiber vs pool: %.2fx\n\n",
           (double)st_naive / (double)st_fiber,
           (double)st_pool  / (double)st_fiber);

    /* ── Scenario 4 ── */
    printf("[ Scenario 4: High-yield IO simulation (%d tasks x %d yields) ]\n",
           HIGHYIELD_TASKS, HIGHYIELD_ROUNDS);
    printf("  Tasks yield frequently — simulates async IO concurrency.\n");
    uint64_t hy_fiber = avg_runs(bench_highyield_fiber,        NUM_RUNS);
    uint64_t hy_pool  = avg_runs(bench_highyield_pthread_pool, NUM_RUNS);
    print_result("Fiber M:N",    hy_fiber);
    print_result("Pthread pool", hy_pool);
    printf("  Fiber vs pool: %.2fx\n\n",
           (double)hy_pool / (double)hy_fiber);

    return 0;
}