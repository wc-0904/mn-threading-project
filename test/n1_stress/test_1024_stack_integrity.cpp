// 1024 fibers each with unique locals, yielding 20 times
// Checks that no fiber's stack gets corrupted by another
#include <stdio.h>
#include "fiber.h"

#define YIELDS 20

static int pass = 1;
static int checked = 0;

void my_work(void *arg) {
    int id = *(int*)arg;

    // each fiber has unique values based on its id
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
            printf("  FAIL: fiber %d locals corrupted on yield %d\n", id, i);
            pass = 0;
            return;
        }
    }
    checked++;
}

int main() {
    printf("[test_1024_stack_integrity]\n");
    scheduler_init();

    int ids[MAX_FIBERS];
    for (int i = 0; i < MAX_FIBERS; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();

    if (pass && checked == MAX_FIBERS) {
        printf("  PASS: all %d fibers' stack locals intact across %d yields\n",
            MAX_FIBERS, YIELDS);
    } else {
        printf("  FAIL: checked=%d, expected=%d\n", checked, MAX_FIBERS);
        return 1;
    }

    return 0;
}
