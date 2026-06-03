#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <numa.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

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
    int node;
} bw_thread_arg_t;

static void *bw_worker(void *arg) {
    bw_thread_arg_t *a = (bw_thread_arg_t *)arg;
    numa_run_on_node(a->node);        /* pin THIS worker, not creator */
    uint64_t *b = (uint64_t *)a->buf;
    size_t s = a->start/sizeof(uint64_t), e = a->end/sizeof(uint64_t);
    uint64_t sink = 0;
    for (size_t i = s; i < e; i++) sink += b[i];
    __asm__ __volatile__("" : "+r"(sink) : : "memory");
    return NULL;
}

static int numa_bandwidth_init(void **state) {
    numa_bandwidth_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (numa_available() < 0) { free(s); return -1; }
    int max_node = numa_max_node();
    if (max_node < 1) { free(s); return -1; }
    s->node = max_node > 0 ? 1 : 0;
    s->buffer = numa_alloc_onnode(BUF_SIZE, s->node);
    if (!s->buffer) { free(s); return -1; }
    memset(s->buffer, 0xAB, BUF_SIZE);
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 1) s->nthreads = 1;
    *state = s; return 0;
}

static int numa_bandwidth_warmup(void *state) {
    numa_bandwidth_state_t *s = (numa_bandwidth_state_t *)state;
    volatile char sk = 0;
    for (size_t i = 0; i < BUF_SIZE/10; i+=8) sk += s->buffer[i];
    __asm__ __volatile__("" : "+r"(sk));
    return 0;
}

static int numa_bandwidth_measure(void *state, measurement_t *result) {
    numa_bandwidth_state_t *s = (numa_bandwidth_state_t *)state;
    struct timespec t0, t1;
    int n = s->nthreads;
    pthread_t *th = malloc(n * sizeof(pthread_t));
    bw_thread_arg_t *args = malloc(n * sizeof(bw_thread_arg_t));
    size_t ch = BUF_SIZE / n;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int t = 0; t < n; t++) {
        args[t] = (bw_thread_arg_t){s->buffer, t*ch, (t+1)*ch, 0}; /* run local, read remote */
        pthread_create(&th[t], NULL, bw_worker, &args[t]);
    }
    for (int t = 0; t < n; t++) pthread_join(th[t], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(th); free(args);
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)BUF_SIZE / el;
    result->wall_seconds = el;
    return 0;
}

static int numa_bandwidth_cleanup(void *state) {
    numa_bandwidth_state_t *s = (numa_bandwidth_state_t *)state;
    if (s->buffer) numa_free(s->buffer, BUF_SIZE);
    free(s); return 0;
}

benchmark_t bench_numa_bandwidth = {
    .name="numa-bandwidth", .category="C5",
    .description="NUMA remote node read bandwidth (per-thread node pinning)",
    .tier=1, .primary_metric_name="bytes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=numa_bandwidth_init, .warmup=numa_bandwidth_warmup,
    .measure=numa_bandwidth_measure, .cleanup=numa_bandwidth_cleanup,
    .num_threads=-1,
};
SSB_BENCHMARK_REGISTER(bench_numa_bandwidth);
