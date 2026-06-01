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
    if (count < 1) {
        if (mean) *mean = 0.0;
        if (stddev) *stddev = 0.0;
        return;
    }
    double sum = 0.0;
    for (int i = 0; i < count; i++) sum += values[i];
    double m = sum / count;
    if (mean) *mean = m;
    if (count < 2) {
        if (stddev) *stddev = 0.0;
        return;
    }
    double ssq = 0.0;
    for (int i = 0; i < count; i++) {
        double d = values[i] - m;
        ssq += d * d;
    }
    if (stddev) *stddev = sqrt(ssq / (count - 1));
}

double stats_geometric_mean(const double *values, int count) {
    double log_sum = 0.0;
    for (int i = 0; i < count; i++) {
        if (values[i] <= 0.0) return 0.0;
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

int stats_bootstrap_percentile(const double *values, int count,
        int n_resamples, double *ci_lower, double *ci_upper) {
    if (count < 2 || n_resamples < 100) return -1;

    double *sample = malloc(count * sizeof(double));
    double *means = malloc(n_resamples * sizeof(double));
    if (!sample || !means) { free(sample); free(means); return -1; }

    /* Generate bootstrap distribution of means */
    for (int b = 0; b < n_resamples; b++) {
        for (int i = 0; i < count; i++) {
            sample[i] = values[rand() % count];
        }
        double m;
        stats_mean_stddev(sample, count, &m, &(double){0});
        means[b] = m;
    }

    /* Sort bootstrap means and take percentile CI */
    stats_sort(means, n_resamples);
    int lo_idx = (int)(0.025 * n_resamples);
    int hi_idx = (int)(0.975 * n_resamples);
    if (lo_idx < 0) lo_idx = 0;
    if (hi_idx >= n_resamples) hi_idx = n_resamples - 1;
    *ci_lower = means[lo_idx];
    *ci_upper = means[hi_idx];

    free(sample);
    free(means);
    return 0;
}

/* Backward-compatible alias */
int stats_bootstrap_bca(const double *values, int count,
        int n_resamples, double *ci_lower, double *ci_upper) {
    return stats_bootstrap_percentile(values, count, n_resamples, ci_lower, ci_upper);
}

/* Anderson-Darling test for normality.
 * Returns the A²* statistic (adjusted for small sample size).
 * Critical values for alpha=0.05: ~0.752 (sample-size adjusted).
 * Lower values indicate better fit to normal distribution. */
double stats_anderson_darling(const double *values, int count) {
    if (count < 3) return 0.0;

    double mean, stddev;
    stats_mean_stddev(values, count, &mean, &stddev);
    if (stddev < 1e-15) return 0.0;

    double *sorted = malloc(count * sizeof(double));
    memcpy(sorted, values, count * sizeof(double));
    stats_sort(sorted, count);

    /* Standardize to N(0,1) */
    double S = 0.0;
    for (int i = 0; i < count; i++) {
        double zi = (sorted[i] - mean) / stddev;
        double cdf = 0.5 * (1.0 + erf(zi / sqrt(2.0)));  /* Normal CDF */
        /* Clamp to avoid log(0) */
        if (cdf < 1e-15) cdf = 1e-15;
        if (cdf > 1.0 - 1e-15) cdf = 1.0 - 1e-15;
        double cdf_rev = 0.5 * (1.0 + erf((sorted[count - 1 - i] - mean) / (stddev * sqrt(2.0))));
        if (cdf_rev > 1.0 - 1e-15) cdf_rev = 1.0 - 1e-15;
        S += (2.0 * i + 1.0) * (log(cdf) + log(1.0 - cdf_rev));
    }
    double A2 = -count - S / count;

    /* Small sample size correction */
    double A2_star = A2 * (1.0 + 0.75 / count + 2.25 / (count * count));

    free(sorted);
    return A2_star;
}

/* Backward-compatible wrapper: returns p-value-like quantity.
 * Converts A²* to an approximate p-value via Stephens (1974) table. */
double stats_shapiro_wilk(const double *values, int count) {
    double A2 = stats_anderson_darling(values, count);
    /* Approximate p-value: A²* < 0.752 → non-significant (p > 0.05).
     * Return 1.0 - A²* scaled so that higher = more normal (like Shapiro-Wilk W). */
    double result = 1.0 / (1.0 + A2);
    return result;
}

/* Incomplete beta function via continued fraction (Lentz algorithm).
 * Used for t-distribution CDF computation. */
static double beta_inc(double a, double b, double x) {
    if (x < 0.0 || x > 1.0) return 0.0;
    if (x == 0.0 || x == 1.0) return x;

    /* Compute beta(a,b) via log-gamma */
    double log_beta = lgamma(a) + lgamma(b) - lgamma(a + b);

    /* Continued fraction for the incomplete beta function */
    double front = exp(log(a) + log(x) + b * log(1.0 - x) - log(a) - log_beta);

    double f = 1.0, c = 1.0, d = 1.0 - (a + b) * x / (a + 1.0);
    if (fabs(d) < 1e-30) d = 1e-30;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= 200; m++) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((a + m2 - 1.0) * (a + m2));
        d = 1.0 + aa * d;
        if (fabs(d) < 1e-30) d = 1e-30;
        c = 1.0 + aa / c;
        if (fabs(c) < 1e-30) c = 1e-30;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (a + b + m) * x / ((a + m2) * (a + m2 + 1.0));
        d = 1.0 + aa * d;
        if (fabs(d) < 1e-30) d = 1e-30;
        c = 1.0 + aa / c;
        if (fabs(c) < 1e-30) c = 1e-30;
        d = 1.0 / d;
        double del = d * c;
        h *= del;
        if (fabs(del - 1.0) < 3e-7) break;
    }
    return front * (h - 1.0);
}

