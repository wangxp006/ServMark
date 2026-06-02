#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

#define ITERATIONS 50000000

/*
 * False sharing penalty ratio benchmark.
 * Phase A: same cache line, Phase B: different cache lines.
 * Cache line size from sysconf() for correct padding on all architectures
 * (x86=64B, ARM64=64B, Apple M-series=128B, RISC-V=32-64B).
 */

typedef struct {
    _Atomic int64_t counter_a;
    _Atomic int64_t counter_b;
    _Atomic int64_t counter_x;
    _Atomic int64_t counter_y;
    int nthreads;
    long cache_line_size;
} numa_false_sharing_state_t;

static void *fs_worker(void *arg) {
    _Atomic int64_t *counter = (_Atomic int64_t *)arg;
    for (int i = 0; i < ITERATIONS; i++)
        atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
    return NULL;
}

static int numa_false_sharing_init(void **state) {
    long cls = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (cls <= 0) cls = 64;
    size_t alloc_sz = cls * 4;
    numa_false_sharing_state_t *s = aligned_alloc(cls,
        (alloc_sz + cls - 1) / cls * cls);
    if (!s) return -1;
    memset(s, 0, alloc_sz);
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 2) s->nthreads = 2;
    s->cache_line_size = cls;
    *state = s;
    return 0;
}

static int numa_false_sharing_warmup(void *state) {
    numa_false_sharing_state_t *s = (numa_false_sharing_state_t *)state;
    for (int i = 0; i < 100000; i++)
        atomic_fetch_add_explicit(&s->counter_x, 1, memory_order_relaxed);
    return 0;
}

static int numa_false_sharing_measure(void *state, measurement_t *result) {
    numa_false_sharing_state_t *s = (numa_false_sharing_state_t *)state;
    struct timespec t0, t1;
    int iters = ITERATIONS;
    pthread_t threads[2];
    long cls = s->cache_line_size;

    /* Phase A: counter_a and counter_b on same cache line → false sharing */
    atomic_store(&s->counter_a, 0); atomic_store(&s->counter_b, 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&threads[0], NULL, fs_worker, (void *)&s->counter_a);
    pthread_create(&threads[1], NULL, fs_worker, (void *)&s->counter_b);
    pthread_join(threads[0], NULL); pthread_join(threads[1], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double fs_elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double fs_rate = 2.0 * iters / fs_elapsed;

    /* Phase B: counters at offset cls and 2*cls → different cache lines */
    _Atomic int64_t *cx = (_Atomic int64_t *)((char *)s + cls);
    _Atomic int64_t *cy = (_Atomic int64_t *)((char *)s + 2 * cls);
    atomic_store(cx, 0); atomic_store(cy, 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&threads[0], NULL, fs_worker, (void *)cx);
    pthread_create(&threads[1], NULL, fs_worker, (void *)cy);
    pthread_join(threads[0], NULL); pthread_join(threads[1], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns_elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double ns_rate = 2.0 * iters / ns_elapsed;

    double penalty = (ns_rate > 0) ? fs_rate / ns_rate : 1.0;
    memset(result, 0, sizeof(*result));
    result->primary_metric = penalty;
    result->wall_seconds = fs_elapsed + ns_elapsed;
    return 0;
}

static int numa_false_sharing_cleanup(void *state) {
    free(state); return 0;
}

benchmark_t bench_numa_false_sharing = {
    .name = "numa-false-sharing",
    .category = "C5",
    .description = "False sharing ratio: same vs different cache line atomic throughput (runtime CLS)",
    .tier = 1, .primary_metric_name = "ratio", .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS, .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC, .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = numa_false_sharing_init, .warmup = numa_false_sharing_warmup,
    .measure = numa_false_sharing_measure, .cleanup = numa_false_sharing_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_numa_false_sharing);
