#ifndef SSB_BENCHMARK_H
#define SSB_BENCHMARK_H
#include "servsysbench.h"

typedef struct {
    double primary_metric;
    double wall_seconds;
    double cpu_seconds;
    uint64_t instructions;
    uint64_t cycles;
    uint64_t cache_misses;
} measurement_t;

typedef struct {
    double mean, stddev, sem, cv;
    double ci_lower, ci_upper;
    int iterations;
    bool converged;
    const char *reliability;
    double raw_values[SSB_MAX_ITERATIONS];
} benchmark_stats_t;

struct benchmark_s {
    const char *name, *category, *description;
    int tier;
    const char *primary_metric_name;
    bool higher_is_better;
    int min_iterations, max_iterations;
    double convergence_target;
    int min_runtime_sec, max_runtime_sec;
    bool cooldown_required;
    int (*init)(void **state);
    int (*warmup)(void *state);
    int (*measure)(void *state, measurement_t *result);
    int (*cleanup)(void *state);
    int num_threads;
};

int benchmark_register(const benchmark_t *bench);
int benchmark_get_all(const benchmark_t ***list, int *count);
int benchmark_get_by_category(const char *cat, const benchmark_t ***list, int *count);
int benchmark_get_by_tier(int tier, const benchmark_t ***list, int *count);

#define SSB_BENCHMARK_REGISTER(bench) \
    static void __attribute__((constructor)) _ssb_reg_##bench(void) { benchmark_register(&bench); }
#endif
