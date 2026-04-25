#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#define TOTAL_SWITCHES (1024 * 50)

sem_t a, b;

void *thread_fn(void *arg) {
    for (int i = 0; i < TOTAL_SWITCHES / 2; i++) {
        sem_wait(&a);
        sem_post(&b);
    }
    return NULL;
}

int main() {
    sem_init(&a, 0, 0);
    sem_init(&b, 0, 0);
    pthread_t t;
    pthread_create(&t, NULL, thread_fn, NULL);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < TOTAL_SWITCHES / 2; i++) {
        sem_post(&a);
        sem_wait(&b);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    pthread_join(t, NULL);

    long ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("total switches: %d\n", TOTAL_SWITCHES);
    printf("total time:     %ld ns\n", ns);
    printf("per switch:     %.1f ns\n", (double)ns / TOTAL_SWITCHES);
    return 0;
}