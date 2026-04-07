#include <stdio.h>
#include "fiber.h"

void my_work(void *arg) {
    int id = *(int*)arg;
    printf("fiber %d running\n", id);
}

int main() {
    scheduler_init();

    int ids[] = {0, 1, 2};
    spawn(my_work, &ids[0]);
    spawn(my_work, &ids[1]);
    spawn(my_work, &ids[2]);

    scheduler_run();
    printf("all fibers done\n");
    return 0;
}