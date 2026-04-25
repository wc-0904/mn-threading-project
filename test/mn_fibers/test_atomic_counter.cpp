// All fibers atomically increment a shared counter
// Verifies no lost updates under parallel execution
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 256
#define INCREMENTS 50

static std::atomic<long> shared_counter{0};
static std::atomic<int> completed{0};

void my_work(void *arg) {
    for (int i = 0; i < INCREMENTS; i++) {
        shared_counter.fetch_add(1);
        yield();
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_atomic_counter_mn]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    long expected = (long)NUM_FIBERS * INCREMENTS;
    if (completed.load() == NUM_FIBERS && shared_counter.load() == expected) {
        printf("  PASS: counter = %ld (%d fibers x %d increments)\n",
            shared_counter.load(), NUM_FIBERS, INCREMENTS);
    } else {
        printf("  FAIL: completed=%d counter=%ld (expected %d, %ld)\n",
            completed.load(), shared_counter.load(), NUM_FIBERS, expected);
        return 1;
    }

    return 0;
}
