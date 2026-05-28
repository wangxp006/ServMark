#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define OPS_PER_THREAD 150000

typedef struct {
    pthread_rwlock_t rwlock;
    volatile int64_t data;
    volatile int ready;
    volatile int64_t total_ops;
    int nthreads;
} sync_rwlock_state_t;

typedef struct {
    sync_rwlock_state_t *s;
    int is_writer;
    int ops;
} rwlock_thread_arg_t;

static void *rwlock_worker(void *arg) {
    rwlock_thread_arg_t *a = (rwlock_thread_arg_t *)arg;
    sync_rwlock_state_t *s = a->s;
    while (!s->ready) ;
    for (int i = 0; i < a->ops; i++) {
        if (a->is_writer) {
            pthread_rwlock_wrlock(&s->rwlock);
            s->data++;
            pthread_rwlock_unlock(&s->rwlock);
        } else {
            pthread_rwlock_rdlock(&s->rwlock);
            __sync_fetch_and_add(&s->total_ops, 1);
            pthread_rwlock_unlock(&s->rwlock);
        }
    }
    return NULL;
}

static int sync_rwlock_init(void **state) {
    sync_rwlock_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    pthread_rwlock_init(&s->rwlock, NULL);
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 2) s->nthreads = 2;
    *state = s;
    return 0;
}

static int sync_rwlock_warmup(void *state) {
    sync_rwlock_state_t *s = (sync_rwlock_state_t *)state;
    for (int i = 0; i < 10000; i++) {
        pthread_rwlock_wrlock(&s->rwlock);
        s->data++;
        pthread_rwlock_unlock(&s->rwlock);
    }
    return 0;
}

static int sync_rwlock_measure(void *state, measurement_t *result) {
    sync_rwlock_state_t *s = (sync_rwlock_state_t *)state;
    struct timespec t0, t1;
    int n = s->nthreads;
    int nreaders = n * 3 / 4;
    int nwriters = n - nreaders;
    if (nreaders < 1) nreaders = 1;
    if (nwriters < 1) nwriters = 1;

    s->data = s->total_ops = 0;
    s->ready = 0;

    pthread_t *threads = malloc(n * sizeof(pthread_t));
    rwlock_thread_arg_t *args = malloc(n * sizeof(rwlock_thread_arg_t));

    for (int t = 0; t < n; t++) {
        args[t].s = s;
        args[t].is_writer = (t < nwriters) ? 1 : 0;
        args[t].ops = args[t].is_writer ? OPS_PER_THREAD / 3 : OPS_PER_THREAD;
        pthread_create(&threads[t], NULL, rwlock_worker, &args[t]);
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    s->ready = 1;

    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(threads); free(args);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)s->total_ops / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int sync_rwlock_cleanup(void *state) {
    sync_rwlock_state_t *s = (sync_rwlock_state_t *)state;
    pthread_rwlock_destroy(&s->rwlock);
    free(s);
    return 0;
}

benchmark_t bench_sync_rwlock = {
    .name = "sync-rwlock",
    .category = "C7",
    .description = "pthread_rwlock reader/writer contention (auto-scaled threads)",
    .tier = 1,
    .primary_metric_name = "ops/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = sync_rwlock_init,
    .warmup = sync_rwlock_warmup,
    .measure = sync_rwlock_measure,
    .cleanup = sync_rwlock_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_sync_rwlock);
