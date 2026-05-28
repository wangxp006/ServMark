#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>

#define QUEUE_DEPTH 32
#define SSB_BLOCK_SIZE 4096
#define NUM_IOS 20000

typedef struct {
    char *file_path;
    char *buffer;
    int ring_fd;
    struct io_uring_sqe *sqes;
    struct io_uring_cqe *cqes;
    void *sq_ring, *cq_ring, *sqes_mmap;
    int *offsets;
} fs_uring_state_t;

static int fs_uring_init(void **state) {
    fs_uring_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->file_path = strdup("/tmp/ssb_uring.dat");
    s->buffer = malloc(SSB_BLOCK_SIZE);
    s->offsets = malloc(NUM_IOS * sizeof(int));
    if (!s->file_path || !s->buffer || !s->offsets) {
        free(s->file_path); free(s->buffer); free(s->offsets); free(s);
        return -1;
    }
    /* Create test file for reads */
    int fd = open(s->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char *tmp = malloc(SSB_BLOCK_SIZE);
        memset(tmp, 'U', SSB_BLOCK_SIZE);
        int blocks = 64 * 1024 * 1024 / SSB_BLOCK_SIZE;
        for (int i = 0; i < blocks; i++) write(fd, tmp, SSB_BLOCK_SIZE);
        free(tmp);
        close(fd);
    }
    srand(time(NULL));
    int max_blocks = 64 * 1024 * 1024 / SSB_BLOCK_SIZE;
    for (int i = 0; i < NUM_IOS; i++)
        s->offsets[i] = (rand() % max_blocks) * SSB_BLOCK_SIZE;
    *state = s;
    return 0;
}

static int fs_uring_warmup(void *state) {
    fs_uring_state_t *s = (fs_uring_state_t *)state;
    /* Warmup using standard pread */
    int fd = open(s->file_path, O_RDONLY);
    if (fd >= 0) {
        for (int i = 0; i < 100; i++)
            pread(fd, s->buffer, SSB_BLOCK_SIZE, s->offsets[i]);
        close(fd);
    }
    return 0;
}

static int fs_uring_measure(void *state, measurement_t *result) {
    fs_uring_state_t *s = (fs_uring_state_t *)state;
    struct timespec t0, t1;
    volatile char sink = 0;
    int64_t total_ops = 0;

    /* Use synchronous pread as a portable approximation
     * (true io_uring requires kernel support and ring setup) */
    int fd = open(s->file_path, O_RDONLY | O_DIRECT);
    if (fd < 0) fd = open(s->file_path, O_RDONLY);
    if (fd < 0) return -1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_IOS; i++) {
        if (pread(fd, s->buffer, SSB_BLOCK_SIZE, s->offsets[i]) > 0) {
            sink += s->buffer[0];
            total_ops++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    close(fd);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_ops / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fs_uring_cleanup(void *state) {
    fs_uring_state_t *s = (fs_uring_state_t *)state;
    unlink(s->file_path);
    free(s->file_path); free(s->buffer); free(s->offsets); free(s);
    return 0;
}

benchmark_t bench_fs_uring = {
    .name = "fs-uring",
    .category = "C10",
    .description = "io_uring-style random read (sync fallback)",
    .tier = 2,
    .primary_metric_name = "IOPS",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fs_uring_init,
    .warmup = fs_uring_warmup,
    .measure = fs_uring_measure,
    .cleanup = fs_uring_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fs_uring);
