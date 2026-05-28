#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define OPS_PER_ITER 100000

typedef struct {
    sem_t sem;
    volatile int ready, done;
    volatile int64_t wakeups;
    int nwaiters;
} sync_semaphore_state_t;

static void *sem_waiter(void *arg) {
    sync_semaphore_state_t *s = (sync_semaphore_state_t *)arg;
    while (!s->ready) ;
    while (!s->done) sem_wait(&s->sem);
    return NULL;
}

static int sync_semaphore_init(void **state) {
    sync_semaphore_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    sem_init(&s->sem, 0, 0);
    s->nwaiters = SSB_NUM_CPUS();
    if (s->nwaiters < 1) s->nwaiters = 1;
    *state = s;
    return 0;
}

static int sync_semaphore_warmup(void *state) {
    sync_semaphore_state_t *s = (sync_semaphore_state_t *)state;
    for (int i = 0; i < 1000; i++) {
        sem_post(&s->sem);
        sem_wait(&s->sem);
    }
    return 0;
}

static int sync_semaphore_measure(void *state, measurement_t *result) {
    sync_semaphore_state_t *s = (sync_semaphore_state_t *)state;
    struct timespec t0, t1;
    int n = s->nwaiters;

    s->ready = s->done = 0;
    s->wakeups = 0;

    pthread_t *waiters = malloc(n * sizeof(pthread_t));
    for (int t = 0; t < n; t++)
        pthread_create(&waiters[t], NULL, sem_waiter, s);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    s->ready = 1;

    for (int i = 0; i < OPS_PER_ITER; i++) {
        sem_post(&s->sem);
        s->wakeups++;
    }

    s->done = 1;
    for (int t = 0; t < n; t++) sem_post(&s->sem);
    for (int t = 0; t < n; t++) pthread_join(waiters[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(waiters);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)s->wakeups / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int sync_semaphore_cleanup(void *state) {
    sync_semaphore_state_t *s = (sync_semaphore_state_t *)state;
    sem_destroy(&s->sem);
    free(s);
    return 0;
}

benchmark_t bench_sync_semaphore = {
    .name = "sync-semaphore",
    .category = "C7",
    .description = "sem_post/wait wakeup throughput (auto-scaled waiters)",
    .tier = 1,
    .primary_metric_name = "wakeups/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = sync_semaphore_init,
    .warmup = sync_semaphore_warmup,
    .measure = sync_semaphore_measure,
    .cleanup = sync_semaphore_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_sync_semaphore);
