#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N_ELEMENTS 10000000
#define RADIX_BITS 8
#define RADIX_SIZE (1 << RADIX_BITS)
#define RADIX_MASK (RADIX_SIZE - 1)

typedef struct {
    uint64_t *data;
    uint64_t *buffer;
} int_sort_state_t;

static int int_sort_init(void **state) {
    int_sort_state_t *s = calloc(1, sizeof(int_sort_state_t));
    if (!s) return -1;

    s->data = malloc(N_ELEMENTS * sizeof(uint64_t));
    s->buffer = malloc(N_ELEMENTS * sizeof(uint64_t));
    if (!s->data || !s->buffer) {
        free(s->data); free(s->buffer); free(s); return -1;
    }

    srand(time(NULL));
    for (int i = 0; i < N_ELEMENTS; i++) {
        s->data[i] = ((uint64_t)rand() << 32) | (uint64_t)rand();
    }

    *state = s;
    return 0;
}

static void lsb_radix_sort(uint64_t *src, uint64_t *dst, int n) {
    /* 8-pass LSD radix sort on 64-bit integers */
    for (int shift = 0; shift < 64; shift += RADIX_BITS) {
        int count[RADIX_SIZE] = {0};

        for (int i = 0; i < n; i++) {
            int bucket = (src[i] >> shift) & RADIX_MASK;
            count[bucket]++;
        }

        int pos[RADIX_SIZE];
        pos[0] = 0;
        for (int b = 1; b < RADIX_SIZE; b++) {
            pos[b] = pos[b-1] + count[b-1];
        }

        for (int i = 0; i < n; i++) {
            int bucket = (src[i] >> shift) & RADIX_MASK;
            dst[pos[bucket]++] = src[i];
        }

        uint64_t *tmp = src;
        src = dst;
        dst = tmp;
    }
}

static int int_sort_warmup(void *state) {
    int_sort_state_t *s = (int_sort_state_t *)state;
    memcpy(s->buffer, s->data, N_ELEMENTS * sizeof(uint64_t));
    lsb_radix_sort(s->buffer, malloc(N_ELEMENTS * sizeof(uint64_t)),
            N_ELEMENTS / 100);
    return 0;
}

static int int_sort_measure(void *state, measurement_t *result) {
    int_sort_state_t *s = (int_sort_state_t *)state;
    struct timespec t0, t1;
    volatile uint64_t sink = 0;

    memcpy(s->buffer, s->data, N_ELEMENTS * sizeof(uint64_t));
    uint64_t *tmp = malloc(N_ELEMENTS * sizeof(uint64_t));
    if (!tmp) return -1;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    lsb_radix_sort(s->buffer, tmp, N_ELEMENTS);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Verify sortedness */
    for (int i = 0; i < N_ELEMENTS - 1; i++) {
        if (s->buffer[i] > s->buffer[i+1]) sink++;
    }
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = N_ELEMENTS / elapsed;
    result->wall_seconds = elapsed;

    free(tmp);
    return 0;
}

static int int_sort_cleanup(void *state) {
    int_sort_state_t *s = (int_sort_state_t *)state;
    free(s->data); free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_int_sort = {
    .name = "int-sort",
    .category = "C1",
    .description = "64-bit integer LSD radix sort 10M elements",
    .tier = 1,
    .primary_metric_name = "elements/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = int_sort_init,
    .warmup = int_sort_warmup,
    .measure = int_sort_measure,
    .cleanup = int_sort_cleanup,
    .num_threads = 1,
};

SSB_BENCHMARK_REGISTER(bench_int_sort);
