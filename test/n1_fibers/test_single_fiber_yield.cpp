// Edge case: only one fiber in the scheduler
// It yields and should get re-scheduled back to itself
#include <stdio.h>
#include "fiber.h"

static int iterations = 0;

void my_work(void *arg) {
    for (int i = 0; i < 5; i++) {
        iterations++;
        printf("  single fiber iteration %d\n", i);
        yield();
    }
}

int main() {
    printf("[test_single_fiber_yield]\n");
    scheduler_init();

    spawn(my_work, NULL);
    scheduler_run();

    if (iterations == 5) {
        printf("  PASS: single fiber yielded and resumed 5 times\n");
    } else {
        printf("  FAIL: expected 5 iterations, got %d\n", iterations);
        return 1;
    }

    return 0;
}
