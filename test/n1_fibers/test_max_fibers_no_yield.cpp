// Spawn MAX_FIBERS fibers, each just prints and returns
#include <stdio.h>
#include "fiber.h"

static int completed = 0;

void my_work(void *arg) {
    int id = *(int*)arg;
    printf("  fiber %d running\n", id);
    completed++;
}

int main() {
    printf("[test_max_fibers_no_yield]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (completed == MAX_FIBERS) {
        printf("  PASS: all %d fibers completed\n", MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d completions, got %d\n", MAX_FIBERS, completed);
        return 1;
    }

    return 0;
}