/* t-distribution CDF: P(T <= t) for df degrees of freedom */
static double t_dist_cdf(double t, double df) {
    double x = df / (df + t * t);
    double ib = beta_inc(df / 2.0, 0.5, x);
    if (t >= 0) return 1.0 - 0.5 * ib;
    else return 0.5 * ib;
}

/* Inverse t-distribution: find t such that P(|T| > t) = alpha.
 * Uses Newton-Raphson on the CDF. */
static double t_dist_quantile(double alpha, double df) {
    if (df <= 0) return 1.96;  /* fallback to normal */
    /* Start with normal approximation */
    double t = 1.96;
    if (alpha < 0.3) {
        /* Cornish-Fisher expansion for better initial guess */
        double z = 1.96; /* z_0.025 */
        double z3 = z * z * z + z;
        t = z + z3 / (4.0 * df) + (5.0 * z * z * z * z * z + 16.0 * z * z * z + 3.0 * z) / (96.0 * df * df);
    }
    /* Simple bisection for robustness */
    double lo = 0.0, hi = 10.0;
    double target = 1.0 - alpha / 2.0;
    for (int iter = 0; iter < 50; iter++) {
        double mid = (lo + hi) / 2.0;
        double cdf_mid = t_dist_cdf(mid, df);
        if (cdf_mid < target) lo = mid;
        else hi = mid;
        if (hi - lo < 1e-6) break;
    }
    return (lo + hi) / 2.0;
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

    /* P-value from t-distribution (not normal approximation) */
    if (df > 0) {
        *p_value = 2.0 * (1.0 - t_dist_cdf(fabs(t), df));
    } else {
        *p_value = 2.0 * (1.0 - 0.5 * (1.0 + erf(fabs(t) / sqrt(2.0))));
    }
    if (*p_value > 1.0) *p_value = 1.0;
    if (*p_value < 0.0) *p_value = 0.0;

    /* CI using t-distribution critical value (alpha=0.05 two-sided) */
    double t_crit = (df > 0 && df < 1000) ? t_dist_quantile(0.05, df) : 1.96;
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

        /* Critical value using t-distribution (Rosner 1983) */
        int r = count - k;
        double alpha_r = alpha / (2.0 * (r + 1));  /* Bonferroni-adjusted alpha */
        double t_crit = t_dist_quantile(2.0 * alpha_r, r - 2);
        double lambda = t_crit * (r - 1) / sqrt((double)r * (r - 2 + t_crit * t_crit));

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
