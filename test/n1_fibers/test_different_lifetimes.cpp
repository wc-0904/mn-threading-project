// Fibers that finish at different times
// Some yield many times, some yield few times
// Verifies the scheduler handles fibers completing at different rates
#include <stdio.h>
#include "fiber.h"

#define NUM_FIBERS 5

static int completed = 0;

// arg encodes both id and number of iterations
typedef struct {
    int id;
    int iters;
} work_args;

void my_work(void *arg) {
    work_args *w = (work_args*)arg;
    for (int i = 0; i < w->iters; i++) {
        printf("  fiber %d iteration %d/%d\n", w->id, i + 1, w->iters);
        yield();
    }
    completed++;
}

int main() {
    printf("[test_different_lifetimes]\n");
    scheduler_init();

    work_args args[NUM_FIBERS] = {
        {0, 1},   // finishes fast
        {1, 5},
        {2, 2},
        {3, 8},   // finishes last
        {4, 3},
    };

    for (int i = 0; i < NUM_FIBERS; i++) {
        spawn(my_work, &args[i]);
    }

    scheduler_run();

    if (completed == NUM_FIBERS) {
        printf("  PASS: all %d fibers completed with different lifetimes\n", NUM_FIBERS);
    } else {
        printf("  FAIL: expected %d completions, got %d\n", NUM_FIBERS, completed);
        return 1;
    }

    return 0;
}
