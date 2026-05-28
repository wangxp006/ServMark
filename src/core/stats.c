#include "stats.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int _compare_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

void stats_sort(double *values, int count) {
    qsort(values, count, sizeof(double), _compare_double);
}

void stats_mean_stddev(const double *values, int count,
        double *mean, double *stddev) {
    double sum = 0.0;
    for (int i = 0; i < count; i++) sum += values[i];
    *mean = sum / count;
    double ssq = 0.0;
    for (int i = 0; i < count; i++) {
        double d = values[i] - *mean;
        ssq += d * d;
    }
    *stddev = sqrt(ssq / (count - 1));
}

double stats_geometric_mean(const double *values, int count) {
    double log_sum = 0.0;
    for (int i = 0; i < count; i++) {
        log_sum += log(values[i]);
    }
    return exp(log_sum / count);
}

int stats_compute(const double *values, int count, benchmark_stats_t *out) {
    if (count < 2 || !out) return -1;
    memset(out, 0, sizeof(*out));

    stats_mean_stddev(values, count, &out->mean, &out->stddev);
    out->iterations = count;
    out->sem = out->stddev / sqrt((double)count);
    out->cv = (out->mean != 0) ? out->stddev / fabs(out->mean) : 1.0;

    /* convergence: stop condition in harness is SEM/mean <= 0.02 */
    double sem_rel = (out->mean != 0) ? out->sem / fabs(out->mean) : 1.0;
    out->converged = (sem_rel <= 0.02);

    /* reliability */
    out->reliability = stats_reliability_label(out->cv);

    /* store raw values */
    memcpy(out->raw_values, values, count * sizeof(double));

    /* bootstrap CI */
    stats_bootstrap_bca(values, count, SSB_BOOTSTRAP_RESAMPLES,
            &out->ci_lower, &out->ci_upper);

    return 0;
}

const char *stats_reliability_label(double cv) {
    if (cv < SSB_CV_STABLE) return "stable";
    if (cv < SSB_CV_MODERATE) return "moderate";
    if (cv < SSB_CV_HIGH) return "high";
    return "unreliable";
}

int stats_bootstrap_bca(const double *values, int count,
        int n_resamples, double *ci_lower, double *ci_upper) {
    if (count < 2 || n_resamples < 100) return -1;

    double *sample = malloc(count * sizeof(double));
    double *means = malloc(n_resamples * sizeof(double));
    if (!sample || !means) { free(sample); free(means); return -1; }

    double orig_mean;
    stats_mean_stddev(values, count, &orig_mean, &(double){0});

    /* Generate bootstrap distribution of means */
    for (int b = 0; b < n_resamples; b++) {
        for (int i = 0; i < count; i++) {
            sample[i] = values[rand() % count];
        }
        double m;
        stats_mean_stddev(sample, count, &m, &(double){0});
        means[b] = m;
    }

    /* Sort bootstrap means */
    stats_sort(means, n_resamples);

    /* Percentile CI (simple bootstrap, close to BCa for symmetric cases) */
    int lo_idx = (int)(0.025 * n_resamples);
    int hi_idx = (int)(0.975 * n_resamples);
    /* BCa acceleration correction (approximate) */
    double z0 = 0.0;
    int count_less = 0;
    for (int b = 0; b < n_resamples; b++) {
        if (means[b] < orig_mean) count_less++;
    }
    z0 = (count_less > 0 && count_less < n_resamples)
        ? 0.0 : 0.0; /* simplified; proper BCa needs jackknife */

    double z_alpha = 1.96;
    double a1 = (z0 + z_alpha) / (1 - 0.0 * (z0 + z_alpha));
    double a2 = (z0 - z_alpha) / (1 - 0.0 * (z0 - z_alpha));
    double p1 = 0.5 + 0.5 * erf(a1 / sqrt(2.0));
    double p2 = 0.5 + 0.5 * erf(a2 / sqrt(2.0));

    int li = (int)(p1 * n_resamples);
    int hi = (int)(p2 * n_resamples);
    if (li < 0) li = 0;
    if (li >= n_resamples) li = n_resamples - 1;
    if (hi < 0) hi = 0;
    if (hi >= n_resamples) hi = n_resamples - 1;

    *ci_lower = means[li];
    *ci_upper = means[hi];

    free(sample);
    free(means);
    return 0;
}

