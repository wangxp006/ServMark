#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NUM_CALLS 10000000

typedef struct {
    volatile int64_t sink;
} syscall_vdso_state_t;

static int syscall_vdso_init(void **state) {
    syscall_vdso_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int syscall_vdso_warmup(void *state) {
    syscall_vdso_state_t *s = (syscall_vdso_state_t *)state;
    volatile int64_t sink = 0;
    struct timespec ts;
    for (int i = 0; i < 100000; i++)
        sink += clock_gettime(CLOCK_MONOTONIC, &ts) ? 0 : ts.tv_nsec;
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int syscall_vdso_measure(void *state, measurement_t *result) {
    syscall_vdso_state_t *s = (syscall_vdso_state_t *)state;
    struct timespec t0, t1, ts;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* vDSO clock_gettime - pure userspace fast path on modern kernels */
    for (int i = 0; i < NUM_CALLS; i++) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        sink += ts.tv_nsec;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    s->sink = sink;
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_CALLS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int syscall_vdso_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_syscall_vdso = {
    .name = "syscall-vdso",
    .category = "C12",
    .description = "vDSO clock_gettime (userspace fast path)",
    .tier = 1,
    .primary_metric_name = "calls/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = syscall_vdso_init,
    .warmup = syscall_vdso_warmup,
    .measure = syscall_vdso_measure,
    .cleanup = syscall_vdso_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_syscall_vdso);
