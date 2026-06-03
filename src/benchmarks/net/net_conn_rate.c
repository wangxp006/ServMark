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

#define PORT 19992
#define NUM_CONNS 10000
#define LISTEN_BACKLOG 4096

typedef struct { int listen_fd; _Atomic int ready, done; _Atomic int64_t accepted; } net_conn_rate_state_t;

static void *acceptor(void *arg) {
    net_conn_rate_state_t *s=(net_conn_rate_state_t*)arg;
    struct sockaddr_in addr; socklen_t alen=sizeof(addr);
    atomic_store_explicit(&s->ready,1,memory_order_release);
    while(!atomic_load_explicit(&s->done,memory_order_acquire)){
        int c=accept(s->listen_fd,(struct sockaddr*)&addr,&alen);
        if(c>=0){close(c);atomic_fetch_add_explicit(&s->accepted,1,memory_order_relaxed);}
        else break;
    }
    return NULL;
}

static int net_conn_rate_init(void **state) {
    net_conn_rate_state_t *s=calloc(1,sizeof(*s));
    if(!s) return -1;
    s->listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(s->listen_fd<0){free(s);return -1;}
    setsockopt(s->listen_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
    struct sockaddr_in a={0}; a.sin_family=AF_INET;
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK); a.sin_port=htons(PORT);
    if(bind(s->listen_fd,(struct sockaddr*)&a,sizeof(a))<0){close(s->listen_fd);free(s);return -1;}
    listen(s->listen_fd,LISTEN_BACKLOG); *state=s; return 0;
}

static int nc_warmup(void *state) {
    net_conn_rate_state_t *s=(net_conn_rate_state_t*)state;
    atomic_store(&s->ready,0);atomic_store(&s->done,0);atomic_store(&s->accepted,0);
    pthread_t at; pthread_create(&at,NULL,acceptor,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT);
    for(int i=0;i<100;i++){int fd=socket(AF_INET,SOCK_STREAM,0);if(connect(fd,(struct sockaddr*)&d,sizeof(d))==0)close(fd);}
    atomic_store_explicit(&s->done,1,memory_order_release);
    shutdown(s->listen_fd,SHUT_RD); pthread_join(at,NULL);
    s->listen_fd=socket(AF_INET,SOCK_STREAM,0);
    setsockopt(s->listen_fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
    bind(s->listen_fd,(struct sockaddr*)&d,sizeof(d)); listen(s->listen_fd,LISTEN_BACKLOG);
    return 0;
}

static int nc_measure(void *state, measurement_t *result) {
    net_conn_rate_state_t *s=(net_conn_rate_state_t*)state;
    struct timespec t0,t1; int actual=0;
    atomic_store(&s->ready,0);atomic_store(&s->done,0);atomic_store(&s->accepted,0);
    pthread_t at; pthread_create(&at,NULL,acceptor,s);
    while(!atomic_load_explicit(&s->ready,memory_order_acquire));
    struct sockaddr_in d={0}; d.sin_family=AF_INET;
    d.sin_addr.s_addr=htonl(INADDR_LOOPBACK); d.sin_port=htons(PORT);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<NUM_CONNS;i++){
        int fd=socket(AF_INET,SOCK_STREAM,0);
        setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int));
        if(connect(fd,(struct sockaddr*)&d,sizeof(d))==0){close(fd);actual++;}
    }
    shutdown(s->listen_fd,SHUT_RD);
    atomic_store_explicit(&s->done,1,memory_order_release);
    pthread_join(at,NULL);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    double el=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    int64_t ac=atomic_load(&s->accepted);
    memset(result,0,sizeof(*result));
    result->primary_metric=(double)ac/el; result->wall_seconds=el;
    return (ac>=NUM_CONNS*0.95)?0:-1;
}

static int nc_cleanup(void *state){net_conn_rate_state_t *s=(net_conn_rate_state_t*)state;close(s->listen_fd);free(s);return 0;}

benchmark_t bench_net_conn_rate={
    .name="net-conn-rate",.category="C13",
    .description="TCP connection rate connect/accept/close (backlog=4096, actual count)",
    .tier=1,.primary_metric_name="conns/sec",.higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS,.max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC,.max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=net_conn_rate_init,.warmup=nc_warmup,.measure=nc_measure,.cleanup=nc_cleanup,.num_threads=2,
};
SSB_BENCHMARK_REGISTER(bench_net_conn_rate);
