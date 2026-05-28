#ifndef SSB_SCORING_H
#define SSB_SCORING_H
#include "harness.h"

typedef struct { const char *id, *name; double weight; const char *group; } category_weight_t;

const category_weight_t *scoring_get_categories(int *count);
double scoring_normalize(double raw_score, double baseline);
double scoring_compute_baseline(const double *raw_scores, int count);
double scoring_category_score(const double *sub_scores, int count);
int scoring_compute_pillars(const run_result_t *result, double *throughput, double *latency, double *efficiency);
double scoring_overall_score(double t, double l, double e);
double scoring_mitigation_tax(double mitigated, double unmitigated);
double scoring_thermal_derating(double sustained, double peak);
int scoring_load_reference(const char *path, double **baselines, char ***names, int *count);
double scoring_estimate_runtime(int subtest_count, bool peak_mode);
#endif
