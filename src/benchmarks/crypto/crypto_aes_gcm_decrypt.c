#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>

#define CHUNK_SIZE (64*1024)
#define NUM_CHUNKS 200
#define KEY_SZ 32
#define IV_SZ  12
#define TAG_SZ 16

/* AES-256-GCM decrypt + tag verify. GCM decrypt costs more than encrypt
 * due to GHASH recompute + constant-time tag comparison. */

typedef struct {
    uint8_t *ciphertext, *plaintext, *key, *iv, *tag;
} aes_dec_state_t;

static int crypto_aes_gcm_decrypt_init(void **state) {
    aes_dec_state_t *s = calloc(1, sizeof(*s));
    if(!s) return -1;
    s->ciphertext=malloc(CHUNK_SIZE+TAG_SZ); s->plaintext=malloc(CHUNK_SIZE);
    s->key=malloc(KEY_SZ); s->iv=malloc(IV_SZ); s->tag=malloc(TAG_SZ);
    if(!s->ciphertext||!s->plaintext||!s->key||!s->iv||!s->tag) {
        free(s->ciphertext); free(s->plaintext); free(s->key);
        free(s->iv); free(s->tag); free(s); return -1;
    }
    for(int i=0;i<CHUNK_SIZE;i++) s->plaintext[i]=rand()&0xFF;
    for(int i=0;i<KEY_SZ;i++) s->key[i]=rand()&0xFF;
    for(int i=0;i<IV_SZ;i++) s->iv[i]=rand()&0xFF;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    int len,ct_len;
    EVP_EncryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,NULL,NULL);
    EVP_EncryptInit_ex(ctx,NULL,NULL,s->key,s->iv);
    EVP_EncryptUpdate(ctx,s->ciphertext,&len,s->plaintext,CHUNK_SIZE);
    EVP_EncryptFinal_ex(ctx,s->ciphertext+len,&ct_len);
    EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_GET_TAG,TAG_SZ,s->tag);
    EVP_CIPHER_CTX_free(ctx);
    *state=s; return 0;
}

static int crypto_aes_gcm_decrypt_warmup(void *state) {
    aes_dec_state_t *s=(aes_dec_state_t*)state;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    if(!ctx) return -1;
    int len,ret=0;
    ret=EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,NULL,NULL);
    if(ret!=1) goto e;
    ret=EVP_DecryptInit_ex(ctx,NULL,NULL,s->key,s->iv);
    if(ret!=1) goto e;
    ret=EVP_DecryptUpdate(ctx,s->plaintext,&len,s->ciphertext,CHUNK_SIZE/4);
    if(ret!=1) goto e;
    EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,TAG_SZ,s->tag);
    ret=EVP_DecryptFinal_ex(ctx,s->plaintext+len,&len);
e:  EVP_CIPHER_CTX_free(ctx); return (ret==1)?0:-1;
}

static int crypto_aes_gcm_decrypt_measure(void *state, measurement_t *result) {
    aes_dec_state_t *s=(aes_dec_state_t*)state;
    struct timespec t0,t1;
    volatile int sink=0; int64_t total=0; int ret;
    EVP_CIPHER_CTX *ctx=EVP_CIPHER_CTX_new();
    if(!ctx){memset(result,0,sizeof(*result));return -1;}
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int n=0;n<NUM_CHUNKS;n++){
        int len,pt_len;
        ret=EVP_DecryptInit_ex(ctx,EVP_aes_256_gcm(),NULL,NULL,NULL);
        if(ret!=1) goto err;
        ret=EVP_DecryptInit_ex(ctx,NULL,NULL,s->key,s->iv);
        if(ret!=1) goto err;
        ret=EVP_DecryptUpdate(ctx,s->plaintext,&len,s->ciphertext,CHUNK_SIZE+TAG_SZ);
        if(ret!=1) goto err;
        ret=EVP_CIPHER_CTX_ctrl(ctx,EVP_CTRL_GCM_SET_TAG,TAG_SZ,s->tag);
        if(ret!=1) goto err;
        ret=EVP_DecryptFinal_ex(ctx,s->plaintext+len,&pt_len);
        if(ret!=1) goto err;
        sink+=s->plaintext[0]; total+=CHUNK_SIZE;
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    EVP_CIPHER_CTX_free(ctx);
    __asm__ __volatile__("":"+r"(sink));
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)total/el; result->wall_seconds=el; return 0;
err:
    clock_gettime(CLOCK_MONOTONIC,&t1);
    EVP_CIPHER_CTX_free(ctx);
    memset(result,0,sizeof(*result)); return -1;
}

static int crypto_aes_gcm_decrypt_cleanup(void *state) {
    aes_dec_state_t *s=(aes_dec_state_t*)state;
    free(s->ciphertext); free(s->plaintext); free(s->key);
    free(s->iv); free(s->tag); free(s); return 0;
}

benchmark_t bench_crypto_aes_gcm_decrypt = {
    .name="crypto-aes-gcm-decrypt", .category="C3",
    .description="AES-256-GCM decrypt + tag verify (cf. encrypt. AES-NI+CLMUL x86, ARMv8 CE ARM)",
    .tier=2, .primary_metric_name="bytes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=crypto_aes_gcm_decrypt_init, .warmup=crypto_aes_gcm_decrypt_warmup,
    .measure=crypto_aes_gcm_decrypt_measure, .cleanup=crypto_aes_gcm_decrypt_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_crypto_aes_gcm_decrypt);
