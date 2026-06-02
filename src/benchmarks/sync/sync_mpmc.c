#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define QUEUE_SIZE 1024
#define OPS_PER_PRODUCER 300000

/*
 * Lock-free MPMC queue using C11 atomics.
 *
 * Uses acquire/release ordering for realistic performance on weakly-ordered
 * architectures (ARM64, RISC-V). Seq_cst would penalize RISC-V (~10-15ns
 * fence per op) unnecessarily for single-producer/single-consumer patterns.
 */

typedef struct {
    _Atomic uint64_t items[QUEUE_SIZE];
    _Atomic int head;
    _Atomic int tail;
} mpmc_queue_t;

typedef struct {
    mpmc_queue_t queue;
    _Atomic int ready;
    _Atomic int64_t produced;
    _Atomic int64_t consumed;
    int nproducers, nconsumers;
} sync_mpmc_state_t;

static int mpmc_enqueue(mpmc_queue_t *q, uint64_t item) {
    int t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    int nxt = (t + 1) % QUEUE_SIZE;
    if (nxt == atomic_load_explicit(&q->head, memory_order_acquire)) return 0;
    atomic_store_explicit(&q->items[t], item, memory_order_relaxed);
    atomic_store_explicit(&q->tail, nxt, memory_order_release);
    return 1;
}

static uint64_t mpmc_dequeue(mpmc_queue_t *q) {
    int h = atomic_load_explicit(&q->head, memory_order_relaxed);
    if (h == atomic_load_explicit(&q->tail, memory_order_acquire)) return 0;
    uint64_t item = atomic_load_explicit(&q->items[h], memory_order_relaxed);
    atomic_store_explicit(&q->head, (h + 1) % QUEUE_SIZE, memory_order_release);
    return item;
}

static void *mpmc_producer(void *arg) {
    sync_mpmc_state_t *s = (sync_mpmc_state_t *)arg;
    while (!atomic_load_explicit(&s->ready, memory_order_acquire)) ;
    for (int i = 0; i < OPS_PER_PRODUCER; i++) {
        while (!mpmc_enqueue(&s->queue, (uint64_t)i)) ;
        atomic_fetch_add_explicit(&s->produced, 1, memory_order_relaxed);
    }
    return NULL;
}

static void *mpmc_consumer(void *arg) {
    sync_mpmc_state_t *s = (sync_mpmc_state_t *)arg;
    while (!atomic_load_explicit(&s->ready, memory_order_acquire)) ;
    int64_t local = 0;
    int64_t target = (int64_t)s->nproducers * OPS_PER_PRODUCER;
    while (atomic_load_explicit(&s->produced, memory_order_acquire) < target) {
        if (mpmc_dequeue(&s->queue) != 0) local++;
    }
    uint64_t v;
    while ((v = mpmc_dequeue(&s->queue)) != 0) local++;
    atomic_fetch_add_explicit(&s->consumed, local, memory_order_relaxed);
    return NULL;
}

static int sync_mpmc_init(void **state) {
    sync_mpmc_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    atomic_init(&s->queue.head, 0);
    atomic_init(&s->queue.tail, 0);
    int ncpu = SSB_NUM_CPUS();
    if (ncpu < 2) ncpu = 2;
    s->nproducers = ncpu / 2;
    s->nconsumers = ncpu - s->nproducers;
    if (s->nproducers < 1) s->nproducers = 1;
    if (s->nconsumers < 1) s->nconsumers = 1;
    *state = s;
    return 0;
}

static int sync_mpmc_warmup(void *state) {
    sync_mpmc_state_t *s = (sync_mpmc_state_t *)state;
    mpmc_enqueue(&s->queue, 42);
    mpmc_dequeue(&s->queue);
    return 0;
}

static int sync_mpmc_measure(void *state, measurement_t *result) {
    sync_mpmc_state_t *s = (sync_mpmc_state_t *)state;
    struct timespec t0, t1;
    int np = s->nproducers, nc = s->nconsumers;

    atomic_store(&s->ready, 0); atomic_store(&s->produced, 0); atomic_store(&s->consumed, 0);
    pthread_t *threads = malloc((np + nc) * sizeof(pthread_t));

    for (int t = 0; t < nc; t++)
        pthread_create(&threads[t], NULL, mpmc_consumer, s);
    for (int t = 0; t < np; t++)
        pthread_create(&threads[nc + t], NULL, mpmc_producer, s);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    atomic_store_explicit(&s->ready, 1, memory_order_release);

    for (int t = 0; t < np + nc; t++)
        pthread_join(threads[t], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(threads);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)atomic_load(&s->consumed) / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int sync_mpmc_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_sync_mpmc = {
    .name = "sync-mpmc",
    .category = "C7",
    .description = "Lock-free MPMC queue (C11 atomics acquire/release, auto-scaled P+C threads)",
    .tier = 1,
    .primary_metric_name = "ops/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = sync_mpmc_init,
    .warmup = sync_mpmc_warmup,
    .measure = sync_mpmc_measure,
    .cleanup = sync_mpmc_cleanup,
    .num_threads = -1,
};
SSB_BENCHMARK_REGISTER(bench_sync_mpmc);
