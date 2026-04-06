// Each fiber computes a portion of work (sum of range)
// yielding periodically, then stores result
// Verifies correctness of cooperative computation
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 5
#define WORK_PER_FIBER 200
#define YIELD_INTERVAL 10

static long results[NUM_FIBERS];

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
    results[r->id] = sum;
}

int main() {
    printf("[test_cooperative_computation]\n");
    scheduler_init();

    range_args args[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        args[i].id = i;
        args[i].start = i * WORK_PER_FIBER;
        args[i].count = WORK_PER_FIBER;
        results[i] = -1;
        spawn(compute_sum, &args[i]);
    }

    scheduler_run();

    // verify each result
    int pass = 1;
    long total = 0;
    for (int i = 0; i < NUM_FIBERS; i++) {
        long expected = 0;
        for (int j = 0; j < WORK_PER_FIBER; j++) {
            expected += (args[i].start + j);
        }
        if (results[i] != expected) {
            printf("  FAIL: fiber %d result = %ld, expected %ld\n",
                i, results[i], expected);
            pass = 0;
        }
        total += results[i];
    }

    // total should be sum of 0..999
    long expected_total = 0;
    int n = NUM_FIBERS * WORK_PER_FIBER;
    for (int i = 0; i < n; i++) {
        expected_total += i;
    }

    if (total != expected_total) {
        printf("  FAIL: total = %ld, expected %ld\n", total, expected_total);
        pass = 0;
    }

    if (pass) {
        printf("  PASS: cooperative computation correct, total = %ld\n", total);
    }

    return pass ? 0 : 1;
}
