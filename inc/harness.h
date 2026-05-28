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
int harness_run_single(const benchmark_t *bench, run_mode_t mode, benchmark_stats_t *stats);
void harness_free_result(run_result_t *result);
#endif
