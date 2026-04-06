// Each fiber yields 100 times, verifying the scheduler
// handles many round trips without corruption
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 5
#define YIELDS_PER_FIBER 100

static int yield_counts[NUM_FIBERS];

void my_work(void *arg) {
    int id = *(int*)arg;
    for (int i = 0; i < YIELDS_PER_FIBER; i++) {
        yield_counts[id]++;
        yield();
    }
}

int main() {
    printf("[test_many_yields]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        yield_counts[i] = 0;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    int pass = 1;
    for (int i = 0; i < NUM_FIBERS; i++) {
        if (yield_counts[i] != YIELDS_PER_FIBER) {
            printf("  FAIL: fiber %d yielded %d times, expected %d\n",
                i, yield_counts[i], YIELDS_PER_FIBER);
            pass = 0;
        }
    }

    if (pass) {
        printf("  PASS: all %d fibers completed %d yields each\n",
            NUM_FIBERS, YIELDS_PER_FIBER);
    }

    return pass ? 0 : 1;
}
