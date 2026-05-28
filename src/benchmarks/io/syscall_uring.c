#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NUM_OPS 5000000

typedef struct {
    volatile int64_t sink;
} syscall_uring_state_t;

static int syscall_uring_init(void **state) {
    syscall_uring_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int syscall_uring_warmup(void *state) {
    syscall_uring_state_t *s = (syscall_uring_state_t *)state;
    volatile int64_t sink = 0;
    for (int i = 0; i < 100000; i++)
        sink += syscall(SYS_getpid);
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int syscall_uring_measure(void *state, measurement_t *result) {
    syscall_uring_state_t *s = (syscall_uring_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Measure raw syscall(SYS_getpid) as baseline for syscall overhead comparison
     * This benchmarks the kernel entry/exit cost without vDSO */
    for (int i = 0; i < NUM_OPS; i++) {
        sink += syscall(SYS_getpid);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    s->sink = sink;
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_OPS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int syscall_uring_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_syscall_uring = {
    .name = "syscall-uring",
    .category = "C12",
    .description = "Raw syscall(SYS_getpid) baseline (no vDSO fast path)",
    .tier = 2,
    .primary_metric_name = "calls/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = syscall_uring_init,
    .warmup = syscall_uring_warmup,
    .measure = syscall_uring_measure,
    .cleanup = syscall_uring_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_syscall_uring);
