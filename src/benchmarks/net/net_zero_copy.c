#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

#define FILE_SIZE (64*1024*1024)
#define SENDFILE_PASSES 20
#define PORT_ZC 19994

/* sendfile() zero-copy throughput: file -> TCP socket (splice kernel path).
 * Sink thread drains the accepted connection. File opened ONCE before timing;
 * sendfile() with explicit offset reuses the same fd across passes. */

typedef struct {
    char *path; int listen_fd;
    _Atomic int ready,done; _Atomic int64_t drained; pthread_t th;
} zc_state_t;

static void *zc_sink(void *arg) {
    zc_state_t *s=(zc_state_t*)arg;
    struct sockaddr_in addr; socklen_t alen=sizeof(addr);
    atomic_store_explicit(&s->ready,1,memory_order_release);
    int cl=accept(s->listen_fd,(struct sockaddr*)&addr,&alen);
    if(cl<0) return NULL;
    char buf[65536]; ssize_t n;
    while((n=recv(cl,buf,sizeof(buf),0))>0)
        atomic_fetch_add_explicit(&s->drained,n,memory_order_relaxed);
    close(cl); return NULL;
}

static int zc_init(void **state) {
    zc_state_t *s=calloc(1,sizeof(*s));
    if(!s) return -1;
    s->path=strdup("/tmp/ssb_sendfile_src.bin");
    if(!s->path){free(s);return -1;}
    int fd=open(s->path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0){free(s->path);free(s);return -1;}
    char *buf=malloc(65536); memset(buf,'Z',65536);
    size_t r=FILE_SIZE;
    while(r>0){size_t n=r>65536?65536:r; write(fd,buf,n); r-=n;}
    free(buf); close(fd);
    s->listen_fd=socket(AF_INET,SOCK_STREAM,0);
    setsockopt(s->listen_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT_ZC);
    bind(s->listen_fd,(struct sockaddr*)&a,sizeof(a)); listen(s->listen_fd,1);
    *state=s; return 0;
}

static int zc_warmup(void *state) {
    zc_state_t *s=(zc_state_t*)state;
    atomic_store(&s->ready,0);atomic_store(&s->done,0);atomic_store(&s->drained,0);
    pthread_create(&s->th,NULL,zc_sink,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int cl=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT_ZC);
    if(connect(cl,(struct sockaddr*)&d,sizeof(d))==0){
        int fd=open(s->path,O_RDONLY); struct stat st; fstat(fd,&st);
        sendfile(cl,fd,NULL,st.st_size/10); close(fd); close(cl);
    }
    shutdown(s->listen_fd,SHUT_RD);
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(s->th,NULL);
    s->listen_fd=socket(AF_INET,SOCK_STREAM,0);
    setsockopt(s->listen_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
    bind(s->listen_fd,(struct sockaddr*)&d,sizeof(d)); listen(s->listen_fd,1);
    return 0;
}

static int zc_measure(void *state, measurement_t *result) {
    zc_state_t *s=(zc_state_t*)state;
    struct timespec t0,t1; int64_t total=0;
    atomic_store(&s->ready,0);atomic_store(&s->done,0);atomic_store(&s->drained,0);
    pthread_create(&s->th,NULL,zc_sink,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int cl=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT_ZC);
    if(connect(cl,(struct sockaddr*)&d,sizeof(d))<0) return -1;
    int fd=open(s->path,O_RDONLY);
    if(fd<0){close(cl);return -1;}
    struct stat st; fstat(fd,&st);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int p=0;p<SENDFILE_PASSES;p++){
        off_t off=0;
        while((size_t)off<(size_t)st.st_size){
            ssize_t n=sendfile(cl,fd,&off,st.st_size-off);
            if(n<=0) break; total+=n;
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    close(fd); shutdown(cl,SHUT_WR); close(cl);
    shutdown(s->listen_fd,SHUT_RD);
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(s->th,NULL);
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)total/el; result->wall_seconds=el;
    return 0;
}

static int zc_cleanup(void *state) {
    zc_state_t *s=(zc_state_t*)state;
    unlink(s->path); close(s->listen_fd); free(s->path); free(s); return 0;
}

benchmark_t bench_net_zero_copy={
    .name="net-zero-copy",.category="C13",
    .description="sendfile() file->TCP socket (Linux splice path, single open, real socket sink)",
    .tier=1,.primary_metric_name="bytes/sec",.higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS,.max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC,.max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=zc_init,.warmup=zc_warmup,.measure=zc_measure,.cleanup=zc_cleanup,.num_threads=2,
};
SSB_BENCHMARK_REGISTER(bench_net_zero_copy);
