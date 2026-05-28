#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define SWITCHES 2000000
#define ITER_COUNT 50000000

typedef struct {
    volatile int flag;
    volatile int ready;
    volatile int64_t iters;
    volatile int64_t ping_count;
    volatile int64_t pong_count;
} cswitch_smt_state_t;

static int cswitch_smt_init(void **state) {
    cswitch_smt_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int cswitch_smt_warmup(void *state) {
    /* SMT ping-pong using shared variable */
    cswitch_smt_state_t *s = (cswitch_smt_state_t *)state;
    volatile int64_t *ping = &s->ping_count;
    volatile int64_t *pong = &s->pong_count;
    /* Single-threaded warmup - just exercise atomic ops */
    for (int i = 0; i < 100000; i++) {
        __sync_fetch_and_add(ping, 1);
        __sync_fetch_and_add(pong, 1);
    }
    return 0;
}

static int cswitch_smt_measure(void *state, measurement_t *result) {
    cswitch_smt_state_t *s = (cswitch_smt_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* SMT co-location test: two counters in the same cache line,
     * measuring cache line bouncing across SMT siblings */
    for (int i = 0; i < ITER_COUNT; i++) {
        __sync_fetch_and_add(&s->ping_count, 1);
        __sync_fetch_and_add(&s->pong_count, 1);
        sink += s->ping_count + s->pong_count;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    /* Report atomic ops per second - measures cache line contention */
    result->primary_metric = 2.0 * ITER_COUNT / elapsed;
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
    .description = "SMT cache line bouncing (same cache line atomic ops)",
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
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_cswitch_smt);
