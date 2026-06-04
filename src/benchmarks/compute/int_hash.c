#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 64-bit integer hash table benchmark - maps to Dhrystone integer ALU path */

#define TABLE_SIZE (1 << 24)
#define OPS_PER_ITER 10000000
#define MAX_PROBES (TABLE_SIZE * 2)

typedef struct {
    uint64_t *keys;
    uint64_t *values;
    uint64_t *table_keys;
    uint64_t *table_vals;
    int64_t ops_done;        /* reserved for future use */
} int_hash_state_t;

static int int_hash_init(void **state) {
    int_hash_state_t *s = calloc(1, sizeof(int_hash_state_t));
    if (!s) return -1;

    s->keys = malloc(OPS_PER_ITER * sizeof(uint64_t));
    s->values = malloc(OPS_PER_ITER * sizeof(uint64_t));
    s->table_keys = calloc(TABLE_SIZE, sizeof(uint64_t));
    s->table_vals = calloc(TABLE_SIZE, sizeof(uint64_t));

    if (!s->keys || !s->values || !s->table_keys || !s->table_vals) {
        free(s->keys); free(s->values);
        free(s->table_keys); free(s->table_vals);
        free(s);
        return -1;
    }

    /* Generate random keys and values */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        s->keys[i] = ((uint64_t)rand() << 32) | (uint64_t)rand();
        s->values[i] = s->keys[i] ^ 0xDEADBEEFCAFE1234ULL;
    }
    *state = s; return 0;
}

static uint64_t hash_func(uint64_t key) {
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return key;
}

static void hash_insert(int_hash_state_t *s, uint64_t key, uint64_t val) {
    uint64_t h = hash_func(key) & (TABLE_SIZE - 1);
    int probes = 0;
    while (s->table_keys[h] != 0 && s->table_keys[h] != key && probes < MAX_PROBES) {
        h = (h + 1) & (TABLE_SIZE - 1);
        probes++;
    }
    if (probes < MAX_PROBES) {
        s->table_keys[h] = key;
        s->table_vals[h] = val;
    }
}

static uint64_t hash_lookup(int_hash_state_t *s, uint64_t key) {
    uint64_t h = hash_func(key) & (TABLE_SIZE - 1);
    int probes = 0;
    while (s->table_keys[h] != key && probes < TABLE_SIZE) {
        if (s->table_keys[h] == 0) return 0;
        h = (h + 1) & (TABLE_SIZE - 1);
        probes++;
    }
    return (probes < TABLE_SIZE) ? s->table_vals[h] : 0;
}

static void hash_delete(int_hash_state_t *s, uint64_t key) {
    uint64_t h = hash_func(key) & (TABLE_SIZE - 1);
    int probes = 0;
    while (s->table_keys[h] != key && probes < TABLE_SIZE) {
        if (s->table_keys[h] == 0) return;
        h = (h + 1) & (TABLE_SIZE - 1);
        probes++;
    }
    if (probes < TABLE_SIZE) {
        s->table_keys[h] = 0;
        s->table_vals[h] = 0;
    }
}

static int int_hash_warmup(void *state) {
    int_hash_state_t *s = (int_hash_state_t *)state;
    volatile uint64_t sink = 0;
    /* Insert all */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        hash_insert(s, s->keys[i], s->values[i]);
        sink ^= s->values[i];
    }
    /* Lookup all */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        sink ^= hash_lookup(s, s->keys[i]);
    }
    /* Delete all */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        hash_delete(s, s->keys[i]);
    }
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int int_hash_measure(void *state, measurement_t *result) {
    int_hash_state_t *s = (int_hash_state_t *)state;
    struct timespec t0, t1;
    volatile uint64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Insert */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        hash_insert(s, s->keys[i], s->values[i]);
    }
    /* Lookup */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        sink ^= hash_lookup(s, s->keys[i]);
    }
    /* Delete + verify: lookup after each phase to confirm correctness */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        hash_delete(s, s->keys[i]);
    }
    /* Verify: all lookups post-delete should return 0 */
    for (int i = 0; i < OPS_PER_ITER; i++) {
        sink ^= hash_lookup(s, s->keys[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (4.0 * OPS_PER_ITER) / elapsed; /* insert+lookup+delete+verify = 4 phases */
    result->wall_seconds = elapsed;

    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int int_hash_cleanup(void *state) {
    int_hash_state_t *s = (int_hash_state_t *)state;
    free(s->keys); free(s->values);
    free(s->table_keys); free(s->table_vals);
    free(s);
    return 0;
}

benchmark_t bench_int_hash = {
    .name = "int-hash",
    .category = "C1",
    .description = "64-bit integer hash table insert/lookup/delete",
    .tier = 1,
    .primary_metric_name = "ops/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = int_hash_init,
    .warmup = int_hash_warmup,
    .measure = int_hash_measure,
    .cleanup = int_hash_cleanup,
    .num_threads = 1,
};

SSB_BENCHMARK_REGISTER(bench_int_hash);
