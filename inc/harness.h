#ifndef SSB_HARNESS_H
#define SSB_HARNESS_H
#include "servmark.h"
#include "benchmark.h"

typedef enum { SSB_MODE_VALIDATE, SSB_MODE_PEAK, SSB_MODE_SUSTAINED } run_mode_t;

typedef struct {
    run_mode_t mode;
    bool mitigations_off;
    int tier_mask;
    const char *output_dir, *reference_file, *category_filter;
    bool dry_run;
    int num_instances;
    char **bench_filter;
    int bench_filter_count;
    /* Per-benchmark overrides (0 = use benchmark default) */
    int min_iterations;
    int max_iterations;
    double convergence_target;
    int min_runtime_sec;
    int max_runtime_sec;
    int cooldown_sec;
    bool reportable;         /* skip benchmarks with CV >= 10% */
    bool require_validate;   /* auto-run --validate before benchmark run */
    /* Manual CPU / NUMA binding */
    char *cpu_pin_spec;      /* "auto" | "0,2,4,6" | "0-7" — CPU instance pinning */
    char *numa_bind_spec;    /* "18:0,19:0,20:1" or "0-15:0,16-31:1" — per-CPU NUMA binding */
} run_config_t;

typedef struct {
    const benchmark_t *bench;
    benchmark_stats_t stats;
    double normalized_score;
    const char *status;
} subtest_result_t;

struct run_result_s {
    char run_id[37];
    run_config_t config;
    system_info_t *sysinfo;
    subtest_result_t *subtests;
    int subtest_count;
    double category_scores[16];
    double pillar_throughput, pillar_latency, pillar_efficiency;
    double overall_score;
    double mitigation_tax_pct, thermal_derating_factor;
    time_t start_time, end_time;
};

int harness_run(const run_config_t *config, run_result_t **result);
int harness_run_single(const benchmark_t *bench, run_mode_t mode, benchmark_stats_t *stats,
                       const run_config_t *overrides);
void harness_free_result(run_result_t *result);
#endif
