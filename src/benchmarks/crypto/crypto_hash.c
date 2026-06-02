#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define DATA_MB 256
#define DATA_SIZE (DATA_MB * 1024 * 1024)

/*
 * SHA-256 hash throughput benchmark.
 *
 * NOTE: At 256MB, the dataset exceeds L3 cache on most server CPUs.
 * On systems with SHA-NI / SHA extensions, hashing at memory bandwidth
 * (~15-25 GB/s) makes this partially DRAM-bound. On software-only
 * implementations (~200-800 MB/s), the hash engine is the bottleneck.
 *
 * For a pure CPU-bound SHA-256 benchmark, reduce DATA_SIZE to fit in L1/L2.
 */

typedef struct {
    uint8_t *data;
    uint8_t digest[32];
} crypto_hash_state_t;

static int crypto_hash_init(void **state) {
    crypto_hash_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->data = malloc(DATA_SIZE);
    if (!s->data) { free(s); return -1; }
    /* rand() is acceptable for benchmark data — content does not affect SHA-256 speed. */
    for (int i = 0; i < DATA_SIZE; i++)
        s->data[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int crypto_hash_warmup(void *state) {
    crypto_hash_state_t *s = (crypto_hash_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    int ret = 0;
    ret = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    if (ret != 1) goto warmup_err;
    ret = EVP_DigestUpdate(ctx, s->data, DATA_SIZE / 4);
    if (ret != 1) goto warmup_err;
    ret = EVP_DigestFinal_ex(ctx, s->digest, NULL);
warmup_err:
    EVP_MD_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_hash_measure(void *state, measurement_t *result) {
    crypto_hash_state_t *s = (crypto_hash_state_t *)state;
    struct timespec t0, t1;
    unsigned int dlen = 0;
    volatile int sink = 0;
    int ret;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    ret = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    if (ret != 1) goto measure_err;

    ret = EVP_DigestUpdate(ctx, s->data, DATA_SIZE);
    if (ret != 1) goto measure_err;

    ret = EVP_DigestFinal_ex(ctx, s->digest, &dlen);
    if (ret != 1) goto measure_err;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);

    /* Force the compiler to materialize digest results — prevents the
     * entire hashing chain from being eliminated as dead code. */
    sink = s->digest[0] + s->digest[31];
    __asm__ __volatile__("" : "+r"(sink) : "r"(dlen));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)DATA_SIZE / elapsed;
    result->wall_seconds = elapsed;
    return 0;

measure_err:
    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);
    memset(result, 0, sizeof(*result));
    result->primary_metric = 0.0;
    result->wall_seconds = 0.0;
    return -1;
}

static int crypto_hash_cleanup(void *state) {
    crypto_hash_state_t *s = (crypto_hash_state_t *)state;
    free(s->data); free(s);
    return 0;
}

benchmark_t bench_crypto_hash = {
    .name = "crypto-hash",
    .category = "C3",
    .description = "SHA-256 hash 256MB (HW: SHA-NI x86, ARMv8 Crypto ARM64, SW RISC-V. May be DRAM-bound)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = crypto_hash_init,
    .warmup = crypto_hash_warmup,
    .measure = crypto_hash_measure,
    .cleanup = crypto_hash_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_hash);
