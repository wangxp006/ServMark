#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define BUF_MB 256
#define BUF_SIZE (BUF_MB * 1024 * 1024)

typedef struct {
    char *src;
} mem_bandwidth_state_t;

static int mem_bandwidth_init(void **state) {
    mem_bandwidth_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->src = malloc(BUF_SIZE);
    if (!s->src) { free(s); return -1; }
    for (size_t i = 0; i < BUF_SIZE; i++) s->src[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int mem_bandwidth_warmup(void *state) {
    mem_bandwidth_state_t *s = (mem_bandwidth_state_t *)state;
    uint64_t *src64 = (uint64_t *)s->src;
    size_t count = BUF_SIZE / sizeof(uint64_t);
    uint64_t sink = 0;
    for (size_t i = 0; i < count; i++)
        sink += src64[i];
    __asm__ __volatile__("" : "+r"(sink) : : "memory");
    return 0;
}

static int mem_bandwidth_measure(void *state, measurement_t *result) {
    mem_bandwidth_state_t *s = (mem_bandwidth_state_t *)state;
    struct timespec t0, t1;
    volatile char sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Sequential read bandwidth. volatile load prevents Clang auto-vec
     * (GCC -O2 is scalar, but Clang -O2 auto-vectorizes. This keeps
     * cross-compiler results comparable per project design). */
    {
        volatile uint64_t *src64 = (volatile uint64_t *)s->src;
        size_t count = BUF_SIZE / sizeof(uint64_t);
        uint64_t sink64 = 0;
        for (size_t i = 0; i < count; i++) sink64 += src64[i];
        __asm__ __volatile__("" : "+r"(sink64) : : "memory");
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)BUF_SIZE / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int mem_bandwidth_cleanup(void *state) {
    mem_bandwidth_state_t *s = (mem_bandwidth_state_t *)state;
    free(s->src); free(s);
    return 0;
}

benchmark_t bench_mem_bandwidth = {
    .name = "mem-bandwidth",
    .category = "C4",
    .description = "Sequential memory read bandwidth 256MB",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = mem_bandwidth_init,
    .warmup = mem_bandwidth_warmup,
    .measure = mem_bandwidth_measure,
    .cleanup = mem_bandwidth_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_mem_bandwidth);
