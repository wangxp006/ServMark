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
    numa_false_sharing_state_t *s = aligned_alloc(64,
        (sizeof(*s) + 63) / 64 * 64);
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
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
    int iters_per_thread = ITERATIONS;
    pthread_t threads[2];

    /* Phase A: false sharing — 2 threads write to counter_a and counter_b
     * which are on the SAME cache line (offset 0 and 8, both within 64 bytes).
     * Using exactly 2 threads isolates false sharing from true contention. */
    s->counter_a = s->counter_b = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&threads[0], NULL, fs_worker, (void *)&s->counter_a);
    pthread_create(&threads[1], NULL, fs_worker, (void *)&s->counter_b);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double fs_elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double fs_rate = 2.0 * iters_per_thread / fs_elapsed;

    /* Phase B: no false sharing — threads write to counter_x and counter_y
     * which are on DIFFERENT cache lines (offset 64 and 136 with padding). */
    s->counter_x = s->counter_y = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&threads[0], NULL, fs_worker, (void *)&s->counter_x);
    pthread_create(&threads[1], NULL, fs_worker, (void *)&s->counter_y);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double ns_elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double ns_rate = 2.0 * iters_per_thread / ns_elapsed;

    /* Report false sharing penalty: 1.0 = no impact, <1.0 = slowdown */
    double penalty = (ns_rate > 0) ? fs_rate / ns_rate : 1.0;

    memset(result, 0, sizeof(*result));
    result->primary_metric = penalty;
    result->wall_seconds = fs_elapsed + ns_elapsed;
    return 0;
}

static int numa_false_sharing_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_numa_false_sharing = {
    .name = "numa-false-sharing",
    .category = "C5",
    .description = "False sharing ratio: same-cache-line vs different-cache-line atomic throughput (2 threads)",
    .tier = 1,
    .primary_metric_name = "ratio",
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
