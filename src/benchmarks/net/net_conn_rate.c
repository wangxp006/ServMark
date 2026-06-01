#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 19992
#define NUM_CONNS 10000

typedef struct {
    int listen_fd;
    volatile int ready;
    volatile int done;
    volatile int64_t accepted;
} net_conn_rate_state_t;

static void *acceptor(void *arg) {
    net_conn_rate_state_t *s = (net_conn_rate_state_t *)arg;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    s->ready = 1;
    while (!s->done) {
        int c = accept(s->listen_fd, (struct sockaddr *)&addr, &alen);
        if (c >= 0) {
            close(c);
            __sync_fetch_and_add(&s->accepted, 1);
        }
    }
    return NULL;
}

static int net_conn_rate_init(void **state) {
    net_conn_rate_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) { free(s); return -1; }
    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->listen_fd); free(s); return -1;
    }
    listen(s->listen_fd, 128);
    *state = s;
    return 0;
}

static int net_conn_rate_warmup(void *state) {
    net_conn_rate_state_t *s = (net_conn_rate_state_t *)state;
    s->ready = 0; s->done = 0; s->accepted = 0;
    pthread_t at;
    pthread_create(&at, NULL, acceptor, s);
    while (!s->ready) ;
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(PORT);
    for (int i = 0; i < 100; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) == 0) close(fd);
    }
    s->done = 1;
    pthread_join(at, NULL);
    return 0;
}

static int net_conn_rate_measure(void *state, measurement_t *result) {
    net_conn_rate_state_t *s = (net_conn_rate_state_t *)state;
    struct timespec t0, t1;

    s->ready = 0; s->done = 0; s->accepted = 0;
    pthread_t at;
    pthread_create(&at, NULL, acceptor, s);
    while (!s->ready) ;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(PORT);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_CONNS; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(fd, (struct sockaddr *)&dst, sizeof(dst)) == 0)
            close(fd);
    }

    s->done = 1;
    pthread_join(at, NULL);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_CONNS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int net_conn_rate_cleanup(void *state) {
    net_conn_rate_state_t *s = (net_conn_rate_state_t *)state;
    close(s->listen_fd);
    free(s);
    return 0;
}

benchmark_t bench_net_conn_rate = {
    .name = "net-conn-rate",
    .category = "C13",
    .description = "TCP connection establishment rate (connect/accept/close)",
    .tier = 1,
    .primary_metric_name = "conns/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = net_conn_rate_init,
    .warmup = net_conn_rate_warmup,
    .measure = net_conn_rate_measure,
    .cleanup = net_conn_rate_cleanup,
    .num_threads = 2,
};
SSB_BENCHMARK_REGISTER(bench_net_conn_rate);
