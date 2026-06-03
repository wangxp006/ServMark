#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/time.h>

#define PORT 19991
#define MSG_SIZE 1472
#define NUM_MSGS 100000
#define UDP_BUF (16*1024*1024)

typedef struct { int fd; _Atomic int ready,done; _Atomic int64_t packets; } net_udp_state_t;

static void *udp_receiver(void *arg) {
    net_udp_state_t *s=(net_udp_state_t*)arg;
    char buf[MSG_SIZE];
    atomic_store_explicit(&s->ready,1,memory_order_release);
    struct sockaddr_in from; socklen_t flen=sizeof(from);
    struct timeval tv={0,100000}; setsockopt(s->fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    while(!atomic_load_explicit(&s->done,memory_order_acquire)){
        ssize_t n=recvfrom(s->fd,buf,MSG_SIZE,0,(struct sockaddr*)&from,&flen);
        if(n>0) atomic_fetch_add_explicit(&s->packets,1,memory_order_relaxed);
    }
    return NULL;
}

static int net_udp_init(void **state) {
    net_udp_state_t *s=calloc(1,sizeof(*s));
    if(!s) return -1;
    s->fd=socket(AF_INET,SOCK_DGRAM,0);
    if(s->fd<0){free(s);return -1;}
    int rcv=UDP_BUF; setsockopt(s->fd,SOL_SOCKET,SO_RCVBUF,&rcv,sizeof(rcv));
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT);
    if(bind(s->fd,(struct sockaddr*)&a,sizeof(a))<0){close(s->fd);free(s);return -1;}
    *state=s; return 0;
}

static int nu_warmup(void *state) {
    net_udp_state_t *s=(net_udp_state_t*)state;
    atomic_store(&s->ready,0);atomic_store(&s->packets,0);atomic_store(&s->done,0);
    pthread_t rt; pthread_create(&rt,NULL,udp_receiver,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int sf=socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT);
    char buf[MSG_SIZE]; memset(buf,'U',MSG_SIZE);
    for(int i=0;i<100;i++) sendto(sf,buf,MSG_SIZE,0,(struct sockaddr*)&d,sizeof(d));
    close(sf);
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(rt,NULL); return 0;
}

static int nu_measure(void *state, measurement_t *result) {
    net_udp_state_t *s=(net_udp_state_t*)state;
    struct timespec t0,t1;
    atomic_store(&s->ready,0);atomic_store(&s->packets,0);atomic_store(&s->done,0);
    pthread_t rt; pthread_create(&rt,NULL,udp_receiver,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    int sf=socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT);
    char *buf=malloc(MSG_SIZE); memset(buf,'U',MSG_SIZE);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<NUM_MSGS;i++) sendto(sf,buf,MSG_SIZE,0,(struct sockaddr*)&d,sizeof(d));
    close(sf);
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(rt,NULL); free(buf);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)atomic_load(&s->packets)/el;
    result->wall_seconds=el; return 0;
}

static int nu_cleanup(void *state){net_udp_state_t *s=(net_udp_state_t*)state;close(s->fd);free(s);return 0;}

benchmark_t bench_net_udp={
    .name="net-udp",.category="C13",
    .description="UDP loopback 1472B pkt (16MB rcvbuf, 100ms timeout, no silent loss)",
    .tier=1,.primary_metric_name="packets/sec",.higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS,.max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC,.max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=net_udp_init,.warmup=nu_warmup,.measure=nu_measure,.cleanup=nu_cleanup,.num_threads=2,
};
SSB_BENCHMARK_REGISTER(bench_net_udp);
