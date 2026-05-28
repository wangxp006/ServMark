#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 19991
#define MSG_SIZE 1472
#define NUM_MSGS 100000

typedef struct {
    int fd;
    volatile int ready;
    volatile int64_t packets;
    volatile int done;
} net_udp_state_t;

static void *udp_receiver(void *arg) {
    net_udp_state_t *s = (net_udp_state_t *)arg;
    char buf[MSG_SIZE];
    s->ready = 1;
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    while (!s->done) {
        if (recvfrom(s->fd, buf, MSG_SIZE, 0, (struct sockaddr *)&from, &flen) > 0)
            __sync_fetch_and_add(&s->packets, 1);
    }
    return NULL;
}

static int net_udp_init(void **state) {
    net_udp_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd < 0) { free(s); return -1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(s->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->fd); free(s); return -1;
    }
    *state = s;
    return 0;
}

static int net_udp_warmup(void *state) {
    net_udp_state_t *s = (net_udp_state_t *)state;
    s->ready = 0; s->packets = 0; s->done = 0;
    pthread_t rt;
    pthread_create(&rt, NULL, udp_receiver, s);
    while (!s->ready) ;

    int send_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(PORT);
    char buf[MSG_SIZE];
    memset(buf, 'U', MSG_SIZE);
    for (int i = 0; i < 100; i++)
        sendto(send_fd, buf, MSG_SIZE, 0, (struct sockaddr *)&dst, sizeof(dst));
    close(send_fd);
    s->done = 1;
    pthread_join(rt, NULL);
    return 0;
}

static int net_udp_measure(void *state, measurement_t *result) {
    net_udp_state_t *s = (net_udp_state_t *)state;
    struct timespec t0, t1;

    s->ready = 0; s->packets = 0; s->done = 0;
    pthread_t rt;
    pthread_create(&rt, NULL, udp_receiver, s);
    while (!s->ready) ;

    int send_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(PORT);

    char *buf = malloc(MSG_SIZE);
    memset(buf, 'U', MSG_SIZE);

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_MSGS; i++)
        sendto(send_fd, buf, MSG_SIZE, 0, (struct sockaddr *)&dst, sizeof(dst));

    close(send_fd);
    s->done = 1;
    pthread_join(rt, NULL);
    free(buf);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)s->packets / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int net_udp_cleanup(void *state) {
    net_udp_state_t *s = (net_udp_state_t *)state;
    close(s->fd);
    free(s);
    return 0;
}

benchmark_t bench_net_udp = {
    .name = "net-udp",
    .category = "C13",
    .description = "UDP loopback packet throughput",
    .tier = 1,
    .primary_metric_name = "packets/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = net_udp_init,
    .warmup = net_udp_warmup,
    .measure = net_udp_measure,
    .cleanup = net_udp_cleanup,
    .num_threads = 2,
};
SSB_BENCHMARK_REGISTER(bench_net_udp);
