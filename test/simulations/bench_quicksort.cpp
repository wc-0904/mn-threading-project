// test/simulations/bench_quicksort.cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fiber.h"

#define ARRAY_SIZE   5000000
#define SEQ_CUTOFF   5000

static int arr[ARRAY_SIZE];
static int tmp[ARRAY_SIZE];  // scratch space

static long long now_ns() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}

/* ---- sequential sort for base case ---- */
static int cmp_int(const void *a, const void *b) {
    return *(int*)a - *(int*)b;
}

static void seq_sort(int *a, int n) {
    qsort(a, n, sizeof(int), cmp_int);
}

/* ---- parallel merge sort ---- */
typedef struct {
    int *arr;
    int *tmp;
    int  lo;
    int  hi;
} sort_arg_t;

static sort_arg_t sort_args[ARRAY_SIZE / SEQ_CUTOFF + 16];
static std::atomic<int> sort_arg_idx{0};

static sort_arg_t *alloc_sort_arg() {
    int idx = sort_arg_idx.fetch_add(1);
    return &sort_args[idx];
}

static void merge(int *arr, int *tmp, int lo, int mid, int hi) {
    memcpy(tmp + lo, arr + lo, (hi - lo) * sizeof(int));
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (tmp[i] <= tmp[j]) arr[k++] = tmp[i++];
        else                   arr[k++] = tmp[j++];
    }
    while (i < mid) arr[k++] = tmp[i++];
    while (j < hi)  arr[k++] = tmp[j++];
}

void par_sort(void *arg);

void par_sort(void *arg) {
    sort_arg_t *a = (sort_arg_t*)arg;
    int *ar = a->arr;
    int *tm = a->tmp;
    int  lo = a->lo;
    int  hi = a->hi;
    int  n  = hi - lo;

    if (n <= SEQ_CUTOFF) {
        seq_sort(ar + lo, n);
        return;
    }

    int mid = lo + n / 2;

    counter_t *c = create_counter(2);

    sort_arg_t *left  = alloc_sort_arg();
    sort_arg_t *right = alloc_sort_arg();
    *left  = {ar, tm, lo,  mid};
    *right = {ar, tm, mid, hi};

    spawn_with_counter(par_sort, left,  c);
    spawn_with_counter(par_sort, right, c);
    wait_for_counter(c, 0);
    free_counter(c);

    merge(ar, tm, lo, mid, hi);
}

/* ---- root spawner ---- */
static sort_arg_t root_arg;

void sort_spawner(void *arg) {
    root_arg = {arr, tmp, 0, ARRAY_SIZE};
    par_sort(&root_arg);
}

/* ---- verify ---- */
static bool verify() {
    for (int i = 1; i < ARRAY_SIZE; i++)
        if (arr[i] < arr[i-1]) return false;
    return true;
}

/* ---- benchmark ---- */
static void run(int n_workers, double t1_ms) {
    // reset array
    srand(42);
    for (int i = 0; i < ARRAY_SIZE; i++)
        arr[i] = rand();
    sort_arg_idx.store(0);

    reset_scheduler_stats();
    scheduler_init(n_workers);

    long long t0 = now_ns();
    spawn(sort_spawner, nullptr);
    scheduler_run(n_workers);
    long long t1 = now_ns();

    double ms = (t1 - t0) / 1e6;
    bool ok = verify();
    printf("N=%-2d  %7.1f ms  speedup=%5.2fx  steals=%-6ld  %s\n",
           n_workers, ms,
           t1_ms > 0 ? t1_ms / ms : 1.0,
           steal_successes.load(),
           ok ? "CORRECT" : "WRONG");
}

int main() {
    printf("=== Parallel Mergesort Benchmark ===\n");
    printf("N=%d elements  sequential cutoff=%d\n\n",
           ARRAY_SIZE, SEQ_CUTOFF);

    // serial baseline
    srand(42);
    for (int i = 0; i < ARRAY_SIZE; i++) arr[i] = rand();
    long long t0 = now_ns();
    seq_sort(arr, ARRAY_SIZE);
    long long t1 = now_ns();
    double ser_ms = (t1 - t0) / 1e6;
    printf("Serial qsort: %.1f ms\n\n", ser_ms);

    printf("Parallel mergesort:\n");
    double t1_ms = 0;
    int wc[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; i++) {
        if (i == 0) {
            // measure parallel N=1 as baseline for speedup
            srand(42);
            for (int j = 0; j < ARRAY_SIZE; j++) arr[j] = rand();
            sort_arg_idx.store(0);
            reset_scheduler_stats();
            scheduler_init(1);
            long long ts = now_ns();
            spawn(sort_spawner, nullptr);
            scheduler_run(1);
            long long te = now_ns();
            t1_ms = (te - ts) / 1e6;
            printf("N=%-2d  %7.1f ms  speedup=%5.2fx  steals=%-6ld  %s\n",
                   1, t1_ms, 1.0,
                   steal_successes.load(),
                   verify() ? "CORRECT" : "WRONG");
        } else {
            run(wc[i], t1_ms);
        }
    }

    return 0;
}