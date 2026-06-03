#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define FFT_N 2048
#define FLOPS_PER_FFT (5.0 * FFT_N * log2(FFT_N))

typedef struct {
    double *real, *imag;
    double *twiddle_cos, *twiddle_sin;
    int *bit_rev;
    double *work_real, *work_imag;
} fp_fft_state_t;

static void fft_radix2(double *real, double *imag, int n,
                       const double *cos_t, const double *sin_t, const int *rev) {
    for (int i = 0; i < n; i++) {
        int j = rev[i];
        if (i < j) {
            double tr = real[i]; real[i] = real[j]; real[j] = tr;
            double ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < half; j++) {
                double wr = cos_t[j * (n / len)];
                double wi = sin_t[j * (n / len)];
                double tr = real[i + j + half] * wr - imag[i + j + half] * wi;
                double ti = real[i + j + half] * wi + imag[i + j + half] * wr;
                real[i + j + half] = real[i + j] - tr;
                imag[i + j + half] = imag[i + j] - ti;
                real[i + j] += tr; imag[i + j] += ti;
            }
        }
    }
}

static int fp_fft_init(void **state) {
    fp_fft_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    int n = FFT_N;
    s->real = malloc(n*sizeof(double)); s->imag = malloc(n*sizeof(double));
    s->twiddle_cos = malloc(n*sizeof(double)); s->twiddle_sin = malloc(n*sizeof(double));
    s->bit_rev = malloc(n*sizeof(int));
    s->work_real = malloc(n*sizeof(double)); s->work_imag = calloc(n, sizeof(double));
    if (!s->real||!s->imag||!s->twiddle_cos||!s->twiddle_sin||!s->bit_rev||!s->work_real||!s->work_imag) {
        free(s->real); free(s->imag); free(s->twiddle_cos);
        free(s->twiddle_sin); free(s->bit_rev);
        free(s->work_real); free(s->work_imag); free(s); return -1;
    }
    for (int i = 0; i < n; i++) {
        double a = -2.0*M_PI*i/n;
        s->twiddle_cos[i]=cos(a); s->twiddle_sin[i]=sin(a);
    }
    for (int i = 0; i < n; i++) {
        s->bit_rev[i]=0;
        for (int b = 0; (1<<b) < n; b++)
            if (i & (1<<b)) s->bit_rev[i] |= (n>>(b+1));
    }
    for (int i = 0; i < n; i++) {
        s->real[i] = (double)rand()/RAND_MAX - 0.5; s->imag[i]=0.0;
    }
    *state = s; return 0;
}

static int fp_fft_warmup(void *state) {
    fp_fft_state_t *s = (fp_fft_state_t *)state;
    memcpy(s->work_real, s->real, FFT_N*sizeof(double));
    memset(s->work_imag, 0, FFT_N*sizeof(double));
    fft_radix2(s->work_real, s->work_imag, FFT_N, s->twiddle_cos, s->twiddle_sin, s->bit_rev);
    return 0;
}

static int fp_fft_measure(void *state, measurement_t *result) {
    fp_fft_state_t *s = (fp_fft_state_t *)state;
    struct timespec t0, t1;
    volatile double sink = 0.0;

    /* Work buffers pre-allocated in init(), no heap alloc in timed section */
    memcpy(s->work_real, s->real, FFT_N*sizeof(double));
    memset(s->work_imag, 0, FFT_N*sizeof(double));

    clock_gettime(CLOCK_MONOTONIC, &t0);
    fft_radix2(s->work_real, s->work_imag, FFT_N, s->twiddle_cos, s->twiddle_sin, s->bit_rev);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    /* Inverse FFT verification: negate imag twiddle for inverse, apply 1/N scaling */
    for (int i = 0; i < FFT_N; i++) s->twiddle_sin[i] = -s->twiddle_sin[i];
    fft_radix2(s->work_real, s->work_imag, FFT_N, s->twiddle_cos, s->twiddle_sin, s->bit_rev);
    for (int i = 0; i < FFT_N; i++) {
        s->work_real[i] /= FFT_N; s->work_imag[i] /= FFT_N;
        sink += fabs(s->work_real[i] - s->real[i]);
    }
    for (int i = 0; i < FFT_N; i++) s->twiddle_sin[i] = -s->twiddle_sin[i];
    __asm__ __volatile__("" : "+r"(sink));

    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric = FLOPS_PER_FFT / el;
    result->wall_seconds = el;
    return 0;
}

static int fp_fft_cleanup(void *state) {
    fp_fft_state_t *s = (fp_fft_state_t *)state;
    free(s->real); free(s->imag); free(s->twiddle_cos); free(s->twiddle_sin);
    free(s->bit_rev); free(s->work_real); free(s->work_imag); free(s);
    return 0;
}

benchmark_t bench_fp_fft = {
    .name="fp-fft", .category="C2",
    .description="Radix-2 FFT N=2048 (pre-allocated work buffers, no heap in timed path)",
    .tier=1, .primary_metric_name="FLOPS", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=fp_fft_init, .warmup=fp_fft_warmup,
    .measure=fp_fft_measure, .cleanup=fp_fft_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_fp_fft);
