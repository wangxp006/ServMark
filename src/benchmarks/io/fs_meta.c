#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define NUM_FILES 50000
#define TEST_DIR "/tmp/ssb_meta_test"

typedef struct {
    char *dir_path;
} fs_meta_state_t;

static int fs_meta_init(void **state) {
    fs_meta_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->dir_path = strdup(TEST_DIR);
    mkdir(s->dir_path, 0755);
    /* Pre-create files for stat test */
    for (int i = 0; i < NUM_FILES; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/f_%d", s->dir_path, i);
        close(open(path, O_WRONLY | O_CREAT, 0644));
    }
    *state = s;
    return 0;
}

static int fs_meta_warmup(void *state) {
    fs_meta_state_t *s = (fs_meta_state_t *)state;
    struct stat st;
    char path[512];
    for (int i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "%s/f_%d", s->dir_path, i);
        stat(path, &st);
    }
    return 0;
}

static int fs_meta_measure(void *state, measurement_t *result) {
    fs_meta_state_t *s = (fs_meta_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    struct stat st;
    char path[512];

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(path, sizeof(path), "%s/f_%d", s->dir_path, i);
        if (stat(path, &st) == 0)
            sink += st.st_size;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)NUM_FILES / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fs_meta_cleanup(void *state) {
    fs_meta_state_t *s = (fs_meta_state_t *)state;
    for (int i = 0; i < NUM_FILES; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/f_%d", s->dir_path, i);
        unlink(path);
    }
    rmdir(s->dir_path);
    free(s->dir_path); free(s);
    return 0;
}

benchmark_t bench_fs_meta = {
    .name = "fs-meta",
    .category = "C10",
    .description = "Filesystem metadata stat() throughput (50K files)",
    .tier = 1,
    .primary_metric_name = "stats/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fs_meta_init,
    .warmup = fs_meta_warmup,
    .measure = fs_meta_measure,
    .cleanup = fs_meta_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fs_meta);
