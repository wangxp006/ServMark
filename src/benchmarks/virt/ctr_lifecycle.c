#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>

#define CLONE_ITERS 2000

typedef struct { char *stack; } ctr_lifecycle_state_t;

static int child_func(void *arg) { (void)arg; _exit(0); }

static int ctr_lifecycle_init(void **state) {
    ctr_lifecycle_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->stack = malloc(65536);
    if (!s->stack) { free(s); return -1; }
    *state = s; return 0;
}

static int ctr_lifecycle_warmup(void *state) {
    ctr_lifecycle_state_t *s = (ctr_lifecycle_state_t *)state;
    volatile int sink = 0;
    for (int i = 0; i < 20; i++) {
        pid_t pid = clone(child_func, s->stack + 65536,
                          SIGCHLD | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWPID, NULL);
        if (pid >= 0) { int st; waitpid(pid, &st, 0); if(WIFEXITED(st)) sink+=WEXITSTATUS(st); }
    }
    __asm__ __volatile__("":"+r"(sink)); return 0;
}

static int ctr_lifecycle_measure(void *state, measurement_t *result) {
    ctr_lifecycle_state_t *s = (ctr_lifecycle_state_t *)state;
    struct timespec t0, t1; int64_t total = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < CLONE_ITERS; i++) {
        pid_t pid = clone(child_func, s->stack + 65536,
                          SIGCHLD | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWPID, NULL);
        if (pid >= 0) { int st; waitpid(pid, &st, 0); total++; }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = el * 1e6 / (total > 0 ? total : 1);
    result->wall_seconds = el; return 0;
}

static int ctr_lifecycle_cleanup(void *state) {
    ctr_lifecycle_state_t *s = (ctr_lifecycle_state_t *)state;
    free(s->stack); free(s); return 0;
}

benchmark_t bench_ctr_lifecycle = {
    .name = "ctr-lifecycle", .category = "C15",
    .description = "Namespace clone create/destroy (NS+UTS+NET+PID, pre-alloc stack, no cgroups)",
    .tier = 1, .primary_metric_name = "us/container", .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS, .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC, .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = ctr_lifecycle_init, .warmup = ctr_lifecycle_warmup,
    .measure = ctr_lifecycle_measure, .cleanup = ctr_lifecycle_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_ctr_lifecycle);
