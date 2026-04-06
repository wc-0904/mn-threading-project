// All 1024 fibers each yield 50 times
// Total context switches: 1024 * 50 = 51,200
#include <stdio.h>
#include "fiber.h"

#define YIELDS_PER_FIBER 50

static int total_yields = 0;

void my_work(void *arg) {
    for (int i = 0; i < YIELDS_PER_FIBER; i++) {
        total_yields++;
        yield();
    }
}

int main() {
    printf("[test_1024_fibers_50_yields]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    int expected = MAX_FIBERS * YIELDS_PER_FIBER;
    if (total_yields == expected) {
        printf("  PASS: %d total yields across %d fibers\n", total_yields, MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d yields, got %d\n", expected, total_yields);
        return 1;
    }

    return 0;
}
