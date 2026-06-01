#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* Multi-level cache latency benchmark: probes L1, L2, L3, and DRAM.
 * Uses pointer chasing at increasing working-set sizes to identify
 * latency cliffs at each cache level boundary. */
#define MAX_SIZE       (256 * 1024 * 1024)  /* 256MB, larger than any L3 */
#define STRIDE         64
#define CHASES         2000000              /* per-size chases */

/* Working set sizes for multi-level probing */
static const size_t _size_levels[] = {
    8 * 1024,           /* 8KB   — L1 */
    32 * 1024,          /* 32KB  — L1 */
    256 * 1024,         /* 256KB — L2 */
    1024 * 1024,        /* 1MB   — L2/L3 */
    8 * 1024 * 1024,    /* 8MB   — L3 */
    32 * 1024 * 1024,   /* 32MB  — L3 */
    128 * 1024 * 1024,  /* 128MB — DRAM */
    256 * 1024 * 1024,  /* 256MB — DRAM */
};
#define NUM_LEVELS (sizeof(_size_levels) / sizeof(_size_levels[0]))

typedef struct {
    char *buffer;
    size_t size;
} mem_latency_state_t;

static void setup_pointer_chase(char *buf, size_t size, int stride) {
    size_t num_slots = size / stride;
    if (num_slots < 2) return;
    int *order = malloc(num_slots * sizeof(int));
    for (size_t i = 0; i < num_slots; i++) order[i] = (int)i;
    /* Fisher-Yates shuffle */
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
    /* Build pointer chain for the full 256MB region.
     * Smaller sizes are carved out as sub-ranges of this buffer. */
    setup_pointer_chase(s->buffer, s->size, STRIDE);
    *state = s;
    return 0;
}

static int mem_latency_warmup(void *state) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    /* Warmup on the full 256MB working set */
    volatile uintptr_t *p = (volatile uintptr_t *)s->buffer;
    for (int i = 0; i < 500000; i++) {
        uintptr_t _next;
        memcpy(&_next, (void*)p, sizeof(_next));
        p = (volatile uintptr_t *)_next;
    }
    __asm__ __volatile__("" : "+r"(p));
    return 0;
}

static double chase_latency(char *buf, size_t region_size, int stride, int nchases) {
    size_t num_slots = region_size / stride;
    if (num_slots < 2) return 0.0;

    /* Re-randomize the pointer chain within this region */
    setup_pointer_chase(buf, region_size, stride);

    struct timespec t0, t1;
    volatile uintptr_t *p = (volatile uintptr_t *)buf;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < nchases; i++) {
        uintptr_t _next;
        memcpy(&_next, (void*)p, sizeof(_next));
        /* Mask to keep pointer within region */
        _next = (_next & ~(region_size - 1)) | ((uintptr_t)buf & (region_size - 1));
        /* Actually just let the chain work — the shuffle already keeps it in bounds */
        p = (volatile uintptr_t *)_next;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(p));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    return elapsed * 1e9 / nchases;  /* ns/chase */
}

static int mem_latency_measure(void *state, measurement_t *result) {
    mem_latency_state_t *s = (mem_latency_state_t *)state;
    double total_elapsed = 0.0;
    double lowest_latency = 1e18;

    /* Probe each working-set size. The largest size (256MB) measures
     * true DRAM latency; smaller sizes fall within various cache levels. */
    for (int level = 0; level < (int)NUM_LEVELS; level++) {
        size_t sz = _size_levels[level];
        if (sz > s->size) continue;
        double lat = chase_latency(s->buffer, sz, STRIDE, CHASES);
        total_elapsed += lat;  /* accumulate for wall time approximation */
        if (lat < lowest_latency && lat > 0) lowest_latency = lat;
    }

    memset(result, 0, sizeof(*result));
    /* Report DRAM latency (largest size = 256MB) as primary metric */
    double dram_lat = chase_latency(s->buffer, _size_levels[NUM_LEVELS - 1], STRIDE, CHASES);
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
    .name = "mem-latency",
    .category = "C4",
    .description = "Multi-level pointer chase latency (8KB–256MB working sets, 64B stride)",
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
