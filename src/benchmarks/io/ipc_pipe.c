#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#define MSG_SIZE 512
#define NUM_MSGS 500000

typedef struct {
    int pipe_fd[2];
    char *buffer;
    pid_t child_pid;
} ipc_pipe_state_t;

static int ipc_pipe_init(void **state) {
    ipc_pipe_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (pipe(s->pipe_fd) != 0) { free(s); return -1; }
    s->buffer = malloc(MSG_SIZE);
    if (!s->buffer) { close(s->pipe_fd[0]); close(s->pipe_fd[1]); free(s); return -1; }
    memset(s->buffer, 'P', MSG_SIZE);
    s->child_pid = 0;
    *state = s;
    return 0;
}

static int ipc_pipe_warmup(void *state) {
    ipc_pipe_state_t *s = (ipc_pipe_state_t *)state;
    /* Short warmup: fork child, exchange a few messages */
    int pipes[2];
    if (pipe(pipes) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(pipes[1]);
        char buf[MSG_SIZE];
        for (int i = 0; i < 1000; i++) {
            ssize_t n = read(pipes[0], buf, MSG_SIZE);
            if (n <= 0) break;
            /* echo back */
        }
        close(pipes[0]);
        _exit(0);
    }
    if (pid > 0) {
        close(pipes[0]);
        for (int i = 0; i < 1000; i++)
            write(pipes[1], s->buffer, MSG_SIZE);
        close(pipes[1]);
        waitpid(pid, NULL, 0);
    }
    return 0;
}

static int ipc_pipe_measure(void *state, measurement_t *result) {
    ipc_pipe_state_t *s = (ipc_pipe_state_t *)state;
    struct timespec t0, t1;

    /* Fork: child reads, parent writes — true pipe throughput */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: read side */
        close(s->pipe_fd[1]);
        char buf[MSG_SIZE];
        int64_t bytes = 0;
        while (bytes < (int64_t)NUM_MSGS * MSG_SIZE) {
            ssize_t n = read(s->pipe_fd[0], buf, MSG_SIZE);
            if (n <= 0) break;
            bytes += n;
        }
        close(s->pipe_fd[0]);
        _exit(0);
    }
    if (pid < 0) return -1;

    /* Parent: write side */
    close(s->pipe_fd[0]);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    int64_t total_bytes = 0;
    for (int i = 0; i < NUM_MSGS; i++) {
        write(s->pipe_fd[1], s->buffer, MSG_SIZE);
        total_bytes += MSG_SIZE;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    close(s->pipe_fd[1]);
    waitpid(pid, NULL, 0);

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
    .description = "Pipe throughput 512B messages (two-process pipe, UnixBench-style)",
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
    .num_threads = 1,  /* two-process via fork, not threads */
};
SSB_BENCHMARK_REGISTER(bench_ipc_pipe);
