#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#define SWITCHES_PER_ITER 1000000

/*
 * Pipe-based context switching benchmark (UnixBench Pipe Ctx Switch equivalent).
 *
 * Two threads ping-pong a byte through a pipe. On modern kernels the default
 * pipe capacity is 64KB (16 pages), so 1-byte writes never fill the buffer
 * and reads never block on empty — the pipe acts as a signalling channel
 * whose syscall entry/exit cost may dominate context switch latency.
 */

typedef struct {
    int pipe_fd[2];
    pthread_t thread;
    _Atomic bool thread_ready;
    _Atomic bool thread_failed;
    _Atomic int64_t switch_count;
} cswitch_pipe_state_t;

static void *ping_thread(void *arg) {
    cswitch_pipe_state_t *s = (cswitch_pipe_state_t *)arg;
    char c = 'x';
    atomic_store_explicit(&s->thread_ready, true, memory_order_release);

    for (int i = 0; i < SWITCHES_PER_ITER / 2; i++) {
        if (write(s->pipe_fd[1], &c, 1) != 1) {
            atomic_store_explicit(&s->thread_failed, true, memory_order_relaxed);
            break;
        }
        if (read(s->pipe_fd[0], &c, 1) != 1) {
            atomic_store_explicit(&s->thread_failed, true, memory_order_relaxed);
            break;
        }
        atomic_fetch_add_explicit(&s->switch_count, 2, memory_order_relaxed);
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
    char c = 'x';
    for (int i = 0; i < 10000; i++) {
        write(s->pipe_fd[1], &c, 1);
        read(s->pipe_fd[0], &c, 1);
    }
    return 0;
}

static int cswitch_pipe_measure(void *state, measurement_t *result) {
    cswitch_pipe_state_t *s = (cswitch_pipe_state_t *)state;
    int ret;

    atomic_store(&s->thread_ready, false);
    atomic_store(&s->thread_failed, false);
    atomic_store(&s->switch_count, 0);

    ret = pthread_create(&s->thread, NULL, ping_thread, s);
    if (ret != 0) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    /* Wait for thread to be ready, then start timing */
    while (!atomic_load(&s->thread_ready))
        ;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Pong: read and write back */
    char c = 'y';
    for (int i = 0; i < SWITCHES_PER_ITER / 2; i++) {
        if (read(s->pipe_fd[0], &c, 1) != 1) break;
        if (write(s->pipe_fd[1], &c, 1) != 1) break;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    pthread_join(s->thread, NULL);

    if (atomic_load(&s->thread_failed)) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)atomic_load(&s->switch_count) / elapsed;
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
