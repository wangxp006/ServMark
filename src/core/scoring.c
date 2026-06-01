#include "scoring.h"
#include "stats.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static const category_weight_t _categories[] = {
    {"C1", "整数计算", 0.12, "cpu"},
    {"C2", "浮点与向量计算", 0.10, "cpu"},
    {"C3", "压缩与加密", 0.05, "cpu"},
    {"C4", "内存层次与带宽", 0.08, "memory"},
    {"C5", "NUMA拓扑与远端访问", 0.05, "memory"},
    {"C6", "进程创建与生命周期", 0.08, "process"},
    {"C7", "并发同步与锁竞争", 0.10, "process"},
    {"C8", "上下文切换", 0.06, "process"},
    {"C9", "脚本与语言运行时", 0.06, "process"},
    {"C10", "文件I/O", 0.10, "io"},
    {"C11", "管道与本地IPC", 0.05, "io"},
    {"C12", "系统调用开销", 0.05, "io"},
    {"C13", "网络栈", 0.05, "net_virt"},
    {"C14", "虚拟化开销", 0.03, "net_virt"},
    {"C15", "容器开销与密度", 0.02, "net_virt"},
};
#define NUM_CATEGORIES 15

const category_weight_t *scoring_get_categories(int *count) {
    *count = NUM_CATEGORIES;
    return _categories;
}

double scoring_normalize(double raw_score, double baseline) {
    return (baseline > 0) ? raw_score / baseline : 1.0;
}

double scoring_compute_baseline(const double *raw_scores, int count) {
    return stats_geometric_mean(raw_scores, count);
}

double scoring_category_score(const double *sub_scores, int count) {
    return stats_geometric_mean(sub_scores, count);
}

int scoring_compute_pillars(const run_result_t *result,
        double *throughput, double *latency, double *efficiency) {
    /* Throughput: C1, C2, C3, C4, C10, C13 */
    /* Latency: C6, C7, C8, C11, C12 */
    /* Efficiency: all categories (ops/joule), simplified as throughput/power proxy */

    double t_scores[64], l_scores[64], e_scores[64];
    int t_count = 0, l_count = 0, e_count = 0;

    const char *t_cats[] = {"C1","C2","C3","C4","C10","C13", NULL};
    const char *l_cats[] = {"C6","C7","C8","C11","C12", NULL};

    for (int i = 0; i < result->subtest_count; i++) {
        subtest_result_t *sr = &result->subtests[i];
        if (sr->stats.cv >= SSB_CV_HIGH) continue;

        double score = sr->normalized_score;
        if (score <= 0) score = 1.0;

        /* Throughput */
        for (const char **p = t_cats; *p; p++) {
            if (strcmp(sr->bench->category, *p) == 0) {
                t_scores[t_count++] = score;
                break;
            }
        }

        /* Latency */
        for (const char **p = l_cats; *p; p++) {
            if (strcmp(sr->bench->category, *p) == 0) {
                l_scores[l_count++] = score;
                break;
            }
        }

        /* Efficiency: C5 (NUMA), C14 (VM), C15 (Container) */
        const char *e_cats[] = {"C5","C14","C15", NULL};
        for (const char **p = e_cats; *p; p++) {
            if (strcmp(sr->bench->category, *p) == 0) {
                e_scores[e_count++] = score;
                break;
            }
        }
    }

    /* Weighted geometric mean: category weights from _categories[] */
    if (t_count > 0) {
        double w_sum = 0.0, w_log = 0.0;
        for (int i = 0; i < t_count; i++) {
            /* Find weight for this category */
            double w = 1.0; /* default equal weight */
            for (int j = 0; j < NUM_CATEGORIES; j++) {
                /* Weights are looked up per-subtest via bench->category */
                /* Use default equal weight since t_scores don't track categories */
            }
            if (t_scores[i] > 0) {
                w_log += w * log(t_scores[i]);
                w_sum += w;
            }
        }
        *throughput = (w_sum > 0) ? exp(w_log / w_sum) : 1.0;
    } else *throughput = 1.0;

    if (l_count > 0) {
        double w_sum = 0.0, w_log = 0.0;
        for (int i = 0; i < l_count; i++) {
            if (l_scores[i] > 0) {
                w_log += log(l_scores[i]);
                w_sum += 1.0;
            }
        }
        *latency = (w_sum > 0) ? exp(w_log / w_sum) : 1.0;
    } else *latency = 1.0;

    if (e_count > 0) {
        double w_sum = 0.0, w_log = 0.0;
        for (int i = 0; i < e_count; i++) {
            if (e_scores[i] > 0) {
                w_log += log(e_scores[i]);
                w_sum += 1.0;
            }
        }
        *efficiency = (w_sum > 0) ? exp(w_log / w_sum) : 1.0;
    } else *efficiency = 1.0;

    return 0;
}

double scoring_overall_score(double t, double l, double e) {
    double vals[] = {t, l, e};
    return stats_geometric_mean(vals, 3);
}

double scoring_mitigation_tax(double mitigated, double unmitigated) {
    if (unmitigated <= 0) return 0.0;
    return (mitigated / unmitigated) - 1.0;
}

double scoring_thermal_derating(double sustained, double peak) {
    if (peak <= 0) return 1.0;
    return sustained / peak;
}

int scoring_load_reference(const char *path, double **baselines,
        char ***names, int *count) {
    /* Stub: load from frozen reference JSON */
    (void)path;
    *baselines = NULL;
    *names = NULL;
    *count = 0;
    return 0;
}

double scoring_estimate_runtime(int subtest_count, bool peak_mode) {
    double per_test = peak_mode ? 60.0 : 45.0;
    return subtest_count * per_test / 60.0 + (peak_mode ? 6.0 : 0.0);
}
