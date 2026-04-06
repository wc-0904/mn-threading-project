// 1024 fibers where each fiber yields (id % 20) times
// Some finish immediately, some yield 19 times
// Tests scheduler handling mass completions at different rates
#include <stdio.h>
#include "fiber.h"

static int completed = 0;

void my_work(void *arg) {
    int id = *(int*)arg;
    int iters = id % 20;
    for (int i = 0; i < iters; i++) {
        yield();
    }
    completed++;
}

int main() {
    printf("[test_1024_varying_lifetimes]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (completed == MAX_FIBERS) {
        printf("  PASS: all %d fibers completed with varying lifetimes\n", MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d completions, got %d\n", MAX_FIBERS, completed);
        return 1;
    }

    return 0;
}
