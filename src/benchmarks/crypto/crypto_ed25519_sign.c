#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#define NUM_SIGNS 5000
#define MSG_SIZE 32

/*
 * Ed25519 signing throughput.
 * SSH default since OpenSSH 6.5, mandatory TLS 1.3, WireGuard identity.
 * Every production server uses Ed25519 constantly.
 */

typedef struct {
    EVP_PKEY *pkey;
    uint8_t message[MSG_SIZE];
    uint8_t *sig;
    size_t sig_len;
} ed25519_state_t;

static int crypto_ed25519_sign_init(void **state) {
    ed25519_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (!pctx) { free(s); return -1; }
    int ret = EVP_PKEY_keygen_init(pctx);
    if (ret <= 0) { EVP_PKEY_CTX_free(pctx); free(s); return -1; }
    ret = EVP_PKEY_keygen(pctx, &s->pkey);
    EVP_PKEY_CTX_free(pctx);
    if (ret <= 0 || !s->pkey) {
        if (s->pkey) EVP_PKEY_free(s->pkey);
        free(s); return -1;
    }
    s->sig_len = EVP_PKEY_size(s->pkey);
    s->sig = malloc(s->sig_len);
    if (!s->sig) { EVP_PKEY_free(s->pkey); free(s); return -1; }
    for (size_t i = 0; i < MSG_SIZE; i++) s->message[i] = rand() & 0xFF;
    *state = s; return 0;
}

static int crypto_ed25519_sign_warmup(void *state) {
    ed25519_state_t *s = (ed25519_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    size_t slen = s->sig_len;
    int ret = EVP_DigestSignInit(ctx, NULL, NULL, NULL, s->pkey);
    if (ret == 1) EVP_DigestSign(ctx, s->sig, &slen, s->message, MSG_SIZE);
    EVP_MD_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_ed25519_sign_measure(void *state, measurement_t *result) {
    ed25519_state_t *s = (ed25519_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    int ret;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) { memset(result, 0, sizeof(*result)); return -1; }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < NUM_SIGNS; i++) {
        size_t slen = s->sig_len;
        ret = EVP_DigestSignInit(ctx, NULL, NULL, NULL, s->pkey);
        if (ret != 1) goto err;
        ret = EVP_DigestSign(ctx, s->sig, &slen, s->message, MSG_SIZE);
        if (ret != 1) goto err;
        sink += s->sig[0];
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);
    __asm__ __volatile__("" : "+r"(sink));
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric = (double)NUM_SIGNS / el;
    result->wall_seconds = el;
    return 0;
err:
    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_MD_CTX_free(ctx);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_ed25519_sign_cleanup(void *state) {
    ed25519_state_t *s = (ed25519_state_t *)state;
    EVP_PKEY_free(s->pkey); free(s->sig); free(s); return 0;
}

benchmark_t bench_crypto_ed25519_sign = {
    .name="crypto-ed25519-sign", .category="C3",
    .description="Ed25519 signature throughput (SSH/WireGuard/TLS 1.3 standard)",
    .tier=1, .primary_metric_name="signatures/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_ed25519_sign_init, .warmup=crypto_ed25519_sign_warmup,
    .measure=crypto_ed25519_sign_measure, .cleanup=crypto_ed25519_sign_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_ed25519_sign);
