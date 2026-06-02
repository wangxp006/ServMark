#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

/*
 * SMT cache line bouncing benchmark.
 *
 * Two threads each atomically increment their own counter on the SAME
 * cache line. The cache line bounces between the two logical cores' L1
 * caches via the MESI/MOESI coherence protocol.
 *
 * Counters use memory_order_relaxed: we only need atomic RMW for cache
 * line contention, not cross-thread ordering. The fences required by
 * seq_cst would add ~10-15ns/op (RISC-V fence rw,rw) or ~20ns/op
 * (ARM64 DMB ISH) — unfairly penalizing weakly-ordered architectures.
 */

#define ITER_COUNT 25000000

typedef struct {
    _Atomic int64_t ping_count;
    _Atomic int64_t pong_count;
    pthread_t thread;
    _Atomic int ready;
} cswitch_smt_state_t;

static void *pong_thread(void *arg) {
    cswitch_smt_state_t *s = (cswitch_smt_state_t *)arg;
    atomic_store_explicit(&s->ready, 1, memory_order_release);
    for (int i = 0; i < ITER_COUNT; i++) {
        atomic_fetch_add_explicit(&s->pong_count, 1, memory_order_relaxed);
    }
    return NULL;
}

static int cswitch_smt_init(void **state) {
    cswitch_smt_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int cswitch_smt_warmup(void *state) {
    cswitch_smt_state_t *s = (cswitch_smt_state_t *)state;
    for (int i = 0; i < 100000; i++) {
        atomic_fetch_add_explicit(&s->ping_count, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->pong_count, 1, memory_order_relaxed);
    }
    return 0;
}

static int cswitch_smt_measure(void *state, measurement_t *result) {
    cswitch_smt_state_t *s = (cswitch_smt_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;
    int ret;

    atomic_store_explicit(&s->ready, 0, memory_order_relaxed);
    atomic_store_explicit(&s->ping_count, 0, memory_order_relaxed);
    atomic_store_explicit(&s->pong_count, 0, memory_order_relaxed);

    ret = pthread_create(&s->thread, NULL, pong_thread, s);
    if (ret != 0) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    while (!atomic_load_explicit(&s->ready, memory_order_acquire))
        ;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < ITER_COUNT; i++) {
        atomic_fetch_add_explicit(&s->ping_count, 1, memory_order_relaxed);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    pthread_join(s->thread, NULL);

    sink = atomic_load_explicit(&s->ping_count, memory_order_relaxed)
         + atomic_load_explicit(&s->pong_count, memory_order_relaxed);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)(sink) / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int cswitch_smt_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_cswitch_smt = {
    .name = "cswitch-smt",
    .category = "C8",
    .description = "SMT cache line bouncing (2 threads, shared cache line, relaxed atomics)",
    .tier = 1,
    .primary_metric_name = "ops/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = cswitch_smt_init,
    .warmup = cswitch_smt_warmup,
    .measure = cswitch_smt_measure,
    .cleanup = cswitch_smt_cleanup,
    .num_threads = 2,
};
SSB_BENCHMARK_REGISTER(bench_cswitch_smt);
