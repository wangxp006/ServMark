#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define H 128
#define W 128
#define K 3
#define FLOP_PER_CONV (2.0 * (H-K+1) * (W-K+1) * K * K)

typedef struct {
    float *input, *kernel, *output;
} fp_conv_state_t;

static int fp_conv_init(void **state) {
    fp_conv_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->input = malloc(H * W * sizeof(float));
    s->kernel = malloc(K * K * sizeof(float));
    s->output = malloc((H-K+1) * (W-K+1) * sizeof(float));
    if (!s->input || !s->kernel || !s->output) {
        free(s->input); free(s->kernel); free(s->output); free(s);
        return -1;
    }
    for (int i = 0; i < H * W; i++)
        s->input[i] = (float)rand() / RAND_MAX;
    for (int i = 0; i < K * K; i++)
        s->kernel[i] = (float)rand() / RAND_MAX;
    *state = s;
    return 0;
}

static int fp_conv_warmup(void *state) {
    fp_conv_state_t *s = (fp_conv_state_t *)state;
    volatile float sink = 0.0f;
    int oh = H - K + 1, ow = W - K + 1;
    for (int y = 0; y < oh / 4; y++)
        for (int x = 0; x < ow / 4; x++)
            for (int ky = 0; ky < K; ky++)
                for (int kx = 0; kx < K; kx++)
                    sink += s->input[(y+ky)*W+(x+kx)] * s->kernel[ky*K+kx];
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int fp_conv_measure(void *state, measurement_t *result) {
    fp_conv_state_t *s = (fp_conv_state_t *)state;
    struct timespec t0, t1;
    volatile float sink = 0.0f;
    int oh = H - K + 1, ow = W - K + 1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* 100 convolution passes */
    for (int pass = 0; pass < 100; pass++) {
        for (int y = 0; y < oh; y++) {
            for (int x = 0; x < ow; x++) {
                float acc = 0.0f;
                for (int ky = 0; ky < K; ky++)
                    for (int kx = 0; kx < K; kx++)
                        acc += s->input[(y+ky)*W+(x+kx)] * s->kernel[ky*K+kx];
                s->output[y*ow+x] = acc;
                sink += acc;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = 100.0 * FLOP_PER_CONV / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fp_conv_cleanup(void *state) {
    fp_conv_state_t *s = (fp_conv_state_t *)state;
    free(s->input); free(s->kernel); free(s->output); free(s);
    return 0;
}

benchmark_t bench_fp_conv = {
    .name = "fp-conv",
    .category = "C2",
    .description = "3x3 convolution 128x128 (Whetstone vectorized modernized)",
    .tier = 1,
    .primary_metric_name = "FLOPS",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fp_conv_init,
    .warmup = fp_conv_warmup,
    .measure = fp_conv_measure,
    .cleanup = fp_conv_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fp_conv);
