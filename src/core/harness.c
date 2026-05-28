#include "servsysbench/servsysbench.h"
#include "servsysbench/benchmark.h"
#include "servsysbench/harness.h"
#include "servsysbench/stats.h"
#include <math.h>
#include "servsysbench/system.h"
#include "servsysbench/scoring.h"
#include "servsysbench/output.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* Per-instance state for multi-core parallel execution */
typedef struct {
    const benchmark_t *bench;
    int core_id;
    double values[SSB_MAX_ITERATIONS];
    int iter;
    int ret;
} instance_ctx_t;

static void *instance_runner(void *arg) {
    instance_ctx_t *ctx = (instance_ctx_t *)arg;

    /* Pin to specific core */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(ctx->core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    void *state = NULL;
    int ret = ctx->bench->init(&state);
    if (ret != 0) { ctx->ret = ret; return NULL; }

    /* Warmup */
    int warmup_count = ctx->bench->min_iterations >= 3 ? 3 : ctx->bench->min_iterations;
    for (int i = 0; i < warmup_count; i++) {
        ret = ctx->bench->warmup(state);
        if (ret != 0) { ctx->bench->cleanup(state); ctx->ret = ret; return NULL; }
    }

    /* Measurement loop */
    int max_iter = ctx->bench->max_iterations > 0 ? ctx->bench->max_iterations : SSB_MAX_ITERATIONS;
    int min_iter = ctx->bench->min_iterations > 0 ? ctx->bench->min_iterations : SSB_MIN_ITERATIONS;
    double target = ctx->bench->convergence_target > 0 ? ctx->bench->convergence_target : SSB_CONVERGENCE_TARGET;
    int min_runtime = ctx->bench->min_runtime_sec > 0 ? ctx->bench->min_runtime_sec : SSB_MIN_RUNTIME_SEC;
    int max_runtime = ctx->bench->max_runtime_sec > 0 ? ctx->bench->max_runtime_sec : SSB_MAX_RUNTIME_SEC;

    time_t wall_start = time(NULL);
    int iter = 0;

    while (iter < max_iter) {
        measurement_t m;
        ret = ctx->bench->measure(state, &m);
        if (ret != 0) { ctx->bench->cleanup(state); ctx->ret = ret; return NULL; }
        ctx->values[iter++] = m.primary_metric;

        time_t elapsed = time(NULL) - wall_start;
        if (iter >= min_iter) {
            double mean, stddev;
            stats_mean_stddev(ctx->values, iter, &mean, &stddev);
            double sem = stddev / sqrt((double)iter);
            double sem_rel = (mean != 0) ? sem / fabs(mean) : 1.0;
            if (sem_rel <= target && elapsed >= min_runtime) break;
            if (elapsed >= max_runtime) break;
        }
    }

    ctx->bench->cleanup(state);
    ctx->iter = iter;
    ctx->ret = 0;
    return NULL;
}

/* Aggregate multiple instances into a single benchmark_stats_t.
 * For throughput metrics (higher_is_better): sum across instances.
 * For latency metrics (lower_is_better): take the best (minimum) latency. */
static void aggregate_instances(const benchmark_t *bench, instance_ctx_t *instances,
                                 int n, benchmark_stats_t *out) {
    double all_values[SSB_MAX_ITERATIONS * 32];
    int total_iters = 0;

    if (bench->higher_is_better) {
        /* Throughput: sum per-instance throughput, then compute stats over time */
        /* Use the per-iteration values from the first instance as reference timing,
         * scaled by the sum ratio across all instances */
        double total_sum = 0.0;
        for (int i = 0; i < n; i++) {
            double mean = 0;
            for (int j = 0; j < instances[i].iter; j++)
                mean += instances[i].values[j];
            total_sum += mean / instances[i].iter;
        }
        /* Store the aggregated throughput */
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < instances[i].iter && total_iters < (int)(sizeof(all_values)/sizeof(all_values[0])); j++) {
                /* Scale each instance's values proportionally */
                double inst_mean = 0;
                for (int k = 0; k < instances[i].iter; k++)
                    inst_mean += instances[i].values[k];
                inst_mean /= instances[i].iter;
                all_values[total_iters++] = total_sum;
            }
        }
    } else {
        /* Latency: collect best (minimum) across all instances per "time slot" */
        int max_iter = 0;
        for (int i = 0; i < n; i++)
            if (instances[i].iter > max_iter) max_iter = instances[i].iter;

        for (int j = 0; j < max_iter && total_iters < (int)(sizeof(all_values)/sizeof(all_values[0])); j++) {
            double best = 1e18;
            for (int i = 0; i < n; i++) {
                if (j < instances[i].iter && instances[i].values[j] < best)
                    best = instances[i].values[j];
            }
            if (best < 1e18) all_values[total_iters++] = best;
        }
    }

    if (total_iters == 0) {
        memset(out, 0, sizeof(*out));
        return;
    }

    stats_compute(all_values, total_iters, out);
}

