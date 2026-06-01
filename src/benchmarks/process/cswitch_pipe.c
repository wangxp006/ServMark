#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define SWITCHES_PER_ITER 1000000

/* UnixBench Pipe-based Context Switching exact equivalent:
 * Two threads ping-pong a byte through a pipe */

typedef struct {
    int pipe_fd[2];
    pthread_t thread;
    volatile bool running;
    volatile bool thread_ready;
    int64_t switch_count;
} cswitch_pipe_state_t;

static void *ping_thread(void *arg) {
    cswitch_pipe_state_t *s = (cswitch_pipe_state_t *)arg;
    char c = 'x';
    s->thread_ready = true;

    for (int64_t i = 0; i < SWITCHES_PER_ITER / 2; i++) {
        if (write(s->pipe_fd[1], &c, 1) != 1) break;
        if (read(s->pipe_fd[0], &c, 1) != 1) break;
        __sync_fetch_and_add(&s->switch_count, 2);
    }
    return NULL;
}

static int cswitch_pipe_init(void **state) {
    cswitch_pipe_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (pipe(s->pipe_fd) != 0) { free(s); return -1; }
    *state = s;
    return 0;
}

static int cswitch_pipe_warmup(void *state) {
    cswitch_pipe_state_t *s = (cswitch_pipe_state_t *)state;
    /* Quick warmup */
    char c = 'x';
    for (int i = 0; i < 10000; i++) {
        write(s->pipe_fd[1], &c, 1);
        read(s->pipe_fd[0], &c, 1);
    }
    return 0;
}

static int cswitch_pipe_measure(void *state, measurement_t *result) {
    cswitch_pipe_state_t *s = (cswitch_pipe_state_t *)state;
    s->running = true;
    s->thread_ready = false;
    s->switch_count = 0;

    /* Pre-create the ping thread before starting the timer */
    pthread_create(&s->thread, NULL, ping_thread, s);

    /* Wait for thread to be ready, then start timing */
    while (!s->thread_ready) ;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Pong: read and write back */
    char c = 'y';
    for (int64_t i = 0; i < SWITCHES_PER_ITER / 2; i++) {
        if (read(s->pipe_fd[0], &c, 1) != 1) break;
        if (write(s->pipe_fd[1], &c, 1) != 1) break;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    pthread_join(s->thread, NULL);
    s->running = false;

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)s->switch_count / elapsed; /* switches/sec */
    result->wall_seconds = elapsed;

    return 0;
}

static int cswitch_pipe_cleanup(void *state) {
    cswitch_pipe_state_t *s = (cswitch_pipe_state_t *)state;
    close(s->pipe_fd[0]);
    close(s->pipe_fd[1]);
    free(s);
    return 0;
}

benchmark_t bench_cswitch_pipe = {
    .name = "cswitch-pipe-ping",
    .category = "C8",
    .description = "Pipe-based context switching (UnixBench Pipe Ctx Switch exact equivalent)",
    .tier = 1,
    .primary_metric_name = "switches/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = cswitch_pipe_init,
    .warmup = cswitch_pipe_warmup,
    .measure = cswitch_pipe_measure,
    .cleanup = cswitch_pipe_cleanup,
    .num_threads = 2,
};

SSB_BENCHMARK_REGISTER(bench_cswitch_pipe);
