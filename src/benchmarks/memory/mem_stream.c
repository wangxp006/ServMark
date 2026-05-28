#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define ARRAY_SIZE (16 * 1024 * 1024)

typedef struct {
    double *a, *b, *c;
    int nthreads;
} mem_stream_state_t;

typedef struct {
    double *a, *b, *c;
    int tid, nthreads;
    size_t chunk;
} stream_thread_arg_t;

static void *stream_worker(void *arg) {
    stream_thread_arg_t *ta = (stream_thread_arg_t *)arg;
    size_t start = ta->tid * ta->chunk;
    size_t end = start + ta->chunk;
    double scalar = 3.0;
    for (size_t i = start; i < end; i++)
        ta->c[i] = ta->a[i] + scalar * ta->b[i];
    return NULL;
}

static int mem_stream_init(void **state) {
    mem_stream_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->nthreads = SSB_NUM_CPUS();
    if (s->nthreads < 1) s->nthreads = 1;
    s->a = malloc(ARRAY_SIZE * sizeof(double));
    s->b = malloc(ARRAY_SIZE * sizeof(double));
    s->c = malloc(ARRAY_SIZE * sizeof(double));
    if (!s->a || !s->b || !s->c) {
        free(s->a); free(s->b); free(s->c); free(s);
        return -1;
    }
    srand(time(NULL));
    for (size_t i = 0; i < ARRAY_SIZE; i++) {
        s->a[i] = (double)rand() / RAND_MAX;
        s->b[i] = (double)rand() / RAND_MAX;
    }
    *state = s;
    return 0;
}

static int mem_stream_warmup(void *state) {
    mem_stream_state_t *s = (mem_stream_state_t *)state;
    volatile double sink = 0.0;
    double scalar = 3.0;
    for (size_t i = 0; i < ARRAY_SIZE / 10; i++)
        sink += s->c[i] = s->a[i] + scalar * s->b[i];
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int mem_stream_measure(void *state, measurement_t *result) {
    mem_stream_state_t *s = (mem_stream_state_t *)state;
    struct timespec t0, t1;
    int n = s->nthreads;
    pthread_t *threads = malloc(n * sizeof(pthread_t));
    stream_thread_arg_t *args = malloc(n * sizeof(stream_thread_arg_t));
    size_t chunk = ARRAY_SIZE / n;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int t = 0; t < n; t++) {
        args[t] = (stream_thread_arg_t){s->a, s->b, s->c, t, n, chunk};
        pthread_create(&threads[t], NULL, stream_worker, &args[t]);
    }
    for (int t = 0; t < n; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    free(threads); free(args);
    volatile double sink = s->c[0] + s->c[ARRAY_SIZE - 1];
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double total_bytes = 3.0 * ARRAY_SIZE * sizeof(double);
    memset(result, 0, sizeof(*result));
    result->primary_metric = total_bytes / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int mem_stream_cleanup(void *state) {
    mem_stream_state_t *s = (mem_stream_state_t *)state;
    free(s->a); free(s->b); free(s->c); free(s);
    return 0;
}

benchmark_t bench_mem_stream = {
    .name = "mem-stream",
    .category = "C4",
    .description = "STREAM Triad (auto-scaled to NCPU threads)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = mem_stream_init,
    .warmup = mem_stream_warmup,
    .measure = mem_stream_measure,
    .cleanup = mem_stream_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_mem_stream);
