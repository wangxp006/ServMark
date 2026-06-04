#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define DATA_KB 1024
#define DATA_SIZE (DATA_KB * 1024)
#define HASH_ITERS 5000

/*
 * SHA-256 1MB L2-resident hash throughput.
 * Unlike crypto_hash.c (256MB, DRAM-bound with SHA-NI), this 1MB buffer
 * stays in L2 cache. Measures pure SHA-256 engine throughput without
 * memory bandwidth contamination. Use alongside crypto_hash.c to
 * distinguish crypto engine speed from memory subsystem speed.
 */

typedef struct {
    uint8_t *data;
    uint8_t digest[32];
} hash_l2_state_t;

static int crypto_hash_l2_init(void **state) {
    hash_l2_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->data = malloc(DATA_SIZE);
    if (!s->data) { free(s); return -1; }
    for (int i = 0; i < DATA_SIZE; i++) s->data[i] = rand() & 0xFF;
    *state = s; return 0;
}

static int crypto_hash_l2_warmup(void *state) {
    hash_l2_state_t *s = (hash_l2_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    int ret = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    if (ret != 1) { EVP_MD_CTX_free(ctx); return -1; }
    ret = EVP_DigestUpdate(ctx, s->data, DATA_SIZE);
    if (ret != 1) { EVP_MD_CTX_free(ctx); return -1; }
    ret = EVP_DigestFinal_ex(ctx, s->digest, NULL);
    EVP_MD_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_hash_l2_measure(void *state, measurement_t *result) {
    hash_l2_state_t *s = (hash_l2_state_t *)state;
    struct timespec t0, t1;
    volatile unsigned int dlen = 0;
    volatile int sink = 0; int ret;
    size_t total = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < HASH_ITERS; i++) {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        if (!ctx) goto err;
        ret = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        if (ret != 1) { EVP_MD_CTX_free(ctx); goto err; }
        ret = EVP_DigestUpdate(ctx, s->data, DATA_SIZE);
        if (ret != 1) { EVP_MD_CTX_free(ctx); goto err; }
        ret = EVP_DigestFinal_ex(ctx, s->digest, &dlen);
        EVP_MD_CTX_free(ctx);
        if (ret != 1) goto err;
        sink += s->digest[0];
        total += DATA_SIZE;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("":"+r"(sink):"r"(dlen));

    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)total/el;
    result->wall_seconds=el; return 0;

err:
    clock_gettime(CLOCK_MONOTONIC,&t1);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_hash_l2_cleanup(void *state) {
    hash_l2_state_t *s = (hash_l2_state_t *)state;
    free(s->data); free(s); return 0;
}

benchmark_t bench_crypto_hash_l2 = {
    .name="crypto-hash-l2", .category="C3",
    .description="SHA-256 1MB L2-resident hash (pure crypto engine, cf. 256MB crypto-hash DRAM-bound)",
    .tier=2, .primary_metric_name="bytes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_hash_l2_init, .warmup=crypto_hash_l2_warmup,
    .measure=crypto_hash_l2_measure, .cleanup=crypto_hash_l2_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_hash_l2);
