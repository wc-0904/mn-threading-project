// Spawn all 1024 fibers, each prints and returns
#include <stdio.h>
#include "fiber.h"

static int completed = 0;

void my_work(void *arg) {
    completed++;
}

int main() {
    printf("[test_1024_fibers_no_yield]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        int ret = spawn(my_work, &ids[i]);
        if (ret != 0) {
            printf("  FAIL: could not spawn fiber %d\n", i);
            return 1;
        }
    }

    scheduler_run();

    if (completed == MAX_FIBERS) {
        printf("  PASS: all %d fibers completed\n", MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d, got %d\n", MAX_FIBERS, completed);
        return 1;
    }

    return 0;
}
