#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 512
#define FLOPS_PER_ITER (2ULL * N * N * N)

typedef struct {
    double *A, *B, *C;
} fp_gemm_state_t;

static int fp_gemm_init(void **state) {
    fp_gemm_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->A = malloc(N * N * sizeof(double));
    s->B = malloc(N * N * sizeof(double));
    s->C = malloc(N * N * sizeof(double));
    if (!s->A || !s->B || !s->C) {
        free(s->A); free(s->B); free(s->C); free(s);
        return -1;
    }
    srand(time(NULL));
    for (int i = 0; i < N * N; i++) {
        s->A[i] = (double)rand() / RAND_MAX;
        s->B[i] = (double)rand() / RAND_MAX;
        s->C[i] = 0.0;
    }
    *state = s;
    return 0;
}

static int fp_gemm_warmup(void *state) {
    fp_gemm_state_t *s = (fp_gemm_state_t *)state;
    volatile double sink = 0.0;
    int n = N / 4;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            for (int k = 0; k < n; k++)
                sink += s->A[i * N + k] * s->B[k * N + j];
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int fp_gemm_measure(void *state, measurement_t *result) {
    fp_gemm_state_t *s = (fp_gemm_state_t *)state;
    struct timespec t0, t1;
    volatile double sink = 0.0;

    /* Zero C */
    memset(s->C, 0, N * N * sizeof(double));

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            double aik = s->A[i * N + k];
            for (int j = 0; j < N; j++) {
                s->C[i * N + j] += aik * s->B[k * N + j];
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Anti-DCE: verify a result */
    for (int i = 0; i < N; i++)
        sink += s->C[i];

    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)FLOPS_PER_ITER / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fp_gemm_cleanup(void *state) {
    fp_gemm_state_t *s = (fp_gemm_state_t *)state;
    free(s->A); free(s->B); free(s->C); free(s);
    return 0;
}

benchmark_t bench_fp_gemm = {
    .name = "fp-gemm",
    .category = "C2",
    .description = "DGEMM matrix multiply N=512 (Whetstone modernized)",
    .tier = 1,
    .primary_metric_name = "FLOPS",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fp_gemm_init,
    .warmup = fp_gemm_warmup,
    .measure = fp_gemm_measure,
    .cleanup = fp_gemm_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fp_gemm);
