#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define NUM_THREADS_PER_ITER 5000

/*
 * pthread create/join latency benchmark.
 *
 * Creates and joins NUM_THREADS_PER_ITER threads sequentially. Each thread
 * has the default 8MB stack, so this heavily exercises the kernel's mmap/
 * munmap path for thread stack allocation. On NUMA systems, stack placement
 * follows the default memory policy (typically local-node allocation).
 */

typedef struct {
    int dummy;
} proc_pthread_state_t;

static void *dummy_thread(void *arg) {
    (void)arg;
    return NULL;
}

static int proc_pthread_init(void **state) {
    proc_pthread_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int proc_pthread_warmup(void *state) {
    (void)state;
    pthread_t threads[100];
    for (int i = 0; i < 100; i++) {
        if (pthread_create(&threads[i], NULL, dummy_thread, NULL) != 0)
            break;
        pthread_join(threads[i], NULL);
    }
    return 0;
}

static int proc_pthread_measure(void *state, measurement_t *result) {
    (void)state;
    struct timespec t0, t1;
    int created = 0;

    pthread_t *threads = malloc(NUM_THREADS_PER_ITER * sizeof(pthread_t));
    if (!threads) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_THREADS_PER_ITER; i++) {
        if (pthread_create(&threads[i], NULL, dummy_thread, NULL) != 0)
            break;
        created++;
    }
    for (int i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    free(threads);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    if (created > 0)
        result->primary_metric = elapsed * 1e6 / created;
    else
        result->primary_metric = 0.0;
    result->wall_seconds = elapsed;
    return 0;
}

static int proc_pthread_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_proc_pthread = {
    .name = "proc-pthread",
    .category = "C6",
    .description = "pthread create/join latency (sequential, includes stack mmap/munmap)",
    .tier = 1,
    .primary_metric_name = "us/thread",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = proc_pthread_init,
    .warmup = proc_pthread_warmup,
    .measure = proc_pthread_measure,
    .cleanup = proc_pthread_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_proc_pthread);
