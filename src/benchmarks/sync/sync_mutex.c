#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define OPS_PER_THREAD 200000

typedef struct {
    pthread_mutex_t mutex;
    _Atomic int64_t counter;
    int nthreads;
    _Atomic int ready;
} sync_mutex_state_t;

typedef struct {
    sync_mutex_state_t *s;
} mutex_thread_arg_t;

static void *mutex_worker(void *arg) {
    mutex_thread_arg_t *a = (mutex_thread_arg_t *)arg;
    sync_mutex_state_t *s = a->s;
    while (!atomic_load_explicit(&s->ready, memory_order_acquire)) ;
    for (int i = 0; i < OPS_PER_THREAD; i++) {
        pthread_mutex_lock(&s->mutex);
        atomic_fetch_add_explicit(&s->counter, 1, memory_order_relaxed);
        pthread_mutex_unlock(&s->mutex);
    }
    return NULL;
}

static int sync_mutex_init(void **state) {
    sync_mutex_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    pthread_mutex_init(&s->mutex, NULL);
    atomic_init(&s->counter, 0);
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 2) s->nthreads = 2;
    *state = s;
    return 0;
}

static int sync_mutex_warmup(void *state) {
    sync_mutex_state_t *s = (sync_mutex_state_t *)state;
    for (int i = 0; i < 10000; i++) {
        pthread_mutex_lock(&s->mutex);
        atomic_fetch_add(&s->counter, 1);
        pthread_mutex_unlock(&s->mutex);
    }
    return 0;
}

static int sync_mutex_measure(void *state, measurement_t *result) {
    sync_mutex_state_t *s = (sync_mutex_state_t *)state;
    struct timespec t0, t1;
    int n = s->nthreads;

    atomic_store(&s->counter, 0);
    atomic_store(&s->ready, 0);

    pthread_t *threads = malloc(n * sizeof(pthread_t));
    mutex_thread_arg_t *args = malloc(n * sizeof(mutex_thread_arg_t));
    for (int t = 0; t < n; t++) {
        args[t].s = s;
        pthread_create(&threads[t], NULL, mutex_worker, &args[t]);
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    atomic_store_explicit(&s->ready, 1, memory_order_release);

    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(threads); free(args);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)atomic_load(&s->counter) / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int sync_mutex_cleanup(void *state) {
    sync_mutex_state_t *s = (sync_mutex_state_t *)state;
    pthread_mutex_destroy(&s->mutex);
    free(s);
    return 0;
}

benchmark_t bench_sync_mutex = {
    .name = "sync-mutex",
    .category = "C7",
    .description = "pthread_mutex lock/unlock contention (auto-scaled threads)",
    .tier = 1,
    .primary_metric_name = "ops/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = sync_mutex_init,
    .warmup = sync_mutex_warmup,
    .measure = sync_mutex_measure,
    .cleanup = sync_mutex_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_sync_mutex);
