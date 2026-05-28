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

    /* Generate RSA-2048 key */
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    EVP_PKEY_keygen_init(pctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
    EVP_PKEY_keygen(pctx, &s->pkey);
    EVP_PKEY_CTX_free(pctx);

    if (!s->pkey) { free(s); return -1; }

    s->msg_len = 32;
    s->message = malloc(s->msg_len);
    s->sig_len = EVP_PKEY_size(s->pkey);
    s->sig = malloc(s->sig_len);
    if (!s->message || !s->sig) {
        EVP_PKEY_free(s->pkey);
        free(s->message); free(s->sig); free(s);
        return -1;
    }
    srand(time(NULL));
    for (size_t i = 0; i < s->msg_len; i++) s->message[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int crypto_rsa_warmup(void *state) {
    crypto_rsa_state_t *s = (crypto_rsa_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t slen = s->sig_len;
    EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
    EVP_DigestSign(ctx, s->sig, &slen, s->message, s->msg_len);
    EVP_MD_CTX_free(ctx);
    return 0;
}

static int crypto_rsa_measure(void *state, measurement_t *result) {
    crypto_rsa_state_t *s = (crypto_rsa_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_SIGNS; i++) {
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();
        size_t slen = s->sig_len;
        EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
        EVP_DigestSign(ctx, s->sig, &slen, s->message, s->msg_len);
        EVP_MD_CTX_free(ctx);
        sink += s->sig[0];
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_SIGNS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
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
    .description = "RSA-2048 SHA-256 signature throughput",
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
