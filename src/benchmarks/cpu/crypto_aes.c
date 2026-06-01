#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#define CHUNK_SIZE (64 * 1024)
#define NUM_CHUNKS 200

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
    s->ciphertext = malloc(CHUNK_SIZE + 16);
    s->key = malloc(32);
    s->iv = malloc(12);
    s->tag = malloc(16);
    if (!s->plaintext || !s->ciphertext || !s->key || !s->iv || !s->tag) {
        free(s->plaintext); free(s->ciphertext); free(s->key);
        free(s->iv); free(s->tag); free(s);
        return -1;
    }
    for (int i = 0; i < CHUNK_SIZE; i++) s->plaintext[i] = rand() & 0xFF;
    for (int i = 0; i < 32; i++) s->key[i] = rand() & 0xFF;
    for (int i = 0; i < 12; i++) s->iv[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int crypto_aes_warmup(void *state) {
    crypto_aes_state_t *s = (crypto_aes_state_t *)state;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int len;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, s->key, s->iv);
    EVP_EncryptUpdate(ctx, s->ciphertext, &len, s->plaintext, CHUNK_SIZE / 4);
    EVP_EncryptFinal_ex(ctx, s->ciphertext + len, &len);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

static int crypto_aes_measure(void *state, measurement_t *result) {
    crypto_aes_state_t *s = (crypto_aes_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    int64_t total_bytes = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /* Create CTX once, reuse for all chunks (amortizes key setup cost) */
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    for (int n = 0; n < NUM_CHUNKS; n++) {
        int len, ct_len;
        EVP_EncryptInit_ex(ctx, NULL, NULL, s->key, s->iv);
        EVP_EncryptUpdate(ctx, s->ciphertext, &len, s->plaintext, CHUNK_SIZE);
        EVP_EncryptFinal_ex(ctx, s->ciphertext + len, &ct_len);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, s->tag);
        sink += s->tag[0];
        total_bytes += CHUNK_SIZE;
    }
    EVP_CIPHER_CTX_free(ctx);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_bytes / elapsed;
    result->wall_seconds = elapsed;
    return 0;
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
    .description = "AES-256-GCM encrypt throughput",
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
