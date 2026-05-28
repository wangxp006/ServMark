#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT_BASE 19990
#define MSG_SIZE (64 * 1024)
#define NUM_MSGS 2000

typedef struct {
    int listen_fd;
    volatile int ready;
    volatile int64_t total_bytes;
} net_tcp_state_t;

static void *tcp_server(void *arg) {
    net_tcp_state_t *s = (net_tcp_state_t *)arg;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    s->ready = 1;
    int client = accept(s->listen_fd, (struct sockaddr *)&addr, &alen);
    if (client < 0) return NULL;

    char *buf = malloc(MSG_SIZE);
    ssize_t n;
    while ((n = recv(client, buf, MSG_SIZE, 0)) > 0)
        __sync_fetch_and_add(&s->total_bytes, n);
    free(buf);
    close(client);
    return NULL;
}

static int net_tcp_init(void **state) {
    net_tcp_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) { free(s); return -1; }

    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT_BASE);

    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->listen_fd); free(s); return -1;
    }
    listen(s->listen_fd, 1);
    *state = s;
    return 0;
}

static int net_tcp_warmup(void *state) {
    net_tcp_state_t *s = (net_tcp_state_t *)state;
    s->ready = 0; s->total_bytes = 0;
    pthread_t st;
    pthread_create(&st, NULL, tcp_server, s);
    while (!s->ready) ;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT_BASE);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        char buf[MSG_SIZE];
        memset(buf, 'T', MSG_SIZE);
        for (int i = 0; i < 20; i++)
            send(fd, buf, MSG_SIZE, 0);
        close(fd);
    }
    pthread_join(st, NULL);
    return 0;
}

static int net_tcp_measure(void *state, measurement_t *result) {
    net_tcp_state_t *s = (net_tcp_state_t *)state;
    struct timespec t0, t1;

    s->ready = 0; s->total_bytes = 0;
    pthread_t st;
    pthread_create(&st, NULL, tcp_server, s);
    while (!s->ready) ;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT_BASE);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        pthread_join(st, NULL);
        return -1;
    }

    char *buf = malloc(MSG_SIZE);
    memset(buf, 'T', MSG_SIZE);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_MSGS; i++)
        send(fd, buf, MSG_SIZE, 0);

    shutdown(fd, SHUT_WR);
    close(fd);

    pthread_join(st, NULL);
    free(buf);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)s->total_bytes / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int net_tcp_cleanup(void *state) {
    net_tcp_state_t *s = (net_tcp_state_t *)state;
    close(s->listen_fd);
    free(s);
    return 0;
}

benchmark_t bench_net_tcp = {
    .name = "net-tcp",
    .category = "C13",
    .description = "TCP loopback stream throughput (64KB messages)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = net_tcp_init,
    .warmup = net_tcp_warmup,
    .measure = net_tcp_measure,
    .cleanup = net_tcp_cleanup,
    .num_threads = 2,
};
SSB_BENCHMARK_REGISTER(bench_net_tcp);
