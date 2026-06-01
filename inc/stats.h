#ifndef SSB_STATS_H
#define SSB_STATS_H
#include "servmark.h"
#include "benchmark.h"

int stats_compute(const double *values, int count, benchmark_stats_t *out);
int stats_bootstrap_bca(const double *values, int count, int n_resamples, double *ci_lower, double *ci_upper);
double stats_geometric_mean(const double *values, int count);
double stats_shapiro_wilk(const double *values, int count);
double stats_anderson_darling(const double *values, int count);
int stats_bootstrap_percentile(const double *values, int count, int n_resamples, double *ci_lower, double *ci_upper);
int stats_welch_ttest(const double *a, int na, const double *b, int nb, double *p_value, double *ci_lower, double *ci_upper);
void stats_benjamini_hochberg(double *p_values, int count, double fdr);
int stats_mann_whitney(const double *a, int na, const double *b, int nb, double *p_value, double *hodges_lehmann);
int stats_detect_outliers(const double *values, int count, double alpha, int max_outliers, bool *outlier_mask);
const char *stats_reliability_label(double cv);
void stats_sort(double *values, int count);
void stats_mean_stddev(const double *values, int count, double *mean, double *stddev);
#endif
