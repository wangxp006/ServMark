#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#define CHUNK_SIZE (64 * 1024)
#define NUM_CHUNKS 200

/* AES-256-GCM key (32 bytes), IV/nonce (12 bytes), and tag (16 bytes). */
#define AES_KEY_SIZE 32
#define AES_IV_SIZE  12
#define AES_TAG_SIZE 16

typedef struct {
    uint8_t *plaintext;
    uint8_t *ciphertext;
    uint8_t *key;
    uint8_t *iv;
    uint8_t *tag;
} crypto_aes_state_t;

static int crypto_aes_init(void **state) {
    crypto_aes_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->plaintext = malloc(CHUNK_SIZE);
    s->ciphertext = malloc(CHUNK_SIZE + AES_TAG_SIZE);
    s->key = malloc(AES_KEY_SIZE);
    s->iv = malloc(AES_IV_SIZE);
    s->tag = malloc(AES_TAG_SIZE);
    if (!s->plaintext || !s->ciphertext || !s->key || !s->iv || !s->tag) {
        free(s->plaintext); free(s->ciphertext); free(s->key);
        free(s->iv); free(s->tag); free(s);
        return -1;
    }
    /* rand() is acceptable for benchmark payload generation only —
     * never use rand() for cryptographic key material in production. */
    for (int i = 0; i < CHUNK_SIZE; i++) s->plaintext[i] = rand() & 0xFF;
    for (int i = 0; i < AES_KEY_SIZE; i++) s->key[i] = rand() & 0xFF;
    for (int i = 0; i < AES_IV_SIZE; i++) s->iv[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int crypto_aes_warmup(void *state) {
    crypto_aes_state_t *s = (crypto_aes_state_t *)state;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    int len, ret = 0;
    ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    if (ret != 1) goto warmup_err;
    ret = EVP_EncryptInit_ex(ctx, NULL, NULL, s->key, s->iv);
    if (ret != 1) goto warmup_err;
    ret = EVP_EncryptUpdate(ctx, s->ciphertext, &len, s->plaintext, CHUNK_SIZE / 4);
    if (ret != 1) goto warmup_err;
    ret = EVP_EncryptFinal_ex(ctx, s->ciphertext + len, &len);
warmup_err:
    EVP_CIPHER_CTX_free(ctx);
    return (ret == 1) ? 0 : -1;
}

static int crypto_aes_measure(void *state, measurement_t *result) {
    crypto_aes_state_t *s = (crypto_aes_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    int64_t total_bytes = 0;
    int ret;

    /*
     * GCM IV (nonce) reuse notice:
     *
     * The same (key, IV) pair is reused across all NUM_CHUNKS iterations
     * inside the timed section. In production AES-GCM, IV/nonce reuse with
     * the same key is CATASTROPHIC — it breaks both confidentiality and
     * authentication. For this benchmark we intentionally reuse the IV to
     * avoid contaminating the measurement with RAND_bytes() overhead.
     *
     * DO NOT COPY THIS PATTERN INTO PRODUCTION CODE.
     */

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    /*
     * This benchmark measures AES-256-GCM Init + Update + Final + GetTag
     * for each 64KB chunk. The CTX is created once and reinitialized
     * per-chunk to amortize allocation overhead — matching common
     * real-world patterns where a CTX is reused across multiple messages.
     */

    clock_gettime(CLOCK_MONOTONIC, &t0);

    ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    if (ret != 1) goto measure_err;

    for (int n = 0; n < NUM_CHUNKS; n++) {
        int len, ct_len;

        ret = EVP_EncryptInit_ex(ctx, NULL, NULL, s->key, s->iv);
        if (ret != 1) goto measure_err;

        ret = EVP_EncryptUpdate(ctx, s->ciphertext, &len, s->plaintext, CHUNK_SIZE);
        if (ret != 1) goto measure_err;

        ret = EVP_EncryptFinal_ex(ctx, s->ciphertext + len, &ct_len);
        if (ret != 1) goto measure_err;

        ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_SIZE, s->tag);
        if (ret != 1) goto measure_err;

        sink += s->tag[0];
        total_bytes += CHUNK_SIZE;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_CIPHER_CTX_free(ctx);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_bytes / elapsed;
    result->wall_seconds = elapsed;
    return 0;

measure_err:
    clock_gettime(CLOCK_MONOTONIC, &t1);
    EVP_CIPHER_CTX_free(ctx);
    memset(result, 0, sizeof(*result));
    result->primary_metric = 0.0;
    result->wall_seconds = 0.0;
    return -1;
}

static int crypto_aes_cleanup(void *state) {
    crypto_aes_state_t *s = (crypto_aes_state_t *)state;
    free(s->plaintext); free(s->ciphertext); free(s->key);
    free(s->iv); free(s->tag); free(s);
    return 0;
}

benchmark_t bench_crypto_aes = {
    .name = "crypto-aes",
    .category = "C3",
    .description = "AES-256-GCM encrypt throughput (HW-accel: AES-NI x86, ARMv8 Crypto ARM64, SW-only RISC-V)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = crypto_aes_init,
    .warmup = crypto_aes_warmup,
    .measure = crypto_aes_measure,
    .cleanup = crypto_aes_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_aes);
