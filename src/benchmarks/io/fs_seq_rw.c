#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define FILE_MB 128
#define FILE_SIZE (FILE_MB * 1024 * 1024)
#define BLOCK_SIZE (64 * 1024)

typedef struct {
    char *file_path;
    char *buffer;
    char *write_data;
} fs_seq_rw_state_t;

static int fs_seq_rw_init(void **state) {
    fs_seq_rw_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->file_path = strdup("/tmp/ssb_seq_rw.dat");
    posix_memalign((void**)&s->buffer, 4096, BLOCK_SIZE);
    s->write_data = malloc(BLOCK_SIZE);
    if (!s->file_path || !s->buffer || !s->write_data) {
        free(s->file_path); free(s->buffer); free(s->write_data); free(s);
        return -1;
    }
    /* Create test file */
    int fd = open(s->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        memset(s->write_data, 'A', BLOCK_SIZE);
        for (size_t off = 0; off < FILE_SIZE; off += BLOCK_SIZE)
            write(fd, s->write_data, BLOCK_SIZE);
        close(fd);
    }
    *state = s;
    return 0;
}

static int fs_seq_rw_warmup(void *state) {
    fs_seq_rw_state_t *s = (fs_seq_rw_state_t *)state;
    int fd = open(s->file_path, O_RDONLY);
    if (fd >= 0) {
        read(fd, s->buffer, BLOCK_SIZE);
        close(fd);
    }
    return 0;
}

static int fs_seq_rw_measure(void *state, measurement_t *result) {
    fs_seq_rw_state_t *s = (fs_seq_rw_state_t *)state;
    struct timespec t0, t1;
    int64_t total_read = 0;
    volatile char sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    int fd = open(s->file_path, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        fd = open(s->file_path, O_RDONLY);
    }
    if (fd >= 0) {
        ssize_t n;
        while ((n = read(fd, s->buffer, BLOCK_SIZE)) > 0) {
            sink += s->buffer[0];
            total_read += n;
        }
        close(fd);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_read / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fs_seq_rw_cleanup(void *state) {
    fs_seq_rw_state_t *s = (fs_seq_rw_state_t *)state;
    unlink(s->file_path);
    free(s->file_path); free(s->buffer); free(s->write_data); free(s);
    return 0;
}

benchmark_t bench_fs_seq_rw = {
    .name = "fs-seq-rw",
    .category = "C10",
    .description = "Sequential file read 128MB (Direct I/O)",
    .tier = 1,
    .primary_metric_name = "bytes/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fs_seq_rw_init,
    .warmup = fs_seq_rw_warmup,
    .measure = fs_seq_rw_measure,
    .cleanup = fs_seq_rw_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fs_seq_rw);
