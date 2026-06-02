#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#define NUM_SIGNS 200

typedef struct {
    EVP_PKEY *pkey;
    uint8_t *message;
    size_t msg_len;
    uint8_t *sig;
    size_t sig_len;
} crypto_rsa_state_t;

static int crypto_rsa_init(void **state) {
    crypto_rsa_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;

    /* Generate RSA-2048 key. Check every step — keygen failure is
     * recoverable (e.g., entropy exhaustion) and must not produce UB. */
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx) { free(s); return -1; }

    int ret = EVP_PKEY_keygen_init(pctx);
    if (ret <= 0) { EVP_PKEY_CTX_free(pctx); free(s); return -1; }

    ret = EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
    if (ret <= 0) { EVP_PKEY_CTX_free(pctx); free(s); return -1; }

    ret = EVP_PKEY_keygen(pctx, &s->pkey);
    EVP_PKEY_CTX_free(pctx);
    if (ret <= 0 || !s->pkey) {
        if (s->pkey) EVP_PKEY_free(s->pkey);
        free(s);
        return -1;
    }

    s->msg_len = 32;
    s->message = malloc(s->msg_len);
    s->sig_len = EVP_PKEY_size(s->pkey);
    s->sig = malloc(s->sig_len);
    if (!s->message || !s->sig) {
        EVP_PKEY_free(s->pkey);
        free(s->message); free(s->sig); free(s);
        return -1;
    }
    /* rand() is acceptable for benchmark message generation.
     * Message content does not affect RSA signing performance. */
    for (size_t i = 0; i < s->msg_len; i++) s->message[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int crypto_rsa_warmup(void *state) {
    crypto_rsa_state_t *s = (crypto_rsa_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    size_t slen = s->sig_len;
    int ret = 0;
    ret = EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
    if (ret != 1) goto warmup_err;
    ret = EVP_DigestSign(ctx, s->sig, &slen, s->message, s->msg_len);
warmup_err:
    EVP_MD_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_rsa_measure(void *state, measurement_t *result) {
    crypto_rsa_state_t *s = (crypto_rsa_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    int ret;

    /*
     * Create and initialize the signing context ONCE outside the timed loop
     * so we measure pure RSA-2048 signing throughput, not malloc + sign +
     * free overhead. Reinitializing the digest signing context with the
     * same key is the correct EVP pattern for repeated signatures.
     */
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_SIGNS; i++) {
        size_t slen = s->sig_len;

        ret = EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
        if (ret != 1) goto measure_err;

        ret = EVP_DigestSign(ctx, s->sig, &slen, s->message, s->msg_len);
        if (ret != 1) goto measure_err;

        sink += s->sig[0];
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_SIGNS / elapsed;
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

static int crypto_rsa_cleanup(void *state) {
    crypto_rsa_state_t *s = (crypto_rsa_state_t *)state;
    EVP_PKEY_free(s->pkey);
    free(s->message); free(s->sig); free(s);
    return 0;
}

benchmark_t bench_crypto_rsa = {
    .name = "crypto-rsa",
    .category = "C3",
    .description = "RSA-2048 SHA-256 signature throughput (PKCS#1 v1.5 padding)",
    .tier = 1,
    .primary_metric_name = "signatures/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = crypto_rsa_init,
    .warmup = crypto_rsa_warmup,
    .measure = crypto_rsa_measure,
    .cleanup = crypto_rsa_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_rsa);