double stats_shapiro_wilk(const double *values, int count) {
    /* Simplified implementation for n <= 50 */
    if (count < 3 || count > 50) return 1.0;

    double *sorted = malloc(count * sizeof(double));
    memcpy(sorted, values, count * sizeof(double));
    stats_sort(sorted, count);

    double mean;
    stats_mean_stddev(values, count, &mean, &(double){0});

    /* Compute W statistic using Royston approximation */
    double ssq = 0.0;
    for (int i = 0; i < count; i++) ssq += (values[i] - mean) * (values[i] - mean);

    double b = 0.0;
    for (int k = 0; k < count / 2; k++) {
        /* Coefficients approximated */
        double w = 1.0 / sqrt((double)count);
        b += w * (sorted[count - 1 - k] - sorted[k]);
    }
    b = b * b;
    double W = (ssq > 0) ? b / ssq : 1.0;

    free(sorted);
    return W;
}

int stats_welch_ttest(const double *a, int na, const double *b, int nb,
        double *p_value, double *ci_lower, double *ci_upper) {
    double ma, mb, sa, sb;
    stats_mean_stddev(a, na, &ma, &sa);
    stats_mean_stddev(b, nb, &mb, &sb);

    double se = sqrt(sa*sa/na + sb*sb/nb);
    double df_num = (sa*sa/na + sb*sb/nb) * (sa*sa/na + sb*sb/nb);
    double df_den = (sa*sa/na)*(sa*sa/na)/(na-1) + (sb*sb/nb)*(sb*sb/nb)/(nb-1);
    double df = (df_den > 0) ? df_num / df_den : na + nb - 2;

    double t = (se > 0) ? (ma - mb) / se : 0.0;

    /* Approximate p-value from t-distribution */
    double x = df / (df + t * t);
    *p_value = 2.0 * (1.0 - 0.5 * (1.0 + erf(fabs(t) / sqrt(2.0))));
    if (*p_value > 1.0) *p_value = 1.0;
    if (*p_value < 0.0) *p_value = 0.0;

    /* CI for difference */
    double t_crit = 1.96; /* Normal approx for large df */
    *ci_lower = (ma - mb) - t_crit * se;
    *ci_upper = (ma - mb) + t_crit * se;

    return 0;
}

int stats_detect_outliers(const double *values, int count,
        double alpha, int max_outliers, bool *outlier_mask) {
    /* Generalized ESD test */
    if (count < 3) { memset(outlier_mask, 0, count * sizeof(bool)); return 0; }

    double *sorted = malloc(count * sizeof(double));
    int *indices = malloc(count * sizeof(int));
    memcpy(sorted, values, count * sizeof(double));
    for (int i = 0; i < count; i++) indices[i] = i;

    /* Simple sort with index tracking */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (sorted[i] > sorted[j]) {
                double td = sorted[i]; sorted[i] = sorted[j]; sorted[j] = td;
                int ti = indices[i]; indices[i] = indices[j]; indices[j] = ti;
            }
        }
    }

    memset(outlier_mask, 0, count * sizeof(bool));

    for (int k = 0; k < max_outliers && count - k > 2; k++) {
        double mean, stddev;
        stats_mean_stddev(sorted, count - k, &mean, &stddev);
        if (stddev < 1e-10) break;

        /* Find furthest point */
        double max_dev = 0.0;
        int max_idx = -1;
        for (int i = 0; i < count - k; i++) {
            double dev = fabs(sorted[i] - mean);
            if (dev > max_dev) { max_dev = dev; max_idx = i; }
        }

        /* Critical value (approximate t-distribution) */
        int r = count - k;
        double t = 3.0; /* Simplified; proper ESD uses lambda table */
        double lambda = t * (r - 1) / sqrt((double)r * (r - 2 + t*t));

        if (max_dev / stddev > lambda) {
            outlier_mask[indices[max_idx]] = true;
            /* Remove from working set */
            if (max_idx < count - k - 1) {
                memmove(&sorted[max_idx], &sorted[max_idx + 1],
                        (count - k - max_idx - 1) * sizeof(double));
                memmove(&indices[max_idx], &indices[max_idx + 1],
                        (count - k - max_idx - 1) * sizeof(int));
            }
        } else {
            break;
        }
    }

    free(sorted);
    free(indices);
    return 0;
}

