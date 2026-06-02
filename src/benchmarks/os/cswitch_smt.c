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
 * Two threads (pinned to SMT siblings by the harness) each atomically
 * increment their own counter on the SAME cache line. The cache line
 * bounces between the two logical cores' L1 caches via the MESI/MOESI
 * coherence protocol, measuring cross-hyperthread cache line contention.
 *
 * ping_count and pong_count are placed adjacently in the struct so they
 * share a 64-byte cache line on modern x86 and ARM CPUs.
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
    atomic_store(&s->ready, 1);
    for (int i = 0; i < ITER_COUNT; i++) {
        atomic_fetch_add(&s->pong_count, 1);
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
    /* Single-threaded warmup — primes the atomic execution pipeline */
    for (int i = 0; i < 100000; i++) {
        atomic_fetch_add(&s->ping_count, 1);
        atomic_fetch_add(&s->pong_count, 1);
    }
    return 0;
}

static int cswitch_smt_measure(void *state, measurement_t *result) {
    cswitch_smt_state_t *s = (cswitch_smt_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;
    int ret;

    atomic_store(&s->ready, 0);
    atomic_store(&s->ping_count, 0);
    atomic_store(&s->pong_count, 0);

    ret = pthread_create(&s->thread, NULL, pong_thread, s);
    if (ret != 0) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    while (!atomic_load(&s->ready))
        ;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Main thread hammers ping_count while the pong thread hammers
     * pong_count. Both counters share the same cache line — each atomic
     * increment on one logical core invalidates the other core's L1 copy,
     * forcing a cache line transfer via the coherence protocol. */
    for (int i = 0; i < ITER_COUNT; i++) {
        atomic_fetch_add(&s->ping_count, 1);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    pthread_join(s->thread, NULL);

    sink = atomic_load(&s->ping_count) + atomic_load(&s->pong_count);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    /* Report total atomic ops per second across both threads */
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
    .description = "SMT cache line bouncing (2 threads, shared cache line atomic ops)",
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
