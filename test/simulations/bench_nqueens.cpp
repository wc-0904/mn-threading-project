#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include "fiber.h"

#define NQUEENS 14

static std::atomic<long> solution_count{0};

static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- validity check ---- */
static bool is_valid(int *board, int row, int col) {
    for (int i = 0; i < row; i++) {
        if (board[i] == col) return false;
        if (abs(board[i] - col) == abs(i - row)) return false;
    }
    return true;
}

/* ---- sequential DFS from a given board state ---- */
static long count_from(int *board, int row, int n) {
    if (row == n) return 1;
    long total = 0;
    for (int col = 0; col < n; col++) {
        if (is_valid(board, row, col)) {
            board[row] = col;
            total += count_from(board, row + 1, n);
        }
    }
    return total;
}

/* ---- serial baseline ---- */
static long run_serial() {
    int board[NQUEENS] = {};
    solution_count.store(0);
    long count = count_from(board, 0, NQUEENS);
    return count;
}

/* ---- fiber arg ---- */
typedef struct {
    int board[NQUEENS];
    int start_row;
} queens_arg_t;

/* ---- task pool — sized for prefix_depth rows ---- */
#define MAX_TASKS 2048
static queens_arg_t task_pool[MAX_TASKS];
static int          n_tasks;

/* ---- fiber function ---- */
void solve_subtree(void *arg) {
    queens_arg_t *a = (queens_arg_t*)arg;
    // copy board so recursive calls don't clobber shared state
    int board[NQUEENS];
    memcpy(board, a->board, sizeof(board));
    long count = count_from(board, a->start_row, NQUEENS);
    solution_count.fetch_add(count);
}

/* ---- enumerate tasks up to prefix_depth rows ---- */
static void enumerate_tasks(int *board, int row, int depth) {
    if (row == depth) {
        if (n_tasks < MAX_TASKS) {
            memcpy(task_pool[n_tasks].board, board, NQUEENS * sizeof(int));
            task_pool[n_tasks].start_row = depth;
            n_tasks++;
        }
        return;
    }
    for (int col = 0; col < NQUEENS; col++) {
        if (is_valid(board, row, col)) {
            board[row] = col;
            enumerate_tasks(board, row + 1, depth);
        }
    }
}

/* ---- spawner fiber ---- */
typedef struct { int depth; } spawner_arg_t;
static spawner_arg_t spawner_arg;

void nqueens_spawner(void *arg) {
    spawner_arg_t *a = (spawner_arg_t*)arg;
    (void)a;

    counter_t *c = create_counter(n_tasks);
    for (int i = 0; i < n_tasks; i++)
        spawn_with_counter(solve_subtree, &task_pool[i], c);
    wait_for_counter(c, 0);
    free_counter(c);
}

/* ---- run one configuration ---- */
static double run(int n_workers, int depth, double t1_ms, long expected) {
    // enumerate tasks for this depth
    n_tasks = 0;
    int board[NQUEENS] = {};
    enumerate_tasks(board, 0, depth);

    solution_count.store(0);
    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    spawn(nqueens_spawner, &spawner_arg);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    double ms      = (t1 - t0) / 1e6;
    long   sols    = solution_count.load();
    bool   correct = (sols == expected);

    printf("N=%-2d  depth=%-2d  tasks=%-5d  time=%7.1f ms  "
           "speedup=%5.2fx  solutions=%-8ld  steals=%-6ld  %s\n",
           n_workers, depth, n_tasks, ms,
           t1_ms > 0 ? t1_ms / ms : 1.0,
           sols, steal_successes.load(),
           correct ? "CORRECT" : "WRONG");

    return ms;
}

int main() {
    printf("=== Parallel N-Queens Benchmark ===\n");
    printf("N=%d queens\n\n", NQUEENS);

    // serial baseline
    printf("--- Serial baseline ---\n");
    long long t0 = now_ns();
    long expected = run_serial();
    long long t1 = now_ns();
    double ser_ms = (t1 - t0) / 1e6;
    printf("Serial: %.1f ms  solutions=%ld\n\n", ser_ms, expected);

    // ---- iteration 1: task granularity sweep at N=8 ----
    printf("--- Iteration 1: prefix depth sweep (workers=8) ---\n");
    printf("How many rows to pre-enumerate before switching to sequential?\n");
    int    best_depth = 1;
    double best_ms    = 1e9;
    for (int depth = 1; depth <= 3; depth++) {
        double ms = run(8, depth, 0, expected);
        if (ms < best_ms) { best_ms = ms; best_depth = depth; }
    }
    printf("  -> optimal depth: %d (%.1f ms)\n\n", best_depth, best_ms);

    // ---- iteration 2: worker scaling at optimal depth ----
    printf("--- Iteration 2: worker scaling (depth=%d) ---\n", best_depth);
    double t1_ms = 0;
    int wc[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) {
        double ms = run(wc[i], best_depth, t1_ms, expected);
        if (wc[i] == 1) t1_ms = ms;
    }

    printf("\n");
    printf("N-Queens has highly irregular task tree — branch pruning\n");
    printf("means some board prefixes have orders of magnitude more\n");
    printf("work than others. Work stealing handles this dynamically.\n");
    printf("Speedup is limited by the longest subtree (critical path).\n");

    return 0;
}