void stats_benjamini_hochberg(double *p_values, int count, double fdr) {
    /* Sort p-values */
    double *sorted = malloc(count * sizeof(double));
    memcpy(sorted, p_values, count * sizeof(double));
    stats_sort(sorted, count);

    /* Find threshold */
    double threshold = 0.0;
    for (int i = 0; i < count; i++) {
        double bh_crit = fdr * (i + 1) / count;
        if (sorted[i] <= bh_crit) threshold = sorted[i];
    }

    /* Mark significant */
    for (int i = 0; i < count; i++) {
        p_values[i] = (p_values[i] <= threshold) ? p_values[i] : -1.0;
    }
    free(sorted);
}

int stats_mann_whitney(const double *a, int na, const double *b, int nb,
        double *p_value, double *hodges_lehmann) {
    /* Simplified Mann-Whitney U */
    int total = na + nb;
    double *combined = malloc(total * sizeof(double));
    int *group = malloc(total * sizeof(int));

    for (int i = 0; i < na; i++) { combined[i] = a[i]; group[i] = 0; }
    for (int i = 0; i < nb; i++) { combined[na+i] = b[i]; group[na+i] = 1; }

    /* Sort */
    for (int i = 0; i < total - 1; i++) {
        for (int j = i + 1; j < total; j++) {
            if (combined[i] > combined[j]) {
                double td = combined[i]; combined[i] = combined[j]; combined[j] = td;
                int ti = group[i]; group[i] = group[j]; group[j] = ti;
            }
        }
    }

    /* Compute U statistic */
    double r1 = 0.0;
    int rank = 1;
    for (int i = 0; i < total; i++) {
        if (group[i] == 0) r1 += rank;
        /* Handle ties simply */
        if (i < total - 1 && combined[i] < combined[i+1]) rank = i + 2;
    }

    double u1 = r1 - na * (na + 1) / 2.0;
    double u2 = na * nb - u1;
    double u = (u1 < u2) ? u1 : u2;

    /* Normal approximation p-value */
    double mu = na * nb / 2.0;
    double su = sqrt(na * nb * (na + nb + 1.0) / 12.0);
    double z = (su > 0) ? (u - mu) / su : 0.0;
    *p_value = 2.0 * (1.0 - 0.5 * (1.0 + erf(fabs(z) / sqrt(2.0))));
    if (*p_value > 1.0) *p_value = 1.0;

    /* Hodges-Lehmann: median of all pairwise differences */
    if (hodges_lehmann) {
        double *diffs = malloc(na * nb * sizeof(double));
        int dcount = 0;
        for (int i = 0; i < na; i++)
            for (int j = 0; j < nb; j++)
                diffs[dcount++] = a[i] - b[j];
        stats_sort(diffs, dcount);
        if (dcount % 2 == 0)
            *hodges_lehmann = (diffs[dcount/2-1] + diffs[dcount/2]) / 2.0;
        else
            *hodges_lehmann = diffs[dcount/2];
        free(diffs);
    }

    free(combined);
    free(group);
    return 0;
}
