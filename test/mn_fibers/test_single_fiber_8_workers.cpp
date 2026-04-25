// Edge case: only 1 fiber with 8 workers
// 7 workers have nothing and must properly terminate
// after the 1 fiber completes
#include <stdio.h>
#include <atomic>
#include "fiber.h"

static std::atomic<int> completed{0};

void my_work(void *arg) {
    for (int i = 0; i < 10; i++) {
        yield();
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_single_fiber_8_workers]\n");
    scheduler_init();

    int id = 0;
    spawn(my_work, &id);

    scheduler_run();

    if (completed.load() == 1) {
        printf("  PASS: single fiber completed with 8 workers\n");
    } else {
        printf("  FAIL: expected 1, got %d\n", completed.load());
        return 1;
    }

    return 0;
}
