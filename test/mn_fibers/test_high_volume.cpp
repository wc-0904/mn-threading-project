// 512 fibers each yielding 200 times = 102,400 context switches
// across 8 workers. Maximum pressure on deque operations
// and work stealing
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 512
#define YIELDS 200

static std::atomic<long> total_switches{0};
static std::atomic<int> completed{0};

void my_work(void *arg) {
    for (int i = 0; i < YIELDS; i++) {
        total_switches.fetch_add(1);
        yield();
    }
    completed.fetch_add(1);
}

int main() {
    printf("[test_high_volume_switches]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    long expected = (long)NUM_FIBERS * YIELDS;
    if (completed.load() == NUM_FIBERS && total_switches.load() == expected) {
        printf("  PASS: %d fibers, %ld context switches\n",
            NUM_FIBERS, total_switches.load());
    } else {
        printf("  FAIL: completed=%d (expected %d), switches=%ld (expected %ld)\n",
            completed.load(), NUM_FIBERS, total_switches.load(), expected);
        return 1;
    }

    return 0;
}
