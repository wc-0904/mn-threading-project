// Each fiber stores unique locals, yields many times,
// checks they survive. Fibers may migrate between workers
// via stealing — stack must remain intact regardless
#include <stdio.h>
#include <atomic>
#include "fiber.h"

#define NUM_FIBERS 128
#define YIELDS 30

static std::atomic<int> passed{0};
static std::atomic<int> failed{0};

void my_work(void *arg) {
    int id = *(int*)arg;

    volatile long a = (long)id * 1000 + 1;
    volatile long b = (long)id * 1000 + 2;
    volatile long c = (long)id * 1000 + 3;
    volatile long d = (long)id * 1000 + 4;

    for (int i = 0; i < YIELDS; i++) {
        yield();

        if (a != (long)id * 1000 + 1 ||
            b != (long)id * 1000 + 2 ||
            c != (long)id * 1000 + 3 ||
            d != (long)id * 1000 + 4) {
            failed.fetch_add(1);
            return;
        }
    }
    passed.fetch_add(1);
}

int main() {
    printf("[test_stack_integrity_mn]\n");
    scheduler_init();

    int ids[NUM_FIBERS];
    for (int i = 0; i < NUM_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (passed.load() == NUM_FIBERS && failed.load() == 0) {
        printf("  PASS: all %d fibers' locals intact across %d yields\n",
            NUM_FIBERS, YIELDS);
    } else {
        printf("  FAIL: passed=%d failed=%d (expected %d, 0)\n",
            passed.load(), failed.load(), NUM_FIBERS);
        return 1;
    }

    return 0;
}
