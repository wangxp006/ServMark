#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <zstd.h>

#define DATA_MB 64
#define DATA_SIZE (DATA_MB * 1024 * 1024)

/* Maximum expected formatted line length from snprintf below.
 * Format: "%s %s %s %d %s %s %s\n" with up to 7*13-char words + 10-digit int.
 * Conservatively: 7*13 + 10 + 7 + 1 = 109, rounded up to 128 for safety. */
#define MAX_LINE_LEN 128
#define ZSTD_COMPRESS_LEVEL 3

typedef struct {
    uint8_t *original;
    size_t orig_size;
    uint8_t *compressed;
    size_t comp_capacity;
} crypto_zstd_state_t;

static int crypto_zstd_init(void **state) {
    crypto_zstd_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->original = malloc(DATA_SIZE);
    s->comp_capacity = ZSTD_compressBound(DATA_SIZE);
    s->compressed = malloc(s->comp_capacity);
    if (!s->original || !s->compressed) {
        free(s->original); free(s->compressed); free(s);
        return -1;
    }
    /*
     * Generate text-like data for compression. Uses a fixed vocabulary of 13
     * words with rand() (deterministic default seed per C standard) to produce
     * repeatable, compressible synthetic text. rand() is acceptable here for
     * benchmark payload generation — never use rand() for cryptographic keys.
     */
    const char *words[] = {"the","quick","brown","fox","jumps","over","lazy","dog",
                           "benchmark","compress","decompress","throughput","test"};
    char *p = (char *)s->original;
    int remaining = DATA_SIZE;
    while (remaining > MAX_LINE_LEN) {
        int n = snprintf(p, remaining, "%s %s %s %d %s %s %s\n",
                words[rand()%13], words[rand()%13], words[rand()%13],
                rand(), words[rand()%13], words[rand()%13], words[rand()%13]);
        if (n < 0 || n >= remaining) break;
        p += n; remaining -= n;
    }
    s->orig_size = (size_t)(p - (char *)s->original);
    *state = s;
    return 0;
}

static int crypto_zstd_warmup(void *state) {
    crypto_zstd_state_t *s = (crypto_zstd_state_t *)state;
    uint8_t *tmp = malloc(s->comp_capacity);
    uint8_t *decomp = malloc(s->orig_size);
    if (!tmp || !decomp) {
        free(tmp); free(decomp);
        return -1;
    }
    size_t csize = ZSTD_compress(tmp, s->comp_capacity,
                                  s->original, s->orig_size,
                                  ZSTD_COMPRESS_LEVEL);
    if (!ZSTD_isError(csize))
        ZSTD_decompress(decomp, s->orig_size, tmp, csize);
    free(tmp); free(decomp);
    return 0;
}

static int crypto_zstd_measure(void *state, measurement_t *result) {
    crypto_zstd_state_t *s = (crypto_zstd_state_t *)state;
    struct timespec t0, t1;
    volatile size_t sink = 0;
    size_t csize;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    csize = ZSTD_compress(s->compressed, s->comp_capacity,
                           s->original, s->orig_size,
                           ZSTD_COMPRESS_LEVEL);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    if (ZSTD_isError(csize)) {
        /* Report zero throughput on compression failure so the harness
         * can distinguish a failed run from a valid measurement. */
        memset(result, 0, sizeof(*result));
        result->primary_metric = 0.0;
        result->wall_seconds = 0.0;
        return -1;
    }

    sink = csize;
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)s->orig_size / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int crypto_zstd_cleanup(void *state) {
    crypto_zstd_state_t *s = (crypto_zstd_state_t *)state;
    free(s->original); free(s->compressed); free(s);
    return 0;
}

benchmark_t bench_crypto_zstd = {
    .name = "crypto-zstd",
    .category = "C3",
    .description = "zstd compress level 3 throughput (in-cache, 64MB dataset)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = crypto_zstd_init,
    .warmup = crypto_zstd_warmup,
    .measure = crypto_zstd_measure,
    .cleanup = crypto_zstd_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_zstd);
