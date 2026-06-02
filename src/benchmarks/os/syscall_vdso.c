#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NUM_CALLS 10000000

/*
 * vDSO clock_gettime benchmark.
 *
 * clock_gettime(CLOCK_MONOTONIC) executes entirely in userspace via
 * the vDSO (__vdso_clock_gettime): rdtsc + vvar page arithmetic.
 * Typical latency: ~15-25ns on modern x86 — no kernel entry at all.
 *
 * The ratio between syscall_getpid.c (ns/call, syscall path ~100-150ns)
 * and this benchmark (calls/sec, vDSO path ~15-25ns) is a useful metric
 * for quantifying kernel entry/exit overhead.
 *
 * NOTE: This benchmark reports calls/sec (higher_is_better = true), while
 * syscall_getpid.c reports ns/call (higher_is_better = false). Convert
 * between them by: q calls/sec → 1e9/q ns/call.
 */

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
        sink += clock_gettime(CLOCK_MONOTONIC, &ts) == 0 ? ts.tv_nsec : 0;
    __asm__ __volatile__("" : "+r"(sink));
    s->sink = sink;
    return 0;
}

static int syscall_vdso_measure(void *state, measurement_t *result) {
    syscall_vdso_state_t *s = (syscall_vdso_state_t *)state;
    struct timespec t0, t1, ts;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* vDSO clock_gettime — pure userspace fast path on modern kernels.
     * rdtsc + vvar page multiply/shift, no kernel entry. */
    for (int i = 0; i < NUM_CALLS; i++) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        sink += ts.tv_nsec;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    __asm__ __volatile__("" : "+r"(sink));
    s->sink = sink; /* persist sink to prevent DCE across iterations */

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_CALLS / elapsed; /* calls/sec */
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
    .description = "vDSO clock_gettime (userspace fast path, calls/sec — cf. syscall-getpid ns/call)",
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
