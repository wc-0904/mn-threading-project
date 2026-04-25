// Fibers doing deep recursion across multiple workers
// Verifies stacks don't collide when fibers migrate
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 32
#define DEPTH 150

static std::atomic<int> passed{0};

int recursive_sum(int n) {
    if (n <= 0) return 0;
    volatile int val = n;
    return val + recursive_sum(n - 1);
}

void my_work(void *arg) {
    int result = recursive_sum(DEPTH);
    int expected = (DEPTH * (DEPTH + 1)) / 2;
    if (result == expected) {
        passed.fetch_add(1);
    }
}

int main() {
    printf("[test_deep_stack_mn]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (passed.load() == NUM_FIBERS) {
        printf("  PASS: %d fibers each recursed %d deep correctly\n",
            NUM_FIBERS, DEPTH);
    } else {
        printf("  FAIL: only %d/%d fibers got correct result\n",
            passed.load(), NUM_FIBERS);
        return 1;
    }

    return 0;
}
