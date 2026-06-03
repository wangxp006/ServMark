#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MSG_SIZE 1024
#define NUM_MSGS 100000

typedef struct {
    int fds[2];
    char *buffer;
} ipc_unix_state_t;

static int ipc_unix_init(void **state) {
    ipc_unix_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s->fds) != 0) {
        free(s); return -1;
    }
    s->buffer = malloc(MSG_SIZE);
    if (!s->buffer) {
        close(s->fds[0]); close(s->fds[1]); free(s);
        return -1;
    }
    memset(s->buffer, 'U', MSG_SIZE);
    *state = s;
    return 0;
}

static int ipc_unix_warmup(void *state) {
    ipc_unix_state_t *s = (ipc_unix_state_t *)state;
    for (int i = 0; i < 5000; i++) {
        send(s->fds[0], s->buffer, MSG_SIZE, 0);
        recv(s->fds[1], s->buffer, MSG_SIZE, 0);
    }
    return 0;
}

static int ipc_unix_measure(void *state, measurement_t *result) {
    ipc_unix_state_t *s = (ipc_unix_state_t *)state;
    struct timespec t0, t1;
    int64_t total_bytes = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_MSGS; i++) {
        ssize_t ns = send(s->fds[0], s->buffer, MSG_SIZE, 0);
        ssize_t nr = recv(s->fds[1], s->buffer, MSG_SIZE, MSG_WAITALL);
        if (ns == MSG_SIZE && nr == MSG_SIZE)
            total_bytes += MSG_SIZE * 2;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_bytes / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int ipc_unix_cleanup(void *state) {
    ipc_unix_state_t *s = (ipc_unix_state_t *)state;
    close(s->fds[0]); close(s->fds[1]);
    free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_ipc_unix = {
    .name = "ipc-unix",
    .category = "C11",
    .description = "AF_UNIX socketpair stream throughput",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = ipc_unix_init,
    .warmup = ipc_unix_warmup,
    .measure = ipc_unix_measure,
    .cleanup = ipc_unix_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_ipc_unix);
