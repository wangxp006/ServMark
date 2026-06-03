#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>

#define NUM_SIGNALS 200000

/*
 * kill() signal delivery throughput benchmark.
 *
 * kill() is a standard POSIX syscall on all architectures. The syscall
 * entry/exit mechanism differs by ISA (x86: SYSCALL/SYSRET, ARM64: svc#0,
 * RISC-V: ecall), which is an inherent architectural difference.
 */

typedef struct {
    _Atomic int caught;
    pid_t target_pid;
} ipc_signal_state_t;

static void sig_usr1_handler(int sig) {
    (void)sig;
}

static int ipc_signal_init(void **state) {
    ipc_signal_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    struct sigaction sa = {0};
    sa.sa_handler = sig_usr1_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    s->target_pid = getpid();
    atomic_init(&s->caught, 0);
    *state = s;
    return 0;
}

static int ipc_signal_warmup(void *state) {
    ipc_signal_state_t *s = (ipc_signal_state_t *)state;
    for (int i = 0; i < 1000; i++)
        kill(s->target_pid, SIGUSR1);
    return 0;
}

static int ipc_signal_measure(void *state, measurement_t *result) {
    ipc_signal_state_t *s = (ipc_signal_state_t *)state;
    struct timespec t0, t1;
    int64_t total_sent = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_SIGNALS; i++) {
        kill(s->target_pid, SIGUSR1);
        total_sent++;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_sent / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int ipc_signal_cleanup(void *state) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, NULL);
    free(state);
    return 0;
}

benchmark_t bench_ipc_signal = {
    .name = "ipc-signal",
    .category = "C11",
    .description = "kill() signal send throughput (SIGUSR1, syscall-cost dominant, standard signals coalesce)",
    .tier = 1,
    .primary_metric_name = "signals/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = ipc_signal_init,
    .warmup = ipc_signal_warmup,
    .measure = ipc_signal_measure,
    .cleanup = ipc_signal_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_ipc_signal);
