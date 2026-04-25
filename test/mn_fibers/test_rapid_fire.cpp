// 1024 fibers that do no yielding — just run and finish
// Tests that termination works correctly when fibers
// complete faster than workers can steal
#include <stdio.h>
#include <atomic>
#include "fiber.h"

static std::atomic<int> completed{0};

void my_work(void *arg) {
    // do a tiny bit of work
    volatile int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += i;
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_rapid_fire_mn]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (completed.load() == MAX_FIBERS) {
        printf("  PASS: all %d short-lived fibers completed\n", MAX_FIBERS);
    } else {
        printf("  FAIL: expected %d, got %d\n", MAX_FIBERS, completed.load());
        return 1;
    }

    return 0;
}
