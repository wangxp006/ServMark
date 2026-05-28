#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <numa.h>
#include <pthread.h>
#include <unistd.h>

#define BUF_MB 128
#define BUF_SIZE (BUF_MB * 1024 * 1024)

typedef struct {
    char *buffer;
    int node;
    int nthreads;
} numa_bandwidth_state_t;

typedef struct {
    char *buf;
    size_t start, end;
} bw_thread_arg_t;

static void *bw_worker(void *arg) {
    bw_thread_arg_t *a = (bw_thread_arg_t *)arg;
    volatile char sink = 0;
    for (size_t i = a->start; i < a->end; i += 8)
        sink += a->buf[i];
    __asm__ __volatile__("" : "+r"(sink));
    return NULL;
}

static int numa_bandwidth_init(void **state) {
    numa_bandwidth_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (numa_available() < 0) { free(s); return -1; }
    int max_node = numa_max_node();
    s->node = max_node > 0 ? 1 : 0;
    s->buffer = numa_alloc_onnode(BUF_SIZE, s->node);
    if (!s->buffer) { free(s); return -1; }
    memset(s->buffer, 0xAB, BUF_SIZE);
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 1) s->nthreads = 1;
    *state = s;
    return 0;
}

static int numa_bandwidth_warmup(void *state) {
    numa_bandwidth_state_t *s = (numa_bandwidth_state_t *)state;
    volatile char sink = 0;
    for (size_t i = 0; i < BUF_SIZE / 10; i += 8)
        sink += s->buffer[i];
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int numa_bandwidth_measure(void *state, measurement_t *result) {
    numa_bandwidth_state_t *s = (numa_bandwidth_state_t *)state;
    struct timespec t0, t1;
    int n = s->nthreads;
    pthread_t *threads = malloc(n * sizeof(pthread_t));
    bw_thread_arg_t *args = malloc(n * sizeof(bw_thread_arg_t));
    size_t chunk = BUF_SIZE / n;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int t = 0; t < n; t++) {
        args[t] = (bw_thread_arg_t){s->buffer, t * chunk, (t + 1) * chunk};
        pthread_create(&threads[t], NULL, bw_worker, &args[t]);
    }
    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(threads); free(args);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)BUF_SIZE / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int numa_bandwidth_cleanup(void *state) {
    numa_bandwidth_state_t *s = (numa_bandwidth_state_t *)state;
    if (s->buffer) numa_free(s->buffer, BUF_SIZE);
    free(s);
    return 0;
}

benchmark_t bench_numa_bandwidth = {
    .name = "numa-bandwidth",
    .category = "C5",
    .description = "NUMA remote node read bandwidth (auto-scaled threads)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = numa_bandwidth_init,
    .warmup = numa_bandwidth_warmup,
    .measure = numa_bandwidth_measure,
    .cleanup = numa_bandwidth_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_numa_bandwidth);
