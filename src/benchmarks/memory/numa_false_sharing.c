#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define ITERATIONS 50000000

typedef struct {
    volatile int64_t counter_a;
    volatile int64_t counter_b;
    char __pad1[64 - 2*sizeof(int64_t)];
    volatile int64_t counter_x;
    char __pad2[64];
    volatile int64_t counter_y;
    int nthreads;
} numa_false_sharing_state_t;

static void *fs_worker(void *arg) {
    volatile int64_t *counter = (volatile int64_t *)arg;
    int iters = ITERATIONS;
    for (int i = 0; i < iters; i++)
        __sync_fetch_and_add(counter, 1);
    return NULL;
}

static int numa_false_sharing_init(void **state) {
    numa_false_sharing_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->counter_a = s->counter_b = 0;
    s->counter_x = s->counter_y = 0;
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 2) s->nthreads = 2;
    *state = s;
    return 0;
}

static int numa_false_sharing_warmup(void *state) {
    numa_false_sharing_state_t *s = (numa_false_sharing_state_t *)state;
    for (int i = 0; i < 100000; i++)
        __sync_fetch_and_add(&s->counter_x, 1);
    return 0;
}

static int numa_false_sharing_measure(void *state, measurement_t *result) {
    numa_false_sharing_state_t *s = (numa_false_sharing_state_t *)state;
    struct timespec t0, t1;
    int n = s->nthreads;
    int iters_per_thread = ITERATIONS / n;
    pthread_t *threads = malloc(n * sizeof(pthread_t));

    /* All threads hammer the same cache line (counter_a) */
    s->counter_a = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int t = 0; t < n; t++)
        pthread_create(&threads[t], NULL, fs_worker, (void *)&s->counter_a);
    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    volatile int64_t sink = s->counter_a;
    __asm__ __volatile__("" : "+r"(sink));
    free(threads);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)n * iters_per_thread / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int numa_false_sharing_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_numa_false_sharing = {
    .name = "numa-false-sharing",
    .category = "C5",
    .description = "False sharing degradation (auto-scaled threads, shared cache line)",
    .tier = 1,
    .primary_metric_name = "increment/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = numa_false_sharing_init,
    .warmup = numa_false_sharing_warmup,
    .measure = numa_false_sharing_measure,
    .cleanup = numa_false_sharing_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_numa_false_sharing);
