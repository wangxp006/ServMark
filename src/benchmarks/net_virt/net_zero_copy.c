#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

#define FILE_SIZE (64 * 1024 * 1024)
#define SENDFILE_PASSES 20

typedef struct {
    char *file_path;
    int out_fd;
} net_zero_copy_state_t;

static int net_zero_copy_init(void **state) {
    net_zero_copy_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->file_path = strdup("/tmp/ssb_sendfile_src.bin");
    if (!s->file_path) { free(s); return -1; }

    /* Create test file */
    int fd = open(s->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(s->file_path); free(s); return -1; }
    char *buf = malloc(65536);
    memset(buf, 'Z', 65536);
    size_t remain = FILE_SIZE;
    while (remain > 0) {
        size_t n = remain > 65536 ? 65536 : remain;
        write(fd, buf, n);
        remain -= n;
    }
    free(buf);
    close(fd);

    s->out_fd = open("/dev/null", O_WRONLY);
    *state = s;
    return 0;
}

static int net_zero_copy_warmup(void *state) {
    net_zero_copy_state_t *s = (net_zero_copy_state_t *)state;
    int fd = open(s->file_path, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        fstat(fd, &st);
        sendfile(s->out_fd, fd, NULL, st.st_size / 10);
        close(fd);
    }
    return 0;
}

static int net_zero_copy_measure(void *state, measurement_t *result) {
    net_zero_copy_state_t *s = (net_zero_copy_state_t *)state;
    struct timespec t0, t1;
    int64_t total_sent = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int pass = 0; pass < SENDFILE_PASSES; pass++) {
        int fd = open(s->file_path, O_RDONLY);
        if (fd < 0) continue;
        struct stat st;
        fstat(fd, &st);
        off_t offset = 0;
        while ((size_t)offset < (size_t)st.st_size) {
            ssize_t n = sendfile(s->out_fd, fd, &offset, st.st_size - offset);
            if (n <= 0) break;
            total_sent += n;
        }
        close(fd);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_sent / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int net_zero_copy_cleanup(void *state) {
    net_zero_copy_state_t *s = (net_zero_copy_state_t *)state;
    unlink(s->file_path);
    if (s->out_fd >= 0) close(s->out_fd);
    free(s->file_path); free(s);
    return 0;
}

benchmark_t bench_net_zero_copy = {
    .name = "net-zero-copy",
    .category = "C13",
    .description = "sendfile() zero-copy throughput (file -> /dev/null)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = net_zero_copy_init,
    .warmup = net_zero_copy_warmup,
    .measure = net_zero_copy_measure,
    .cleanup = net_zero_copy_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_net_zero_copy);
