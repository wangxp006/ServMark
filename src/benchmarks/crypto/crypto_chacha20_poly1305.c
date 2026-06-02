#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define CHUNK_SIZE (64 * 1024)
#define NUM_CHUNKS 200

/*
 * ChaCha20-Poly1305 AEAD encrypt. Preferred ARM/WireGuard/QUIC cipher.
 * Same structure as crypto_aes.c for direct AEAD throughput comparison.
 *
 * Nonce reuse notice: same (key,nonce) reused for benchmark consistency.
 * DO NOT reuse nonces in production ChaCha20-Poly1305.
 */

#define KEY_SZ 32
#define NONCE_SZ 12
#define TAG_SZ 16

typedef struct {
    uint8_t *plaintext, *ciphertext, *key, *nonce, *tag;
} chacha_state_t;

static int crypto_chacha20_poly1305_init(void **state) {
    chacha_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->plaintext = malloc(CHUNK_SIZE); s->ciphertext = malloc(CHUNK_SIZE + TAG_SZ);
    s->key = malloc(KEY_SZ); s->nonce = malloc(NONCE_SZ); s->tag = malloc(TAG_SZ);
    if (!s->plaintext||!s->ciphertext||!s->key||!s->nonce||!s->tag) {
        free(s->plaintext); free(s->ciphertext); free(s->key);
        free(s->nonce); free(s->tag); free(s); return -1;
    }
    for (int i=0;i<CHUNK_SIZE;i++) s->plaintext[i]=rand()&0xFF;
    for (int i=0;i<KEY_SZ;i++) s->key[i]=rand()&0xFF;
    for (int i=0;i<NONCE_SZ;i++) s->nonce[i]=rand()&0xFF;
    *state=s; return 0;
}

static int crypto_chacha20_poly1305_warmup(void *state) {
    chacha_state_t *s=(chacha_state_t*)state;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    if(!ctx) return -1;
    int len,ret=0;
    ret=EVP_EncryptInit_ex(ctx,EVP_chacha20_poly1305(),NULL,NULL,NULL);
    if(ret!=1) goto e;
    ret=EVP_EncryptInit_ex(ctx,NULL,NULL,s->key,s->nonce);
    if(ret!=1) goto e;
    ret=EVP_EncryptUpdate(ctx,s->ciphertext,&len,s->plaintext,CHUNK_SIZE/4);
    if(ret!=1) goto e;
    ret=EVP_EncryptFinal_ex(ctx,s->ciphertext+len,&len);
e:  EVP_CIPHER_CTX_free(ctx);
    return (ret==1)?0:-1;
}

static int crypto_chacha20_poly1305_measure(void *state, measurement_t *result) {
    chacha_state_t *s=(chacha_state_t*)state;
    struct timespec t0,t1;
    volatile int sink=0; int64_t total=0; int ret;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    if(!ctx){memset(result,0,sizeof(*result));return -1;}
    clock_gettime(CLOCK_MONOTONIC,&t0);
    ret=EVP_EncryptInit_ex(ctx,EVP_chacha20_poly1305(),NULL,NULL,NULL);
    if(ret!=1) goto err;
    for(int n=0;n<NUM_CHUNKS;n++){
        int len,ct_len;
        ret=EVP_EncryptInit_ex(ctx,NULL,NULL,s->key,s->nonce);
        if(ret!=1) goto err;
        ret=EVP_EncryptUpdate(ctx,s->ciphertext,&len,s->plaintext,CHUNK_SIZE);
        if(ret!=1) goto err;
        ret=EVP_EncryptFinal_ex(ctx,s->ciphertext+len,&ct_len);
        if(ret!=1) goto err;
        ret=EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_AEAD_GET_TAG,TAG_SZ,s->tag);
        if(ret!=1) goto err;
        sink+=s->tag[0]; total+=CHUNK_SIZE;
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

static int crypto_chacha20_poly1305_cleanup(void *state) {
    chacha_state_t *s=(chacha_state_t*)state;
    free(s->plaintext); free(s->ciphertext); free(s->key);
    free(s->nonce); free(s->tag); free(s); return 0;
}

benchmark_t bench_crypto_chacha20_poly1305 = {
    .name="crypto-chacha20-poly1305", .category="C3",
    .description="ChaCha20-Poly1305 AEAD encrypt (ARM/WireGuard/QUIC cipher. HW-accel x86/ARM64/S390x)",
    .tier=1, .primary_metric_name="bytes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_chacha20_poly1305_init,
    .warmup=crypto_chacha20_poly1305_warmup,
    .measure=crypto_chacha20_poly1305_measure,
    .cleanup=crypto_chacha20_poly1305_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_chacha20_poly1305);
