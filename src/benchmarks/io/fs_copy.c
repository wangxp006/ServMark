#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define FILE_SIZE (16 * 1024 * 1024)  /* 16MB */
#define COPY_BUF_SIZE 4096
#define COPY_PASSES 10

typedef struct {
    char *src_path, *dst_path;
    char *buffer;
    size_t buf_size;
} fs_copy_state_t;

static int fs_copy_init(void **state) {
    fs_copy_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->src_path = strdup("/tmp/ssb_fscopy_src.bin");
    s->dst_path = strdup("/tmp/ssb_fscopy_dst.bin");
    s->buf_size = COPY_BUF_SIZE;
    s->buffer = malloc(COPY_BUF_SIZE);
    if (!s->src_path || !s->dst_path || !s->buffer) {
        free(s->src_path); free(s->dst_path); free(s->buffer); free(s);
        return -1;
    }
    /* Create source file */
    int fd = open(s->src_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(s->src_path); free(s->dst_path); free(s->buffer); free(s); return -1; }
    for (size_t off = 0; off < FILE_SIZE; off += COPY_BUF_SIZE) {
        for (size_t i = 0; i < COPY_BUF_SIZE; i++)
            s->buffer[i] = rand() & 0xFF;
        write(fd, s->buffer, COPY_BUF_SIZE);
    }
    close(fd);
    *state = s;
    return 0;
}

static int fs_copy_warmup(void *state) {
    fs_copy_state_t *s = (fs_copy_state_t *)state;
    int sfd = open(s->src_path, O_RDONLY);
    int dfd = open(s->dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sfd >= 0 && dfd >= 0) {
        char tmp[COPY_BUF_SIZE];
        ssize_t n;
        while ((n = read(sfd, tmp, COPY_BUF_SIZE)) > 0)
            write(dfd, tmp, n);
    }
    if (sfd >= 0) close(sfd);
    if (dfd >= 0) close(dfd);
    return 0;
}

static int fs_copy_measure(void *state, measurement_t *result) {
    fs_copy_state_t *s = (fs_copy_state_t *)state;
    struct timespec t0, t1;
    int64_t total_copied = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int pass = 0; pass < COPY_PASSES; pass++) {
        int sfd = open(s->src_path, O_RDONLY);
        int dfd = open(s->dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (sfd < 0 || dfd < 0) {
            if (sfd >= 0) close(sfd);
            if (dfd >= 0) close(dfd);
            continue;
        }
        ssize_t n;
        while ((n = read(sfd, s->buffer, COPY_BUF_SIZE)) > 0) {
            write(dfd, s->buffer, n);
            total_copied += n;
        }
        close(sfd); close(dfd);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_copied / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fs_copy_cleanup(void *state) {
    fs_copy_state_t *s = (fs_copy_state_t *)state;
    unlink(s->src_path); unlink(s->dst_path);
    free(s->src_path); free(s->dst_path); free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_fs_copy = {
    .name = "fs-copy",
    .category = "C10",
    .description = "File copy 4096B buffer (UnixBench File Copy exact equivalent)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fs_copy_init,
    .warmup = fs_copy_warmup,
    .measure = fs_copy_measure,
    .cleanup = fs_copy_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fs_copy);
