#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define DATA_MB 256
#define DATA_SIZE (DATA_MB * 1024 * 1024)

typedef struct {
    uint8_t *data;
    uint8_t digest[32];
} crypto_hash_state_t;

static int crypto_hash_init(void **state) {
    crypto_hash_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->data = malloc(DATA_SIZE);
    if (!s->data) { free(s); return -1; }
    srand(time(NULL));
    for (int i = 0; i < DATA_SIZE; i++)
        s->data[i] = rand() & 0xFF;
    *state = s;
    return 0;
}

static int crypto_hash_warmup(void *state) {
    crypto_hash_state_t *s = (crypto_hash_state_t *)state;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, s->data, DATA_SIZE / 4);
    EVP_DigestFinal_ex(ctx, s->digest, NULL);
    EVP_MD_CTX_free(ctx);
    return 0;
}

static int crypto_hash_measure(void *state, measurement_t *result) {
    crypto_hash_state_t *s = (crypto_hash_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    unsigned int dlen;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, s->data, DATA_SIZE);
    EVP_DigestFinal_ex(ctx, s->digest, &dlen);
    EVP_MD_CTX_free(ctx);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    sink = s->digest[0] + s->digest[31];
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)DATA_SIZE / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int crypto_hash_cleanup(void *state) {
    crypto_hash_state_t *s = (crypto_hash_state_t *)state;
    free(s->data); free(s);
    return 0;
}

benchmark_t bench_crypto_hash = {
    .name = "crypto-hash",
    .category = "C3",
    .description = "SHA-256 hash throughput",
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
