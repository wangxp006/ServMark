#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#define NUM_DERIVES 5000

/*
 * X25519 ECDH key exchange. TLS 1.3 handshake bottleneck (~90% connections).
 * Each iteration generates ephemeral peer key + performs ECDH derive.
 */

typedef struct {
    EVP_PKEY *fixed_key;
    uint8_t secret[32];
    size_t secret_len;
} x25519_state_t;

static int crypto_x25519_ecdh_init(void **state) {
    x25519_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!pctx) { free(s); return -1; }
    int ret = EVP_PKEY_keygen_init(pctx);
    if (ret <= 0) { EVP_PKEY_CTX_free(pctx); free(s); return -1; }
    ret = EVP_PKEY_keygen(pctx, &s->fixed_key);
    EVP_PKEY_CTX_free(pctx);
    if (ret <= 0 || !s->fixed_key) {
        if (s->fixed_key) EVP_PKEY_free(s->fixed_key);
        free(s); return -1;
    }
    *state = s; return 0;
}

static int crypto_x25519_ecdh_warmup(void *state) {
    x25519_state_t *s = (x25519_state_t *)state;
    EVP_PKEY *peer = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    EVP_PKEY_keygen_init(pctx); EVP_PKEY_keygen(pctx, &peer);
    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(s->fixed_key, NULL);
    EVP_PKEY_derive_init(dctx); EVP_PKEY_derive_set_peer(dctx, peer);
    s->secret_len = sizeof(s->secret);
    EVP_PKEY_derive(dctx, s->secret, &s->secret_len);
    EVP_PKEY_CTX_free(dctx);
    EVP_PKEY_free(peer);
    return 0;
}

static int crypto_x25519_ecdh_measure(void *state, measurement_t *result) {
    x25519_state_t *s = (x25519_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0; int ret;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < NUM_DERIVES; i++) {
        EVP_PKEY *peer = NULL;
        EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
        ret = EVP_PKEY_keygen_init(pctx);
        if (ret <= 0) { EVP_PKEY_CTX_free(pctx); goto err; }
        ret = EVP_PKEY_keygen(pctx, &peer);
        EVP_PKEY_CTX_free(pctx);
        if (ret <= 0) goto err;
        EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(s->fixed_key, NULL);
        ret = EVP_PKEY_derive_init(dctx);
        if (ret <= 0) { EVP_PKEY_CTX_free(dctx); EVP_PKEY_free(peer); goto err; }
        ret = EVP_PKEY_derive_set_peer(dctx, peer);
        if (ret <= 0) { EVP_PKEY_CTX_free(dctx); EVP_PKEY_free(peer); goto err; }
        s->secret_len = sizeof(s->secret);
        ret = EVP_PKEY_derive(dctx, s->secret, &s->secret_len);
        EVP_PKEY_CTX_free(dctx); EVP_PKEY_free(peer);
        if (ret <= 0) goto err;
        sink += s->secret[0];
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric = (double)NUM_DERIVES / el;
    result->wall_seconds = el;
    return 0;
err:
    clock_gettime(CLOCK_MONOTONIC, &t1);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_x25519_ecdh_cleanup(void *state) {
    x25519_state_t *s = (x25519_state_t *)state;
    EVP_PKEY_free(s->fixed_key); free(s); return 0;
}

benchmark_t bench_crypto_x25519_ecdh = {
    .name="crypto-x25519-ecdh", .category="C3",
    .description="X25519 ECDH key exchange (TLS 1.3 handshake bottleneck, ~90% connections)",
    .tier=1, .primary_metric_name="exchanges/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_x25519_ecdh_init, .warmup=crypto_x25519_ecdh_warmup,
    .measure=crypto_x25519_ecdh_measure, .cleanup=crypto_x25519_ecdh_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_x25519_ecdh);
