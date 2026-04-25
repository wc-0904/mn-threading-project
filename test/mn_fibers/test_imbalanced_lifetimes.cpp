// Half the fibers finish instantly, half yield 100 times
// Workers that finish their fast fibers must steal slow ones
// from other workers to avoid going idle
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 64

static std::atomic<int> completed{0};

void fast_work(void *arg) {
    completed.fetch_add(1);
}

void slow_work(void *arg) {
    for (int i = 0; i < 100; i++) {
        yield();
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_imbalanced_lifetimes]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        if (i < NUM_FIBERS / 2) {
            spawn(fast_work, &ids[i]);
        } else {
            spawn(slow_work, &ids[i]);
        }
    }

    scheduler_run();

    if (completed.load() == NUM_FIBERS) {
        printf("  PASS: all %d fibers completed (fast + slow)\n", NUM_FIBERS);
    } else {
        printf("  FAIL: expected %d, got %d\n", NUM_FIBERS, completed.load());
        return 1;
    }

    return 0;
}
