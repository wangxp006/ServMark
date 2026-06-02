#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#define NUM_VERIFIES 10000
#define MSG_SIZE 32

/*
 * RSA-2048 SHA-256 verify. Public exponent e=65537 (~17 Montgomery squares).
 * ~100x faster than signing. TLS cert chain verification workload.
 */

typedef struct {
    EVP_PKEY *pkey; uint8_t message[MSG_SIZE]; uint8_t *sig; size_t sig_len;
} rsa_vfy_state_t;

static int crypto_rsa_verify_init(void **state) {
    rsa_vfy_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx) { free(s); return -1; }
    int ret = EVP_PKEY_keygen_init(pctx);
    if (ret <= 0) { EVP_PKEY_CTX_free(pctx); free(s); return -1; }
    ret = EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
    if (ret <= 0) { EVP_PKEY_CTX_free(pctx); free(s); return -1; }
    ret = EVP_PKEY_keygen(pctx, &s->pkey);
    EVP_PKEY_CTX_free(pctx);
    if (ret <= 0 || !s->pkey) {
        if (s->pkey) EVP_PKEY_free(s->pkey); free(s); return -1;
    }
    s->sig_len = EVP_PKEY_size(s->pkey);
    s->sig = malloc(s->sig_len);
    if (!s->sig) { EVP_PKEY_free(s->pkey); free(s); return -1; }
    for (size_t i = 0; i < MSG_SIZE; i++) s->message[i] = rand() & 0xFF;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    size_t slen = s->sig_len;
    EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
    EVP_DigestSign(ctx, s->sig, &slen, s->message, MSG_SIZE);
    EVP_MD_CTX_free(ctx);
    *state = s; return 0;
}

static int crypto_rsa_verify_warmup(void *state) {
    rsa_vfy_state_t *s = (rsa_vfy_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    int ret = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
    if (ret == 1) EVP_DigestVerify(ctx, s->sig, s->sig_len, s->message, MSG_SIZE);
    EVP_MD_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_rsa_verify_measure(void *state, measurement_t *result) {
    rsa_vfy_state_t *s = (rsa_vfy_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0; int ret;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { memset(result,0,sizeof(*result)); return -1; }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < NUM_VERIFIES; i++) {
        ret = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, s->pkey);
        if (ret != 1) goto err;
        ret = EVP_DigestVerify(ctx, s->sig, s->sig_len, s->message, MSG_SIZE);
        if (ret != 1) goto err;
        sink += s->sig[0];
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);
    __asm__ __volatile__("":"+r"(sink));
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)NUM_VERIFIES/el;
    result->wall_seconds=el; return 0;
err:
    clock_gettime(CLOCK_MONOTONIC,&t1);
    EVP_MD_CTX_free(ctx);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_rsa_verify_cleanup(void *state) {
    rsa_vfy_state_t *s = (rsa_vfy_state_t *)state;
    EVP_PKEY_free(s->pkey); free(s->sig); free(s); return 0;
}

benchmark_t bench_crypto_rsa_verify = {
    .name="crypto-rsa-verify", .category="C3",
    .description="RSA-2048 SHA-256 verify (TLS cert chain, ~100x faster than sign)",
    .tier=2, .primary_metric_name="verifications/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_rsa_verify_init, .warmup=crypto_rsa_verify_warmup,
    .measure=crypto_rsa_verify_measure, .cleanup=crypto_rsa_verify_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_rsa_verify);
