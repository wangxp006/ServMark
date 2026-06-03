#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define VEC_DIM 768
#define NUM_VECTORS 2000
#define FLOPS_PER_DOT (2.0 * VEC_DIM)

typedef struct {
    float *vectors;
    float *query;
} fp_distance_state_t;

static int fp_distance_init(void **state) {
    fp_distance_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->vectors = malloc(NUM_VECTORS * VEC_DIM * sizeof(float));
    s->query = malloc(VEC_DIM * sizeof(float));
    if (!s->vectors || !s->query) {
        free(s->vectors); free(s->query); free(s);
        return -1;
    }
    for (int i = 0; i < NUM_VECTORS * VEC_DIM; i++)
        s->vectors[i] = (float)rand() / RAND_MAX;
    for (int i = 0; i < VEC_DIM; i++)
        s->query[i] = (float)rand() / RAND_MAX;
    *state = s;
    return 0;
}

static int fp_distance_warmup(void *state) {
    fp_distance_state_t *s = (fp_distance_state_t *)state;
    volatile float sink = 0.0f;
    for (int i = 0; i < 100; i++) {
        float dp = 0.0f;
        for (int d = 0; d < VEC_DIM; d++)
            dp += s->vectors[i * VEC_DIM + d] * s->query[d];
        sink += dp;
    }
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int fp_distance_measure(void *state, measurement_t *result) {
    fp_distance_state_t *s = (fp_distance_state_t *)state;
    struct timespec t0, t1;
    volatile float sink = 0.0f;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_VECTORS; i++) {
        float dp = 0.0f, sq_norm_a = 0.0f, sq_norm_b = 0.0f;
        for (int d = 0; d < VEC_DIM; d++) {
            float va = s->vectors[i * VEC_DIM + d];
            float vb = s->query[d];
            dp += va * vb;
            sq_norm_a += va * va;
            sq_norm_b += vb * vb;
        }
        float cosine = dp / (sqrtf(sq_norm_a) * sqrtf(sq_norm_b) + 1e-10f);
        sink += cosine;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    double total_flops = (double)NUM_VECTORS * (6.0 * VEC_DIM + 5.0); /* 3 FMA/iter + sqrt*2 + div */
    result->primary_metric = total_flops / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fp_distance_cleanup(void *state) {
    fp_distance_state_t *s = (fp_distance_state_t *)state;
    free(s->vectors); free(s->query); free(s);
    return 0;
}

benchmark_t bench_fp_distance = {
    .name = "fp-distance",
    .category = "C2",
    .description = "Vector dot product + cosine distance dim=768 (ML embedding ops)",
    .tier = 1,
    .primary_metric_name = "FLOPS",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fp_distance_init,
    .warmup = fp_distance_warmup,
    .measure = fp_distance_measure,
    .cleanup = fp_distance_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fp_distance);
