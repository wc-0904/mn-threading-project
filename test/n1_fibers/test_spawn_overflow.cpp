// Try to spawn more than MAX_FIBERS and verify spawn returns -1
#include <stdio.h>
#include "fiber.h"

void my_work(void *arg) {
    yield();
}

int main() {
    printf("[test_spawn_overflow]\n");
    scheduler_init();

    int ids[MAX_FIBERS + 1];
    for (int i = 0; i <= MAX_FIBERS; i++) {
        ids[i] = i;
    }

    // fill the pool
    for (int i = 0; i < MAX_FIBERS; i++) {
        int ret = spawn(my_work, &ids[i]);
        if (ret != 0) {
            printf("  FAIL: spawn failed on fiber %d before pool full\n", i);
            return 1;
        }
    }

    // this one should fail
    int ret = spawn(my_work, &ids[MAX_FIBERS]);
    if (ret == -1) {
        printf("  PASS: spawn correctly returned -1 when pool full\n");
    } else {
        printf("  FAIL: spawn returned %d instead of -1 when pool full\n", ret);
        return 1;
    }

    // clean up — run the fibers so they complete
    scheduler_run();
    return 0;
}
