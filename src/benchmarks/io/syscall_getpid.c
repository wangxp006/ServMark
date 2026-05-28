#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#define CALLS_PER_ITER 5000000

/* UnixBench System Call Overhead exact equivalent:
 * Measures getpid() latency in a tight loop */

typedef struct {
    volatile int64_t sink;
} syscall_getpid_state_t;

static int syscall_getpid_init(void **state) {
    syscall_getpid_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int syscall_getpid_warmup(void *state) {
    syscall_getpid_state_t *s = (syscall_getpid_state_t *)state;
    volatile int64_t sink = 0;
    for (int i = 0; i < CALLS_PER_ITER / 10; i++) {
        sink += getpid();
    }
    __asm__ __volatile__("" : "+r"(sink));
    (void)s;
    return 0;
}

static int syscall_getpid_measure(void *state, measurement_t *result) {
    syscall_getpid_state_t *s = (syscall_getpid_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < CALLS_PER_ITER; i++) {
        sink += getpid();
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Prevent DCE */
    __asm__ __volatile__("" : "+r"(sink));
    s->sink = sink;

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e9 / CALLS_PER_ITER; /* ns/call */
    result->wall_seconds = elapsed;

    return 0;
}

static int syscall_getpid_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_syscall_getpid = {
    .name = "syscall-getpid",
    .category = "C12",
    .description = "getpid() system call overhead (UnixBench exact equivalent)",
    .tier = 1,
    .primary_metric_name = "ns/call",
    .higher_is_better = false,  /* latency: lower is better */
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = syscall_getpid_init,
    .warmup = syscall_getpid_warmup,
    .measure = syscall_getpid_measure,
    .cleanup = syscall_getpid_cleanup,
    .num_threads = 1,
};

SSB_BENCHMARK_REGISTER(bench_syscall_getpid);
