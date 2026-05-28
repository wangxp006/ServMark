#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BUF_SIZE (32 * 1024 * 1024)  /* 32 MB */
#define NUM_ACCESSES 2000000

typedef struct {
    int64_t *buffer;
    size_t num_elements;
    int *indices;
} mem_random_state_t;

static int mem_random_init(void **state) {
    mem_random_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->num_elements = BUF_SIZE / sizeof(int64_t);
    s->buffer = malloc(BUF_SIZE);
    s->indices = malloc(NUM_ACCESSES * sizeof(int));
    if (!s->buffer || !s->indices) {
        free(s->buffer); free(s->indices); free(s);
        return -1;
    }
    /* Fill buffer with data */
    srand(time(NULL));
    for (size_t i = 0; i < s->num_elements; i++)
        s->buffer[i] = (int64_t)i ^ 0xDEADBEEFCAFE1234ULL;
    /* Generate random access pattern */
    for (int i = 0; i < NUM_ACCESSES; i++)
        s->indices[i] = rand() % (int)s->num_elements;
    *state = s;
    return 0;
}

static int mem_random_warmup(void *state) {
    mem_random_state_t *s = (mem_random_state_t *)state;
    volatile int64_t sink = 0;
    for (int i = 0; i < 50000; i++)
        sink += s->buffer[s->indices[i]];
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int mem_random_measure(void *state, measurement_t *result) {
    mem_random_state_t *s = (mem_random_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_ACCESSES; i++) {
        sink += s->buffer[s->indices[i]];
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e9 / NUM_ACCESSES; /* ns/access */
    result->wall_seconds = elapsed;
    return 0;
}

static int mem_random_cleanup(void *state) {
    mem_random_state_t *s = (mem_random_state_t *)state;
    free(s->buffer); free(s->indices); free(s);
    return 0;
}

benchmark_t bench_mem_random = {
    .name = "mem-random",
    .category = "C4",
    .description = "Random access latency 32MB working set",
    .tier = 1,
    .primary_metric_name = "ns/access",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = mem_random_init,
    .warmup = mem_random_warmup,
    .measure = mem_random_measure,
    .cleanup = mem_random_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_mem_random);
