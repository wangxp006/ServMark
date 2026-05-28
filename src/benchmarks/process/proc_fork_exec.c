#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define FORKS_PER_ITER 500

/* UnixBench Process Creation exact equivalent:
 * fork() + exec(/bin/true) + wait() loop */

typedef struct {
    int dummy;
} proc_fork_exec_state_t;

static int proc_fork_exec_init(void **state) {
    proc_fork_exec_state_t *s = calloc(1, sizeof(*s));
    *state = s;
    return (s != NULL) ? 0 : -1;
}

static int proc_fork_exec_warmup(void *state) {
    (void)state;
    volatile int sink = 0;
    for (int i = 0; i < FORKS_PER_ITER / 10; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("/bin/true", "true", NULL);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        sink += WEXITSTATUS(status);
    }
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int proc_fork_exec_measure(void *state, measurement_t *result) {
    (void)state;
    struct timespec t0, t1;
    volatile int sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < FORKS_PER_ITER; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("/bin/true", "true", NULL);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        sink += WEXITSTATUS(status);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e6 / FORKS_PER_ITER; /* us/call */
    result->wall_seconds = elapsed;

    return 0;
}

static int proc_fork_exec_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_proc_fork_exec = {
    .name = "proc-fork-exec",
    .category = "C6",
    .description = "fork+exec+wait loop (UnixBench Process Creation exact equivalent)",
    .tier = 1,
    .primary_metric_name = "us/call",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = proc_fork_exec_init,
    .warmup = proc_fork_exec_warmup,
    .measure = proc_fork_exec_measure,
    .cleanup = proc_fork_exec_cleanup,
    .num_threads = 1,
};

SSB_BENCHMARK_REGISTER(bench_proc_fork_exec);
