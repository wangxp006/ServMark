#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/*
 * Multi-level cache latency benchmark. Pointer chase at increasing
 * working-set sizes. STRIDE uses runtime cache line size from sysconf()
 * for correct cache-level isolation on all architectures.
 */
#define MAX_SIZE       (256 * 1024 * 1024)
#define CHASES         2000000

static const size_t _size_levels[] = {
    8*1024, 32*1024, 256*1024, 1024*1024,
    8*1024*1024, 32*1024*1024, 128*1024*1024, 256*1024*1024,
};
#define NUM_LEVELS (sizeof(_size_levels)/sizeof(_size_levels[0]))

typedef struct {
    char *buffer;
    size_t size;
    int stride;
} mem_latency_state_t;

static void setup_pointer_chase(char *buf, size_t size, int stride) {
    size_t num_slots = size / stride;
    if (num_slots < 2) return;
    int *order = malloc(num_slots * sizeof(int));
    for (size_t i = 0; i < num_slots; i++) order[i] = (int)i;
    for (size_t i = num_slots - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    for (size_t i = 0; i < num_slots - 1; i++) {
        uintptr_t pv = (uintptr_t)(buf + order[i + 1] * stride);
        memcpy(buf + order[i] * stride, &pv, sizeof(pv));
    }
    uintptr_t pf = (uintptr_t)(buf + order[0] * stride);
    memcpy(buf + order[num_slots - 1] * stride, &pf, sizeof(pf));
    free(order);
}

static int mem_latency_init(void **state) {
    mem_latency_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    long cls = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    s->stride = (cls > 0) ? (int)cls : 64;
    s->size = MAX_SIZE;
    s->buffer = malloc(s->size);
    if (!s->buffer) { free(s); return -1; }
    setup_pointer_chase(s->buffer, s->size, s->stride);
    *state = s;
    return 0;
}

static int mem_latency_warmup(void *state) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    volatile uintptr_t *p = (volatile uintptr_t *)s->buffer;
    for (int i = 0; i < 500000; i++) {
        uintptr_t _n; memcpy(&_n, (void*)p, sizeof(_n));
        p = (volatile uintptr_t *)_n;
    }
    __asm__ __volatile__("" : "+r"(p));
    return 0;
}

static double chase_latency(char *buf, size_t rs, int stride, int nc) {
    size_t ns = rs / stride;
    if (ns < 2) return 0.0;
    setup_pointer_chase(buf, rs, stride);
    struct timespec t0, t1;
    volatile uintptr_t *p = (volatile uintptr_t *)buf;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < nc; i++) {
        uintptr_t _n; memcpy(&_n, (void*)p, sizeof(_n));
        p = (volatile uintptr_t *)_n;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(p));
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    return el * 1e9 / nc;
}

static int mem_latency_measure(void *state, measurement_t *result) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    double dram_lat = 0.0;
    for (int lv = 0; lv < (int)NUM_LEVELS; lv++) {
        if (_size_levels[lv] > s->size) continue;
        double lat = chase_latency(s->buffer, _size_levels[lv], s->stride, CHASES);
        if (lv == (int)NUM_LEVELS - 1) dram_lat = lat;
    }
    memset(result, 0, sizeof(*result));
    result->primary_metric = dram_lat;
    result->wall_seconds = dram_lat * CHASES / 1e9 * NUM_LEVELS;
    return 0;
}

static int mem_latency_cleanup(void *state) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_mem_latency = {
    .name = "mem-latency", .category = "C4",
    .description = "Multi-level pointer chase latency (8KB-256MB, runtime CLS stride)",
    .tier = 1, .primary_metric_name = "ns/chase", .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS, .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC, .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = mem_latency_init, .warmup = mem_latency_warmup,
    .measure = mem_latency_measure, .cleanup = mem_latency_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_mem_latency);
