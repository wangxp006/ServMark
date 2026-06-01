#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define MAX_SIZE (64 * 1024 * 1024)  /* 64MB */
#define STRIDE 64
#define CHASES 5000000

typedef struct {
    char *buffer;
    size_t size;
} mem_latency_state_t;

static void setup_pointer_chase(char *buf, size_t size, int stride) {
    size_t num_slots = size / stride;
    /* Create random cyclic linked list */
    int *order = malloc(num_slots * sizeof(int));
    for (size_t i = 0; i < num_slots; i++) order[i] = (int)i;
    /* Fisher-Yates shuffle */
    srand(time(NULL));
    for (size_t i = num_slots - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    /* Store next pointer at each slot using memcpy to avoid strict aliasing UB */
    for (size_t i = 0; i < num_slots - 1; i++) {
        uintptr_t ptr_val = (uintptr_t)(buf + order[i + 1] * stride);
        memcpy(buf + order[i] * stride, &ptr_val, sizeof(ptr_val));
    }
    uintptr_t ptr_val_first = (uintptr_t)(buf + order[0] * stride);
    memcpy(buf + order[num_slots - 1] * stride, &ptr_val_first, sizeof(ptr_val_first));
    free(order);
}

static int mem_latency_init(void **state) {
    mem_latency_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->size = MAX_SIZE;
    s->buffer = malloc(s->size);
    if (!s->buffer) { free(s); return -1; }
    setup_pointer_chase(s->buffer, s->size, STRIDE);
    *state = s;
    return 0;
}

static int mem_latency_warmup(void *state) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    volatile int64_t *p = (volatile int64_t *)s->buffer;
    for (int i = 0; i < 100000; i++)
        {
            uintptr_t _next;
            memcpy(&_next, (void*)p, sizeof(_next));
            p = (volatile uintptr_t *)_next;
        }
    __asm__ __volatile__("" : "+r"(p));
    return 0;
}

static int mem_latency_measure(void *state, measurement_t *result) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    struct timespec t0, t1;

    volatile int64_t *p = (volatile int64_t *)s->buffer;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < CHASES; i++) {
        {
            uintptr_t _next;
            memcpy(&_next, (void*)p, sizeof(_next));
            p = (volatile uintptr_t *)_next;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    __asm__ __volatile__("" : "+r"(p));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    /* Report as ns/chase */
    result->primary_metric = elapsed * 1e9 / CHASES;
    result->wall_seconds = elapsed;
    return 0;
}

static int mem_latency_cleanup(void *state) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_mem_latency = {
    .name = "mem-latency",
    .category = "C4",
    .description = "Pointer chase latency 64MB working set",
    .tier = 1,
    .primary_metric_name = "ns/chase",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = mem_latency_init,
    .warmup = mem_latency_warmup,
    .measure = mem_latency_measure,
    .cleanup = mem_latency_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_mem_latency);
