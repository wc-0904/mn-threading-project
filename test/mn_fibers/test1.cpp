#include <stdio.h>
#include "fiber.h"

void my_work(void *arg) {
    int id = *(int*)arg;
    printf("fiber %d running\n", id);
}

int main() {
    scheduler_init();

    int ids[16];
    for (int i = 0; i < 16; i++) {
        ids[i] = i;
        spawn(my_work, &ids[i]);
    }

    scheduler_run();
    printf("all done\n");
    return 0;
}