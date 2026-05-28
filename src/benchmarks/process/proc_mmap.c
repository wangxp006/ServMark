#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_OPS 20000
#define PAGE_SIZE 4096

typedef struct {
    int dummy;
} proc_mmap_state_t;

static int proc_mmap_init(void **state) {
    proc_mmap_state_t *s = calloc(1, sizeof(*s));
    *state = s;
    return (s != NULL) ? 0 : -1;
}

static int proc_mmap_warmup(void *state) {
    (void)state;
    void *p = mmap(NULL, PAGE_SIZE * 64, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) munmap(p, PAGE_SIZE * 64);
    return 0;
}

static int proc_mmap_measure(void *state, measurement_t *result) {
    (void)state;
    struct timespec t0, t1;
    size_t total_mapped = 0;
    volatile char sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_OPS; i++) {
        void *p = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
            *(char *)p = 0x42;
            sink += *(char *)p;
            munmap(p, PAGE_SIZE);
            total_mapped++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e6 / total_mapped;
    result->wall_seconds = elapsed;
    return 0;
}

static int proc_mmap_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_proc_mmap = {
    .name = "proc-mmap",
    .category = "C6",
    .description = "mmap/munmap 4KB anonymous latency",
    .tier = 1,
    .primary_metric_name = "us/op",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = proc_mmap_init,
    .warmup = proc_mmap_warmup,
    .measure = proc_mmap_measure,
    .cleanup = proc_mmap_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_proc_mmap);
