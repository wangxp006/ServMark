#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

#define PORT_BASE 19990
#define MSG_SIZE (64*1024)
#define NUM_MSGS 2000
#define SOCK_BUF (4*1024*1024)

typedef struct { int listen_fd; _Atomic int ready,done; _Atomic int64_t total_bytes; } net_tcp_state_t;

static void *tcp_server(void *arg) {
    net_tcp_state_t *s=(net_tcp_state_t*)arg;
    struct sockaddr_in addr; socklen_t alen=sizeof(addr);
    atomic_store_explicit(&s->ready,1,memory_order_release);
    int cl=accept(s->listen_fd,(struct sockaddr*)&addr,&alen);
    if(cl<0) return NULL;
    setsockopt(cl,IPPROTO_TCP,TCP_NODELAY,&(int){1},sizeof(int));
    int rcv=SOCK_BUF; setsockopt(cl,SOL_SOCKET,SO_RCVBUF,&rcv,sizeof(rcv));
    char *buf=malloc(MSG_SIZE); ssize_t n;
    while((n=recv(cl,buf,MSG_SIZE,0))>0) atomic_fetch_add_explicit(&s->total_bytes,n,memory_order_relaxed);
    free(buf); close(cl); return NULL;
}

static int net_tcp_init(void **state) {
    net_tcp_state_t *s=calloc(1,sizeof(*s));
    if(!s) return -1;
    s->listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(s->listen_fd<0){free(s);return -1;}
    setsockopt(s->listen_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT_BASE);
    if(bind(s->listen_fd,(struct sockaddr*)&a,sizeof(a))<0){close(s->listen_fd);free(s);return -1;}
    listen(s->listen_fd,1); *state=s; return 0;
}

static int nt_warmup(void *state) {
    net_tcp_state_t *s=(net_tcp_state_t*)state;
    atomic_store(&s->ready,0);atomic_store(&s->total_bytes,0);atomic_store(&s->done,0);
    pthread_t st; pthread_create(&st,NULL,tcp_server,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT_BASE);
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&(int){1},sizeof(int));
    if(connect(fd,(struct sockaddr*)&a,sizeof(a))==0){
        char buf[MSG_SIZE]; memset(buf,'T',MSG_SIZE);
        for(int i=0;i<20;i++) send(fd,buf,MSG_SIZE,0);
        shutdown(fd,SHUT_WR); close(fd);
    }
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(st,NULL); return 0;
}

static int nt_measure(void *state, measurement_t *result) {
    net_tcp_state_t *s=(net_tcp_state_t*)state;
    struct timespec t0,t1;
    atomic_store(&s->ready,0);atomic_store(&s->total_bytes,0);atomic_store(&s->done,0);
    pthread_t st; pthread_create(&st,NULL,tcp_server,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT_BASE);
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&(int){1},sizeof(int));
    int snd=SOCK_BUF; setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&snd,sizeof(snd));
    if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){
        shutdown(s->listen_fd,SHUT_RD);atomic_store_explicit(&s->done,1,memory_order_release);
        pthread_join(st,NULL); return -1;
    }
    char *buf=malloc(MSG_SIZE); memset(buf,'T',MSG_SIZE);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<NUM_MSGS;i++) send(fd,buf,MSG_SIZE,0);
    shutdown(fd,SHUT_WR); close(fd);
    pthread_join(st,NULL); free(buf);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)atomic_load_explicit(&s->total_bytes,memory_order_relaxed)/el;
    result->wall_seconds=el; return 0;
}

static int nt_cleanup(void *state){net_tcp_state_t *s=(net_tcp_state_t*)state;close(s->listen_fd);free(s);return 0;}

benchmark_t bench_net_tcp={
    .name="net-tcp",.category="C13",
    .description="TCP loopback stream 64KB msg (NODELAY on both, 4MB buffers)",
    .tier=1,.primary_metric_name="bytes/sec",.higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS,.max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC,.max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=net_tcp_init,.warmup=nt_warmup,.measure=nt_measure,.cleanup=nt_cleanup,.num_threads=2,
};
SSB_BENCHMARK_REGISTER(bench_net_tcp);
