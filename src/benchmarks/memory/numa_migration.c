#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <numa.h>
#include <pthread.h>

#define BUF_MB 32
#define BUF_SIZE (BUF_MB * 1024 * 1024)
#define ITERATIONS 100

typedef struct {
    char *buffer;
    int nodes[2];
} numa_migration_state_t;

static int numa_migration_init(void **state) {
    numa_migration_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    if (numa_available() < 0) { free(s); return -1; }
    int max_node = numa_max_node();
    s->nodes[0] = 0;
    s->nodes[1] = max_node > 0 ? 1 : 0;
    s->buffer = numa_alloc_onnode(BUF_SIZE, s->nodes[0]);
    if (!s->buffer) { free(s); return -1; }
    memset(s->buffer, 0xCC, BUF_SIZE);
    *state = s;
    return 0;
}

static int numa_migration_warmup(void *state) {
    numa_migration_state_t *s = (numa_migration_state_t *)state;
    /* Touch pages on node 0 to establish mapping */
    volatile char sink = 0;
    for (size_t i = 0; i < BUF_SIZE / 10; i += 4096)
        sink += s->buffer[i];
    /* Migrate to node 1 if available */
    if (s->nodes[1] != s->nodes[0]) {
        numa_tonode_memory(s->buffer, BUF_SIZE, s->nodes[1]);
        for (size_t i = 0; i < BUF_SIZE / 10; i += 4096)
            sink += s->buffer[i];
    }
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int numa_migration_measure(void *state, measurement_t *result) {
    numa_migration_state_t *s = (numa_migration_state_t *)state;
    struct timespec t0, t1;
    int migrations = 0;
    volatile char sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < ITERATIONS; i++) {
        int dst = s->nodes[i % 2];
        numa_tonode_memory(s->buffer, BUF_SIZE, dst);
        /* Touch pages to fault them to the new node */
        for (size_t off = 0; off < BUF_SIZE; off += 4096)
            sink += s->buffer[off];
        migrations++;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)migrations / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int numa_migration_cleanup(void *state) {
    numa_migration_state_t *s = (numa_migration_state_t *)state;
    if (s->buffer) numa_free(s->buffer, BUF_SIZE);
    free(s);
    return 0;
}

benchmark_t bench_numa_migration = {
    .name = "numa-migration",
    .category = "C5",
    .description = "NUMA page migration throughput",
    .tier = 1,
    .primary_metric_name = "migrations/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = numa_migration_init,
    .warmup = numa_migration_warmup,
    .measure = numa_migration_measure,
    .cleanup = numa_migration_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_numa_migration);
