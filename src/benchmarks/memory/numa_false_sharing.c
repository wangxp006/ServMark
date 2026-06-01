#include "benchmark.h"
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
    if (n < 2) n = 2;
    int iters_per_thread = ITERATIONS / n;
    pthread_t *threads = malloc(n * sizeof(pthread_t));

    /* Phase A: false sharing — two threads write to counter_a and counter_b
     * which are on the SAME cache line (offset 0 and 8, both within 64 bytes) */
    s->counter_a = s->counter_b = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&threads[0], NULL, fs_worker, (void *)&s->counter_a);
    pthread_create(&threads[1], NULL, fs_worker, (void *)&s->counter_b);
    for (int t = 0; t < n; t++)
        if (t >= 2) pthread_create(&threads[t], NULL, fs_worker, (void *)&s->counter_a);
    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double false_sharing_elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double false_sharing_rate = (double)n * iters_per_thread / false_sharing_elapsed;

    /* Phase B: no false sharing — threads write to counter_x and counter_y
     * which are on DIFFERENT cache lines (offset 64 and 136 with padding) */
    s->counter_x = s->counter_y = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&threads[0], NULL, fs_worker, (void *)&s->counter_x);
    pthread_create(&threads[1], NULL, fs_worker, (void *)&s->counter_y);
    for (int t = 2; t < n; t++)
        pthread_create(&threads[t], NULL, fs_worker, (void *)&s->counter_x);
    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double no_sharing_elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double no_sharing_rate = (double)n * iters_per_thread / no_sharing_elapsed;

    free(threads);

    /* Report false sharing penalty: lower is worse (0.0 = severe, 1.0 = no penalty) */
    double penalty = (no_sharing_rate > 0) ? false_sharing_rate / no_sharing_rate : 1.0;

    double elapsed = false_sharing_elapsed + no_sharing_elapsed;
    memset(result, 0, sizeof(*result));
    result->primary_metric = penalty;  /* ratio: 1.0 = no false sharing impact */
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
    .description = "False sharing penalty ratio: same-cache-line vs separate-cache-line atomics",
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
