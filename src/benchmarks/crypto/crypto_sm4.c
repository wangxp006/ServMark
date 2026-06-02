#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define CHUNK_SIZE (64 * 1024)
#define NUM_CHUNKS 200

/*
 * SM4 CTR encrypt (GM/T 0002). China's mandated block cipher.
 * 128-bit block/128-bit key. Structurally comparable to AES-128-CTR.
 */

#define KEY_SZ 16
#define IV_SZ  16

typedef struct {
    uint8_t *plaintext, *ciphertext, *key, *iv;
} sm4_state_t;

static int crypto_sm4_init(void **state) {
    sm4_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->plaintext=malloc(CHUNK_SIZE); s->ciphertext=malloc(CHUNK_SIZE);
    s->key=malloc(KEY_SZ); s->iv=malloc(IV_SZ);
    if (!s->plaintext||!s->ciphertext||!s->key||!s->iv) {
        free(s->plaintext); free(s->ciphertext);
        free(s->key); free(s->iv); free(s); return -1;
    }
    for(int i=0;i<CHUNK_SIZE;i++) s->plaintext[i]=rand()&0xFF;
    for(int i=0;i<KEY_SZ;i++) s->key[i]=rand()&0xFF;
    for(int i=0;i<IV_SZ;i++) s->iv[i]=rand()&0xFF;
    *state=s; return 0;
}

static int crypto_sm4_warmup(void *state) {
    sm4_state_t *s=(sm4_state_t*)state;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    if(!ctx) return -1;
    int len,ret=0;
    ret=EVP_EncryptInit_ex(ctx,EVP_sm4_ctr(),NULL,s->key,s->iv);
    if(ret==1) EVP_EncryptUpdate(ctx,s->ciphertext,&len,s->plaintext,CHUNK_SIZE/4);
    EVP_CIPHER_CTX_free(ctx);
    return (ret==1)?0:-1;
}

static int crypto_sm4_measure(void *state, measurement_t *result) {
    sm4_state_t *s=(sm4_state_t*)state;
    struct timespec t0,t1;
    volatile int sink=0; int64_t total=0; int ret;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    if(!ctx){memset(result,0,sizeof(*result));return -1;}
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int n=0;n<NUM_CHUNKS;n++){
        int len;
        ret=EVP_EncryptInit_ex(ctx,EVP_sm4_ctr(),NULL,s->key,s->iv);
        if(ret!=1) goto err;
        ret=EVP_EncryptUpdate(ctx,s->ciphertext,&len,s->plaintext,CHUNK_SIZE);
        if(ret!=1) goto err;
        sink+=s->ciphertext[0]; total+=CHUNK_SIZE;
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    EVP_CIPHER_CTX_free(ctx);
    __asm__ __volatile__("":"+r"(sink));
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)total/el;
    result->wall_seconds=el; return 0;
err:
    clock_gettime(CLOCK_MONOTONIC,&t1);
    EVP_CIPHER_CTX_free(ctx);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_sm4_cleanup(void *state) {
    sm4_state_t *s=(sm4_state_t*)state;
    free(s->plaintext); free(s->ciphertext);
    free(s->key); free(s->iv); free(s); return 0;
}

benchmark_t bench_crypto_sm4 = {
    .name="crypto-sm4", .category="C3",
    .description="SM4 CTR encrypt (GM/T 0002, China cipher standard)",
    .tier=2, .primary_metric_name="bytes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_sm4_init, .warmup=crypto_sm4_warmup,
    .measure=crypto_sm4_measure, .cleanup=crypto_sm4_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_sm4);
