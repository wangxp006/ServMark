#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <numa.h>
#include <stdint.h>
#include <unistd.h>

#define BUF_MB 64
#define BUF_SIZE (BUF_MB * 1024 * 1024)
#define CHASES 2000000

typedef struct {
    char *buffer;
    int node;
    int stride;
} numa_latency_state_t;

static void setup_chase(char *buf, size_t size, int stride) {
    size_t slots = size / stride;
    int *order = malloc(slots * sizeof(int));
    for (size_t i = 0; i < slots; i++) order[i] = (int)i;
    for (size_t i = slots - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    for (size_t i = 0; i < slots - 1; i++) {
        uintptr_t pv = (uintptr_t)(buf + order[i + 1] * stride);
        memcpy(buf + order[i] * stride, &pv, sizeof(pv));
    }
    uintptr_t pf = (uintptr_t)(buf + order[0] * stride);
    memcpy(buf + order[slots - 1] * stride, &pf, sizeof(pf));
    free(order);
}

static int numa_latency_init(void **state) {
    numa_latency_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (numa_available() < 0) { free(s); return -1; }
    long cls = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    s->stride = (cls > 0) ? (int)cls : 64;
    s->node = 0;
    s->buffer = numa_alloc_onnode(BUF_SIZE, s->node);
    if (!s->buffer) { free(s); return -1; }
    setup_chase(s->buffer, BUF_SIZE, s->stride);
    *state = s;
    return 0;
}

static int numa_latency_warmup(void *state) {
    numa_latency_state_t *s = (numa_latency_state_t *)state;
    volatile uintptr_t *p = (volatile uintptr_t *)s->buffer;
    for (int i = 0; i < 50000; i++) {
        uintptr_t _n; memcpy(&_n, (void*)p, sizeof(_n));
        p = (volatile uintptr_t *)_n;
    }
    __asm__ __volatile__("" : "+r"(p));
    return 0;
}

static int numa_latency_measure(void *state, measurement_t *result) {
    numa_latency_state_t *s = (numa_latency_state_t *)state;
    struct timespec t0, t1;
    numa_run_on_node(s->node);
    numa_set_localalloc();
    volatile uintptr_t *p = (volatile uintptr_t *)s->buffer;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < CHASES; i++) {
        uintptr_t _n; memcpy(&_n, (void*)p, sizeof(_n));
        p = (volatile uintptr_t *)_n;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(p));
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = el * 1e9 / CHASES;
    result->wall_seconds = el;
    return 0;
}

static int numa_latency_cleanup(void *state) {
    numa_latency_state_t *s = (numa_latency_state_t *)state;
    if (s->buffer) numa_free(s->buffer, BUF_SIZE);
    free(s); return 0;
}

benchmark_t bench_numa_latency = {
    .name="numa-latency", .category="C5",
    .description="NUMA node 0 local memory latency (runtime CLS stride)",
    .tier=1, .primary_metric_name="ns/chase", .higher_is_better=false,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=numa_latency_init, .warmup=numa_latency_warmup,
    .measure=numa_latency_measure, .cleanup=numa_latency_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_numa_latency);
