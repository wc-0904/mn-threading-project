// Each fiber computes a partial sum, yielding periodically
// Results are stored per-fiber and summed at the end
// Verifies computation correctness under parallel execution
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 64
#define WORK_PER_FIBER 500
#define YIELD_INTERVAL 25

static std::atomic<long> results[NUM_FIBERS];
static std::atomic<int> completed{0};

typedef struct {
    int id;
    int start;
    int count;
} range_args;

void compute_sum(void *arg) {
    range_args *r = (range_args*)arg;
    long sum = 0;
    for (int i = 0; i < r->count; i++) {
        sum += (r->start + i);
        if (i % YIELD_INTERVAL == 0) {
            yield();
        }
    }
    results[r->id].store(sum);
    completed.fetch_add(1);
}

int main() {
    printf("[test_cooperative_computation_mn]\n");
    scheduler_init();

    range_args args[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        args[i].id = i;
        args[i].start = i * WORK_PER_FIBER;
        args[i].count = WORK_PER_FIBER;
        results[i].store(-1);
        spawn(compute_sum, &args[i]);
    }

    scheduler_run();

    int pass = 1;
    long total = 0;
    for (int i = 0; i < NUM_FIBERS; i++) {
        long expected = 0;
        for (int j = 0; j < WORK_PER_FIBER; j++) {
            expected += (args[i].start + j);
        }
        if (results[i].load() != expected) {
            printf("  FAIL: fiber %d result = %ld, expected %ld\n",
                i, results[i].load(), expected);
            pass = 0;
        }
        total += results[i].load();
    }

    long n = (long)NUM_FIBERS * WORK_PER_FIBER;
    long expected_total = (n * (n - 1)) / 2;

    if (total != expected_total) {
        printf("  FAIL: total = %ld, expected %ld\n", total, expected_total);
        pass = 0;
    }

    if (pass && completed.load() == NUM_FIBERS) {
        printf("  PASS: computation correct, total = %ld\n", total);
    }

    return pass ? 0 : 1;
}
