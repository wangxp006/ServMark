#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define NUM_OPS 5000000

typedef struct {
    int efd;
} ipc_eventfd_state_t;

static int ipc_eventfd_init(void **state) {
    ipc_eventfd_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->efd = eventfd(0, EFD_NONBLOCK);
    if (s->efd < 0) { free(s); return -1; }
    /* Pre-seed with value to allow reads */
    uint64_t val = 1;
    write(s->efd, &val, sizeof(val));
    *state = s;
    return 0;
}

static int ipc_eventfd_warmup(void *state) {
    ipc_eventfd_state_t *s = (ipc_eventfd_state_t *)state;
    uint64_t val = 1;
    for (int i = 0; i < 10000; i++) {
        write(s->efd, &val, sizeof(val));
        read(s->efd, &val, sizeof(val));
    }
    return 0;
}

static int ipc_eventfd_measure(void *state, measurement_t *result) {
    ipc_eventfd_state_t *s = (ipc_eventfd_state_t *)state;
    struct timespec t0, t1;
    int64_t total_ops = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_OPS; i++) {
        uint64_t val = 1;
        write(s->efd, &val, sizeof(val));
        read(s->efd, &val, sizeof(val));
        total_ops++;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_ops / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int ipc_eventfd_cleanup(void *state) {
    ipc_eventfd_state_t *s = (ipc_eventfd_state_t *)state;
    close(s->efd);
    free(s);
    return 0;
}

benchmark_t bench_ipc_eventfd = {
    .name = "ipc-eventfd",
    .category = "C11",
    .description = "eventfd write/read roundtrip throughput",
    .tier = 1,
    .primary_metric_name = "roundtrips/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = ipc_eventfd_init,
    .warmup = ipc_eventfd_warmup,
    .measure = ipc_eventfd_measure,
    .cleanup = ipc_eventfd_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_ipc_eventfd);