int harness_run_single(const benchmark_t *bench, run_mode_t mode,
        benchmark_stats_t *stats) {
    instance_ctx_t ctx = {.bench = bench, .core_id = 0};
    instance_runner(&ctx);
    if (ctx.ret != 0) return ctx.ret;
    stats_compute(ctx.values, ctx.iter, stats);
    return 0;
}

/* Run N independent instances in parallel, one pinned per core.
 * For throughput benchmarks, the aggregated score = sum of per-instance throughput.
 * For latency benchmarks, the aggregated score = best (minimum) latency. */
static int harness_run_parallel(const benchmark_t *bench, run_mode_t mode,
                                 int num_instances, benchmark_stats_t *stats) {
    instance_ctx_t *ctxs = calloc(num_instances, sizeof(instance_ctx_t));
    pthread_t *threads = malloc(num_instances * sizeof(pthread_t));

    for (int i = 0; i < num_instances; i++) {
        ctxs[i].bench = bench;
        ctxs[i].core_id = i;
        pthread_create(&threads[i], NULL, instance_runner, &ctxs[i]);
    }

    int any_failed = 0;
    for (int i = 0; i < num_instances; i++) {
        pthread_join(threads[i], NULL);
        if (ctxs[i].ret != 0) any_failed = 1;
    }

    if (!any_failed)
        aggregate_instances(bench, ctxs, num_instances, stats);

    free(ctxs);
    free(threads);
    return any_failed ? -1 : 0;
}

int harness_run(const run_config_t *config, run_result_t **result_out) {
    run_result_t *result = calloc(1, sizeof(run_result_t));
    if (!result) return -1;

    output_generate_uuid(result->run_id);
    result->config = *config;
    result->start_time = time(NULL);

    system_probe(&result->sysinfo);

    const benchmark_t **benchmarks;
    int bench_count;
    benchmark_get_all(&benchmarks, &bench_count);

    result->subtest_count = 0;
    result->subtests = calloc(bench_count, sizeof(subtest_result_t));

    int n = config->num_instances > 0 ? config->num_instances : 1;

    for (int i = 0; i < bench_count; i++) {
        const benchmark_t *b = benchmarks[i];

        if (!(config->tier_mask & (1 << b->tier))) continue;
        if (config->category_filter &&
            strcmp(b->category, config->category_filter) != 0) continue;

        subtest_result_t *sr = &result->subtests[result->subtest_count];
        sr->bench = b;

        int ret;
        if (n > 1 && b->num_threads == 1)
            ret = harness_run_parallel(b, config->mode, n, &sr->stats);
        else
            ret = harness_run_single(b, config->mode, &sr->stats);

        sr->status = (ret == 0) ? "completed" : "failed";
        result->subtest_count++;
    }

    result->end_time = time(NULL);
    *result_out = result;
    return 0;
}

void harness_free_result(run_result_t *result) {
    if (!result) return;
    system_free(result->sysinfo);
    free(result->subtests);
    free(result);
}
