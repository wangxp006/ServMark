#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>

#define NUM_OPS 20000

/*
 * mmap/munmap latency benchmark.
 *
 * Measures mmap + munmap overhead for a single anonymous page.
 * Uses MAP_POPULATE to pre-fault the page during mmap, keeping the
 * page fault handler cost out of the timed region so we measure
 * pure VMA create/destroy overhead.
 *
 * On kernels 6.1+ (maple tree VMAs) the VMA manipulation path differs
 * from older rbtree kernels. The 20000-iteration loop heavily exercises
 * VMA merge/split logic. Per-VMA locking (kernel 6.4+) reduces
 * mmap_lock contention and will produce different results.
 */

typedef struct {
    int dummy;
} proc_mmap_state_t;

static int proc_mmap_init(void **state) {
    proc_mmap_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s;
    return 0;
}

static int proc_mmap_warmup(void *state) {
    (void)state;
    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;
    void *p = mmap(NULL, page_sz * 64, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != MAP_FAILED) munmap(p, page_sz * 64);
    return 0;
}

static int proc_mmap_measure(void *state, measurement_t *result) {
    (void)state;
    struct timespec t0, t1;
    size_t total_mapped = 0;

    long page_sz = sysconf(_SC_PAGESIZE);
    if (page_sz <= 0) page_sz = 4096;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    /*
     * MAP_POPULATE pre-faults the page during mmap so the timed region
     * measures only the VMA create/destroy cost, not the first-access
     * page fault handler (do_anonymous_page, PTE population, TLB fill).
     */
    for (int i = 0; i < NUM_OPS; i++) {
        void *p = mmap(NULL, page_sz, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
        if (p != MAP_FAILED) {
            munmap(p, page_sz);
            total_mapped++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    if (total_mapped > 0)
        result->primary_metric = elapsed * 1e6 / total_mapped;
    else
        result->primary_metric = 0.0;
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
    .description = "mmap/munmap anonymous page latency (MAP_POPULATE, VMA create/destroy only)",
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
