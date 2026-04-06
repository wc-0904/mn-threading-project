#include <stdio.h>
#include "fiber.h"

void my_work(void *arg) {
    int id = *(int*)arg;
    for (int i = 0; i < 3; i++) {
        printf("fiber %d iteration %d\n", id, i);
        yield();
    }
}

int main() {
    scheduler_init();

    int ids[] = {0, 1, 2, 3, 4};
    spawn(my_work, &ids[0]);
    spawn(my_work, &ids[1]);
    spawn(my_work, &ids[2]);
    spawn(my_work, &ids[3]);
    spawn(my_work, &ids[4]);

    scheduler_run();
    printf("all fibers done\n");
    return 0;
}