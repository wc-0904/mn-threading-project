// Each fiber stores unique values in local variables,
// yields multiple times, and checks they're preserved
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 5
#define YIELDS 10

static int pass = 1;

void my_work(void *arg) {
    int id = *(int*)arg;

    // unique locals per fiber
    volatile int a = id * 100 + 1;
    volatile int b = id * 100 + 2;
    volatile int c = id * 100 + 3;

    for (int i = 0; i < YIELDS; i++) {
        yield();

        // check locals survived the yield
        if (a != id * 100 + 1 ||
            b != id * 100 + 2 ||
            c != id * 100 + 3) {
            printf("  FAIL: fiber %d locals corrupted on yield %d\n", id, i);
            pass = 0;
            return;
        }
    }
}

int main() {
    printf("[test_stack_locals_across_yields]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (pass) {
        printf("  PASS: all fiber locals preserved across %d yields\n", YIELDS);
    }

    return pass ? 0 : 1;
}
