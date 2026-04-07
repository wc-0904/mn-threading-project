// All fibers increment a shared counter, yielding between increments
// Since N:1 is single-threaded, no races possible
// Verifies the counter reaches the expected total
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 10
#define INCREMENTS_PER_FIBER 50

static int shared_counter = 0;

void my_work(void *arg) {
    for (int i = 0; i < INCREMENTS_PER_FIBER; i++) {
        shared_counter++;
        yield();
    }
}

int main() {
    printf("[test_shared_counter]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    int expected = NUM_FIBERS * INCREMENTS_PER_FIBER;
    if (shared_counter == expected) {
        printf("  PASS: counter = %d (expected %d)\n", shared_counter, expected);
    } else {
        printf("  FAIL: counter = %d, expected %d\n", shared_counter, expected);
        return 1;
    }

    return 0;
}
