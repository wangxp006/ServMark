#include "servmark.h"
#include "benchmark.h"
#include "harness.h"
#include "stats.h"
#include <math.h>
#include "system.h"
#include "scoring.h"
#include "output.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>

/* Child-parent pipe protocol: child writes result struct after benchmark run */
typedef struct {
    int ret;
    int iter;
    double values[SSB_MAX_ITERATIONS];
} child_result_t;

/* Run benchmark in current process — used by both single and fork modes */
static int run_benchmark_instance(const benchmark_t *bench, double *values_out, int *iter_out) {
    void *state = NULL;
    int ret = bench->init(&state);
    if (ret != 0) return ret;

    int warmup_count = bench->min_iterations >= 3 ? 3 : bench->min_iterations;
    for (int i = 0; i < warmup_count; i++) {
        ret = bench->warmup(state);
        if (ret != 0) { bench->cleanup(state); return ret; }
    }

    int max_iter = bench->max_iterations > 0 ? bench->max_iterations : SSB_MAX_ITERATIONS;
    int min_iter = bench->min_iterations > 0 ? bench->min_iterations : SSB_MIN_ITERATIONS;
    double target = bench->convergence_target > 0 ? bench->convergence_target : SSB_CONVERGENCE_TARGET;
    int min_runtime = bench->min_runtime_sec > 0 ? bench->min_runtime_sec : SSB_MIN_RUNTIME_SEC;
    int max_runtime = bench->max_runtime_sec > 0 ? bench->max_runtime_sec : SSB_MAX_RUNTIME_SEC;

    time_t wall_start = time(NULL);
    int iter = 0;

    while (iter < max_iter) {
        measurement_t m;
        ret = bench->measure(state, &m);
        if (ret != 0) { bench->cleanup(state); return ret; }
        values_out[iter++] = m.primary_metric;

        time_t elapsed = time(NULL) - wall_start;
        if (iter >= min_iter) {
            double mean, stddev;
            stats_mean_stddev(values_out, iter, &mean, &stddev);
            double sem = stddev / sqrt((double)iter);
            double sem_rel = (mean != 0) ? sem / fabs(mean) : 1.0;
            if (sem_rel <= target && elapsed >= min_runtime) break;
            if (elapsed >= max_runtime) break;
        }
    }

    bench->cleanup(state);
    *iter_out = iter;
    return 0;
}

int harness_run_single(const benchmark_t *bench, run_mode_t mode,
        benchmark_stats_t *stats) {
    double values[SSB_MAX_ITERATIONS];
    int iter;
    int ret = run_benchmark_instance(bench, values, &iter);
    if (ret != 0) return ret;
    stats_compute(values, iter, stats);
    return 0;
}

/* Run N independent processes in parallel, one pinned per core.
 * Each child runs the full benchmark lifecycle and sends results back via pipe. */
static int harness_run_parallel(const benchmark_t *bench, run_mode_t mode,
                                 int num_instances, benchmark_stats_t *stats) {
    int (*pipes)[2] = malloc(num_instances * sizeof(int[2]));
    pid_t *pids = malloc(num_instances * sizeof(pid_t));

    for (int i = 0; i < num_instances; i++) {
        if (pipe(pipes[i]) != 0) {
            /* Cleanup previously created pipes */
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]); close(pipes[j][1]);
            }
            free(pipes); free(pids);
            return -1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            /* Child process */
            close(pipes[i][0]); /* close read end */

            /* Pin to specific core */
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(i, &cpuset);
            sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

            child_result_t result = { .ret = -1, .iter = 0 };
            result.ret = run_benchmark_instance(bench, result.values, &result.iter);

            /* Send results to parent via pipe */
            write(pipes[i][1], &result, sizeof(result));
            close(pipes[i][1]);
            _exit(result.ret == 0 ? 0 : 1);
        }

        /* Parent: close write end */
        close(pipes[i][1]);
        pids[i] = pid;
    }

    /* Parent: wait for all children and collect results */
    child_result_t *results = calloc(num_instances, sizeof(child_result_t));
    int any_failed = 0;

    for (int i = 0; i < num_instances; i++) {
        int wstatus;
        waitpid(pids[i], &wstatus, 0);

        /* Read child result from pipe */
        ssize_t n = read(pipes[i][0], &results[i], sizeof(child_result_t));
        close(pipes[i][0]);

        if (n != sizeof(child_result_t) || results[i].ret != 0 || !WIFEXITED(wstatus))
            any_failed = 1;
    }

    if (!any_failed) {
        /* Aggregate: for throughput, sum across processes.
         * For latency, take the best (minimum) across processes. */
        double all_values[SSB_MAX_ITERATIONS * 32];
        int total_iters = 0;

        if (bench->higher_is_better) {
            double total_sum = 0.0;
            for (int i = 0; i < num_instances; i++) {
                double inst_mean = 0;
                for (int j = 0; j < results[i].iter; j++)
                    inst_mean += results[i].values[j];
                total_sum += inst_mean / results[i].iter;
            }
            for (int i = 0; i < num_instances; i++) {
                for (int j = 0; j < results[i].iter &&
                     total_iters < (int)(sizeof(all_values)/sizeof(all_values[0])); j++)
                    all_values[total_iters++] = total_sum;
            }
        } else {
            int max_iter = 0;
            for (int i = 0; i < num_instances; i++)
                if (results[i].iter > max_iter) max_iter = results[i].iter;
            for (int j = 0; j < max_iter &&
                 total_iters < (int)(sizeof(all_values)/sizeof(all_values[0])); j++) {
                double best = 1e18;
                for (int i = 0; i < num_instances; i++) {
                    if (j < results[i].iter && results[i].values[j] < best)
                        best = results[i].values[j];
                }
                if (best < 1e18) all_values[total_iters++] = best;
            }
        }

        if (total_iters > 0)
            stats_compute(all_values, total_iters, stats);
        else
            memset(stats, 0, sizeof(*stats));
    }

    free(results);
    free(pipes);
    free(pids);
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

        if (config->bench_filter_count > 0) {
            bool found = false;
            for (int j = 0; j < config->bench_filter_count; j++) {
                if (strcmp(b->name, config->bench_filter[j]) == 0) {
                    found = true; break;
                }
            }
            if (!found) continue;
        }

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
