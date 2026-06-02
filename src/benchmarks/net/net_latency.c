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

typedef struct {
    int listen_fd;
    _Atomic int ready;
    _Atomic int done;
} net_latency_state_t;

static void *echo_server(void *arg) {
    net_latency_state_t *s = (net_latency_state_t *)arg;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    atomic_store_explicit(&s->ready, 1, memory_order_release);
    int client = accept(s->listen_fd, (struct sockaddr *)&addr, &alen);
    if (client < 0) return NULL;

    char buf[MSG_SIZE];
    while (!atomic_load_explicit(&s->done, memory_order_acquire)) {
        ssize_t n = recv(client, buf, MSG_SIZE, 0);
        if (n <= 0) break;
        send(client, buf, n, 0);
    }
    close(client);
    return NULL;
}

static int net_latency_init(void **state) {
    net_latency_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) { free(s); return -1; }
    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(s->listen_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->listen_fd); free(s); return -1;
    }
    listen(s->listen_fd, 1);
    *state = s;
    return 0;
}

static int net_latency_warmup(void *state) {
    net_latency_state_t *s = (net_latency_state_t *)state;
    atomic_store(&s->ready, 0); atomic_store(&s->done, 0);
    pthread_t st;
    pthread_create(&st, NULL, echo_server, s);
    while (!atomic_load_explicit(&s->ready, memory_order_acquire)) ;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(PORT);
    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) == 0) {
        char buf[MSG_SIZE] = "ping";
        char rbuf[MSG_SIZE];
        for (int i = 0; i < 100; i++) {
            send(fd, buf, MSG_SIZE, 0);
            recv(fd, rbuf, MSG_SIZE, 0);
        }
        close(fd);
    }
    atomic_store_explicit(&s->done, 1, memory_order_release);
    pthread_join(st, NULL);
    return 0;
}

static int net_latency_measure(void *state, measurement_t *result) {
    net_latency_state_t *s = (net_latency_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;
    int64_t total_rrs = 0;

    atomic_store(&s->ready, 0); atomic_store(&s->done, 0);
    pthread_t st;
    pthread_create(&st, NULL, echo_server, s);
    while (!atomic_load_explicit(&s->ready, memory_order_acquire)) ;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(PORT);
    if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        atomic_store_explicit(&s->done, 1, memory_order_release);
        pthread_join(st, NULL);
        return -1;
    }

    char buf[MSG_SIZE], rbuf[MSG_SIZE];
    memset(buf, 'R', MSG_SIZE);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_RRS; i++) {
        send(fd, buf, MSG_SIZE, 0);
        if (recv(fd, rbuf, MSG_SIZE, 0) > 0) {
            sink += rbuf[0];
            total_rrs++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    close(fd);
    atomic_store_explicit(&s->done, 1, memory_order_release);
    pthread_join(st, NULL);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e6 / total_rrs; /* us/RR */
    result->wall_seconds = elapsed;
    return 0;
}

static int net_latency_cleanup(void *state) {
    net_latency_state_t *s = (net_latency_state_t *)state;
    close(s->listen_fd);
    free(s);
    return 0;
}

benchmark_t bench_net_latency = {
    .name = "net-latency",
    .category = "C13",
    .description = "TCP request-response latency (loopback, 64B)",
    .tier = 1,
    .primary_metric_name = "us/RR",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = net_latency_init,
    .warmup = net_latency_warmup,
    .measure = net_latency_measure,
    .cleanup = net_latency_cleanup,
    .num_threads = 2,
};
SSB_BENCHMARK_REGISTER(bench_net_latency);
