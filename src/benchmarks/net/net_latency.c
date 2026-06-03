#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>

#define PORT 19993
#define NUM_RRS 50000
#define MSG_SIZE 64

typedef struct { int listen_fd; _Atomic int ready, done; } net_latency_state_t;

static void *echo_server(void *arg) {
    net_latency_state_t *s=(net_latency_state_t*)arg;
    struct sockaddr_in addr; socklen_t alen=sizeof(addr);
    atomic_store_explicit(&s->ready,1,memory_order_release);
    int cl=accept(s->listen_fd,(struct sockaddr*)&addr,&alen);
    if(cl<0) return NULL;
    setsockopt(cl,IPPROTO_TCP,TCP_NODELAY,&(int){1},sizeof(int));
    char buf[MSG_SIZE];
    while(!atomic_load_explicit(&s->done,memory_order_acquire)){
        ssize_t n=recv(cl,buf,MSG_SIZE,0); if(n<=0) break; send(cl,buf,n,0);
    }
    close(cl); return NULL;
}

static int net_latency_init(void **state) {
    net_latency_state_t *s=calloc(1,sizeof(*s));
    if(!s) return -1;
    s->listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(s->listen_fd<0){free(s);return -1;}
    setsockopt(s->listen_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT);
    if(bind(s->listen_fd,(struct sockaddr*)&a,sizeof(a))<0){close(s->listen_fd);free(s);return -1;}
    listen(s->listen_fd,1); *state=s; return 0;
}

static int nl_warmup(void *state) {
    net_latency_state_t *s=(net_latency_state_t*)state;
    atomic_store(&s->ready,0);atomic_store(&s->done,0);
    pthread_t st; pthread_create(&st,NULL,echo_server,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT);
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&(int){1},sizeof(int));
    if(connect(fd,(struct sockaddr*)&d,sizeof(d))==0){
        char b[MSG_SIZE]="ping",r[MSG_SIZE];
        for(int i=0;i<100;i++){send(fd,b,MSG_SIZE,0);recv(fd,r,MSG_SIZE,0);}
        shutdown(fd,SHUT_WR); close(fd);
    }
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(st,NULL); return 0;
}

static int nl_measure(void *state, measurement_t *result) {
    net_latency_state_t *s=(net_latency_state_t*)state;
    struct timespec t0,t1; volatile int64_t sink=0; int64_t total=0;
    atomic_store(&s->ready,0);atomic_store(&s->done,0);
    pthread_t st; pthread_create(&st,NULL,echo_server,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT);
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&(int){1},sizeof(int));
    setsockopt(fd,IPPROTO_TCP,TCP_QUICKACK,&(int){1},sizeof(int));
    if(connect(fd,(struct sockaddr*)&d,sizeof(d))<0){
        atomic_store_explicit(&s->done,1,memory_order_release); pthread_join(st,NULL); return -1;
    }
    char buf[MSG_SIZE],rbuf[MSG_SIZE]; memset(buf,'R',MSG_SIZE);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<NUM_RRS;i++){
        if(send(fd,buf,MSG_SIZE,0)!=MSG_SIZE) break;
        if(recv(fd,rbuf,MSG_SIZE,0)>0){sink+=rbuf[0];total++;}
    }
    clock_gettime(CLOCK_MONOTONIC,&t1);
    shutdown(fd,SHUT_WR); close(fd);
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(st,NULL);
    __asm__ __volatile__("":"+r"(sink));
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=el*1e6/total; result->wall_seconds=el;
    return 0;
}

static int nl_cleanup(void *state){net_latency_state_t *s=(net_latency_state_t*)state;close(s->listen_fd);free(s);return 0;}

benchmark_t bench_net_latency={
    .name="net-latency",.category="C13",
    .description="TCP RTT 64B loopback (NODELAY+QUICKACK on client+server, no Nagle/delACK)",
    .tier=1,.primary_metric_name="us/RR",.higher_is_better=false,
    .min_iterations=SSB_MIN_ITERATIONS,.max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC,.max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=net_latency_init,.warmup=nl_warmup,.measure=nl_measure,.cleanup=nl_cleanup,.num_threads=2,
};
SSB_BENCHMARK_REGISTER(bench_net_latency);
