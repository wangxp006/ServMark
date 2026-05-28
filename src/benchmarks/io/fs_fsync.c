#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define WRITE_SIZE (128 * 1024)
#define NUM_FSYNCS 5000

typedef struct {
    char *file_path;
    char *buffer;
} fs_fsync_state_t;

static int fs_fsync_init(void **state) {
    fs_fsync_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->file_path = strdup("/tmp/ssb_fsync.dat");
    s->buffer = malloc(WRITE_SIZE);
    if (!s->file_path || !s->buffer) {
        free(s->file_path); free(s->buffer); free(s);
        return -1;
    }
    memset(s->buffer, 'F', WRITE_SIZE);
    *state = s;
    return 0;
}

static int fs_fsync_warmup(void *state) {
    fs_fsync_state_t *s = (fs_fsync_state_t *)state;
    int fd = open(s->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, s->buffer, WRITE_SIZE);
        fsync(fd);
        close(fd);
    }
    return 0;
}

static int fs_fsync_measure(void *state, measurement_t *result) {
    fs_fsync_state_t *s = (fs_fsync_state_t *)state;
    struct timespec t0, t1;

    int fd = open(s->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_FSYNCS; i++) {
        lseek(fd, 0, SEEK_SET);
        write(fd, s->buffer, WRITE_SIZE);
        fsync(fd);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    close(fd);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e6 / NUM_FSYNCS;
    result->wall_seconds = elapsed;
    return 0;
}

static int fs_fsync_cleanup(void *state) {
    fs_fsync_state_t *s = (fs_fsync_state_t *)state;
    unlink(s->file_path);
    free(s->file_path); free(s->buffer); free(s);
    return 0;
}

benchmark_t bench_fs_fsync = {
    .name = "fs-fsync",
    .category = "C10",
    .description = "fsync latency (128KB write + fsync)",
    .tier = 1,
    .primary_metric_name = "us/fsync",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fs_fsync_init,
    .warmup = fs_fsync_warmup,
    .measure = fs_fsync_measure,
    .cleanup = fs_fsync_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fs_fsync);
