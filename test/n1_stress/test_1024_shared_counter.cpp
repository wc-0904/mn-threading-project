// 1024 fibers all incrementing a shared counter 100 times each
// Total: 102,400 increments and 102,400 context switches
#include <stdio.h>
#include "fiber.h"

#define INCREMENTS 100

static long shared_counter = 0;

void my_work(void *arg) {
    for (int i = 0; i < INCREMENTS; i++) {
        shared_counter++;
        yield();
    }
}

int main() {
    printf("[test_1024_shared_counter]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    long expected = (long)MAX_FIBERS * INCREMENTS;
    if (shared_counter == expected) {
        printf("  PASS: counter = %ld (%d fibers x %d increments)\n",
            shared_counter, MAX_FIBERS, INCREMENTS);
    } else {
        printf("  FAIL: counter = %ld, expected %ld\n", shared_counter, expected);
        return 1;
    }

    return 0;
}
