#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define FILE_SIZE (64 * 1024 * 1024)
#define BLOCK_SIZE 4096
#define NUM_OPS 50000

typedef struct {
    char *file_path;
    char *buffer;
    int *offsets;
} fs_rand_rw_state_t;

static int fs_rand_rw_init(void **state) {
    fs_rand_rw_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->file_path = strdup("/tmp/ssb_rand_rw.dat");
    s->buffer = malloc(BLOCK_SIZE);
    s->offsets = malloc(NUM_OPS * sizeof(int));
    if (!s->file_path || !s->buffer || !s->offsets) {
        free(s->file_path); free(s->buffer); free(s->offsets); free(s);
        return -1;
    }
    /* Create test file */
    int fd = open(s->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char *tmp = malloc(BLOCK_SIZE);
        memset(tmp, 'B', BLOCK_SIZE);
        for (size_t off = 0; off < FILE_SIZE; off += BLOCK_SIZE)
            write(fd, tmp, BLOCK_SIZE);
        free(tmp);
        close(fd);
    }
    /* Precompute random offsets */
    srand(time(NULL));
    int max_blocks = FILE_SIZE / BLOCK_SIZE;
    for (int i = 0; i < NUM_OPS; i++)
        s->offsets[i] = (rand() % max_blocks) * BLOCK_SIZE;
    *state = s;
    return 0;
}

static int fs_rand_rw_warmup(void *state) {
    fs_rand_rw_state_t *s = (fs_rand_rw_state_t *)state;
    int fd = open(s->file_path, O_RDONLY);
    if (fd >= 0) {
        for (int i = 0; i < 100; i++)
            pread(fd, s->buffer, BLOCK_SIZE, s->offsets[i]);
        close(fd);
    }
    return 0;
}

static int fs_rand_rw_measure(void *state, measurement_t *result) {
    fs_rand_rw_state_t *s = (fs_rand_rw_state_t *)state;
    struct timespec t0, t1;
    volatile char sink = 0;
    int64_t total_ops = 0;

    int fd = open(s->file_path, O_RDONLY | O_DIRECT);
    if (fd < 0) fd = open(s->file_path, O_RDONLY);
    if (fd < 0) { return -1; }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_OPS; i++) {
        if (pread(fd, s->buffer, BLOCK_SIZE, s->offsets[i]) > 0) {
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

static int fs_rand_rw_cleanup(void *state) {
    fs_rand_rw_state_t *s = (fs_rand_rw_state_t *)state;
    unlink(s->file_path);
    free(s->file_path); free(s->buffer); free(s->offsets); free(s);
    return 0;
}

benchmark_t bench_fs_rand_rw = {
    .name = "fs-rand-rw",
    .category = "C10",
    .description = "Random 4KB read IOPS",
    .tier = 1,
    .primary_metric_name = "IOPS",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fs_rand_rw_init,
    .warmup = fs_rand_rw_warmup,
    .measure = fs_rand_rw_measure,
    .cleanup = fs_rand_rw_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fs_rand_rw);
