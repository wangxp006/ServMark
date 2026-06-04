#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define DATA_MB 256
#define DATA_SIZE (DATA_MB * 1024 * 1024)

/*
 * SM3 hash throughput (GM/T 0004). China's mandated integrity hash.
 */

typedef struct {
    uint8_t *data;
    uint8_t digest[32];
} sm3_state_t;

static int crypto_sm3_init(void **state) {
    sm3_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->data = malloc(DATA_SIZE);
    if (!s->data) { free(s); return -1; }
    for (int i = 0; i < DATA_SIZE; i++) s->data[i] = rand() & 0xFF;
    *state = s; return 0;
}

static int crypto_sm3_warmup(void *state) {
    sm3_state_t *s = (sm3_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    int ret = EVP_DigestInit_ex(ctx, EVP_sm3(), NULL);
    if (ret != 1) { EVP_MD_CTX_free(ctx); return -1; }
    ret = EVP_DigestUpdate(ctx, s->data, DATA_SIZE / 4);
    if (ret != 1) { EVP_MD_CTX_free(ctx); return -1; }
    ret = EVP_DigestFinal_ex(ctx, s->digest, NULL);
    EVP_MD_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_sm3_measure(void *state, measurement_t *result) {
    sm3_state_t *s = (sm3_state_t *)state;
    struct timespec t0, t1;
    volatile unsigned int dlen = 0;
    volatile int sink = 0; int ret;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { memset(result,0,sizeof(*result)); return -1; }
    ret = EVP_DigestInit_ex(ctx, EVP_sm3(), NULL);
    if (ret != 1) goto err;
    ret = EVP_DigestUpdate(ctx, s->data, DATA_SIZE);
    if (ret != 1) goto err;
    ret = EVP_DigestFinal_ex(ctx, s->digest, &dlen);
    if (ret != 1) goto err;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);
    sink = s->digest[0] + s->digest[31];
    __asm__ __volatile__("":"+r"(sink):"r"(dlen));
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)DATA_SIZE/el;
    result->wall_seconds=el; return 0;
err:
    clock_gettime(CLOCK_MONOTONIC,&t1);
    EVP_MD_CTX_free(ctx);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_sm3_cleanup(void *state) {
    sm3_state_t *s = (sm3_state_t *)state;
    free(s->data); free(s); return 0;
}

benchmark_t bench_crypto_sm3 = {
    .name="crypto-sm3", .category="C3",
    .description="SM3 hash 256MB (GM/T 0004, China integrity standard)",
    .tier=2, .primary_metric_name="bytes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_sm3_init, .warmup=crypto_sm3_warmup,
    .measure=crypto_sm3_measure, .cleanup=crypto_sm3_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_sm3);
