#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MSG_SIZE 512
#define NUM_MSGS 500000

typedef struct {
    int pipe_fd[2];
    char *buffer;
} ipc_pipe_state_t;

static int ipc_pipe_init(void **state) {
    ipc_pipe_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (pipe(s->pipe_fd) != 0) { free(s); return -1; }
    s->buffer = malloc(MSG_SIZE);
    if (!s->buffer) { close(s->pipe_fd[0]); close(s->pipe_fd[1]); free(s); return -1; }
    memset(s->buffer, 'P', MSG_SIZE);
    *state = s;
    return 0;
}

static int ipc_pipe_warmup(void *state) {
    ipc_pipe_state_t *s = (ipc_pipe_state_t *)state;
    for (int i = 0; i < 10000; i++) {
        write(s->pipe_fd[1], s->buffer, MSG_SIZE);
        read(s->pipe_fd[0], s->buffer, MSG_SIZE);
    }
    return 0;
}

static int ipc_pipe_measure(void *state, measurement_t *result) {
    ipc_pipe_state_t *s = (ipc_pipe_state_t *)state;
    struct timespec t0, t1;
    int64_t total_bytes = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_MSGS; i++) {
        write(s->pipe_fd[1], s->buffer, MSG_SIZE);
        read(s->pipe_fd[0], s->buffer, MSG_SIZE);
        total_bytes += MSG_SIZE * 2;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_bytes / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int ipc_pipe_cleanup(void *state) {
    ipc_pipe_state_t *s = (ipc_pipe_state_t *)state;
    close(s->pipe_fd[0]); close(s->pipe_fd[1]);
    free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_ipc_pipe = {
    .name = "ipc-pipe",
    .category = "C11",
    .description = "Pipe throughput 512B messages (UnixBench Pipe Throughput exact equivalent)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = ipc_pipe_init,
    .warmup = ipc_pipe_warmup,
    .measure = ipc_pipe_measure,
    .cleanup = ipc_pipe_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_ipc_pipe);
