// Max fibers, each yielding 20 times
// 1024 * 20 = 20,480 context switches across 8 workers
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define YIELDS_PER_FIBER 20

static std::atomic<int> total_yields{0};
static std::atomic<int> completed{0};

void my_work(void *arg) {
    for (int i = 0; i < YIELDS_PER_FIBER; i++) {
        total_yields.fetch_add(1);
        yield();
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_1024_fibers_mn]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    int expected_yields = MAX_FIBERS * YIELDS_PER_FIBER;
    if (completed.load() == MAX_FIBERS && total_yields.load() == expected_yields) {
        printf("  PASS: %d fibers, %d total yields\n",
            completed.load(), total_yields.load());
    } else {
        printf("  FAIL: completed=%d (expected %d), yields=%d (expected %d)\n",
            completed.load(), MAX_FIBERS, total_yields.load(), expected_yields);
        return 1;
    }

    return 0;
}
