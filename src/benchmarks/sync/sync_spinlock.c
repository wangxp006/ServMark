#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define OPS_PER_ITER 5000000

typedef struct {
    pthread_spinlock_t lock;
    volatile int64_t counter;
} sync_spinlock_state_t;

static int sync_spinlock_init(void **state) {
    sync_spinlock_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    pthread_spin_init(&s->lock, PTHREAD_PROCESS_PRIVATE);
    *state = s;
    return 0;
}

static int sync_spinlock_warmup(void *state) {
    sync_spinlock_state_t *s = (sync_spinlock_state_t *)state;
    for (int i = 0; i < 100000; i++) {
        pthread_spin_lock(&s->lock);
        s->counter++;
        pthread_spin_unlock(&s->lock);
    }
    return 0;
}

static int sync_spinlock_measure(void *state, measurement_t *result) {
    sync_spinlock_state_t *s = (sync_spinlock_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;

    s->counter = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < OPS_PER_ITER; i++) {
        pthread_spin_lock(&s->lock);
        s->counter++;
        sink += s->counter;
        pthread_spin_unlock(&s->lock);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)OPS_PER_ITER / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int sync_spinlock_cleanup(void *state) {
    sync_spinlock_state_t *s = (sync_spinlock_state_t *)state;
    pthread_spin_destroy(&s->lock);
    free(s);
    return 0;
}

benchmark_t bench_sync_spinlock = {
    .name = "sync-spinlock",
    .category = "C7",
    .description = "pthread_spinlock lock/unlock contention (auto-scaled threads)",
    .tier = 1,
    .primary_metric_name = "ops/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = sync_spinlock_init,
    .warmup = sync_spinlock_warmup,
    .measure = sync_spinlock_measure,
    .cleanup = sync_spinlock_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_sync_spinlock);
