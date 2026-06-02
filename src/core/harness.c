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
#include <signal.h>
#include <stdbool.h>
#include <sys/syscall.h>

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

    /* Benchmark-defined warmup: lightweight cache/TLB priming (e.g., partial data).
     * Not all benchmarks define a warmup function; skip if NULL. */
    if (bench->warmup) {
        ret = bench->warmup(state);
        if (ret != 0) { bench->cleanup(state); return ret; }
    }

    /* Warmup: execute full measure() 2x and discard results.
     * This properly warms caches, TLB, and branch predictors at full scale. */
    for (int i = 0; i < 2; i++) {
        measurement_t wm;
        ret = bench->measure(state, &wm);
        if (ret != 0) { bench->cleanup(state); return ret; }
    }

    int max_iter = bench->max_iterations > 0 ? bench->max_iterations : SSB_MAX_ITERATIONS;
    int min_iter = bench->min_iterations > 0 ? bench->min_iterations : SSB_MIN_ITERATIONS;
    double target = bench->convergence_target > 0 ? bench->convergence_target : SSB_CONVERGENCE_TARGET;
    int min_runtime = bench->min_runtime_sec > 0 ? bench->min_runtime_sec : SSB_MIN_RUNTIME_SEC;
    int max_runtime = bench->max_runtime_sec > 0 ? bench->max_runtime_sec : SSB_MAX_RUNTIME_SEC;

    struct timespec wall_start, cpu_start;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_start);
    int iter = 0;

    while (iter < max_iter) {
        measurement_t m;
        ret = bench->measure(state, &m);
        if (ret != 0) { bench->cleanup(state); return ret; }
        values_out[iter++] = m.primary_metric;

        struct timespec wall_now;
        clock_gettime(CLOCK_MONOTONIC, &wall_now);
        double wall_elapsed = (wall_now.tv_sec - wall_start.tv_sec)
            + (wall_now.tv_nsec - wall_start.tv_nsec) / 1e9;
        if (iter >= min_iter) {
            double mean, stddev;
            stats_mean_stddev(values_out, iter, &mean, &stddev);
            double sem = stddev / sqrt((double)iter);
            double sem_rel = (mean != 0) ? sem / fabs(mean) : 1.0;
            if (sem_rel <= target && wall_elapsed >= min_runtime) break;
        }
        /* Max runtime: use wall clock as primary guard (preempted CPU time skews) */
        if (wall_elapsed >= max_runtime) break;
        /* Also check CPU time to prevent infinite loops on preempted systems */
        struct timespec cpu_now;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_now);
        double cpu_elapsed = (cpu_now.tv_sec - cpu_start.tv_sec)
            + (cpu_now.tv_nsec - cpu_start.tv_nsec) / 1e9;
        if (cpu_elapsed > max_runtime * 2) break;
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


/* Read thread siblings to find physical core mapping.
 * Returns an array mapping logical CPU -> physical core ID,
 * or NULL on failure (caller should fall back to sequential pinning). */
static int *harness_get_physical_cores(int *num_physical) {
    int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 0) return NULL;
    int *phys_map = calloc(ncpu, sizeof(int));
    if (!phys_map) return NULL;
    
    int phys_id = 0;
    bool *seen = calloc(ncpu, sizeof(bool));
    if (!seen) { free(phys_map); return NULL; }
    
    for (int cpu = 0; cpu < ncpu; cpu++) {
        if (seen[cpu]) continue;
        /* Mark this CPU and its HT siblings as same physical core */
        phys_map[cpu] = phys_id;
        seen[cpu] = true;
        
        char path[256];
        snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
        FILE *f = fopen(path, "r");
        if (f) {
            char line[256];
            if (fgets(line, sizeof(line), f)) {
                /* Parse comma-separated ranges, e.g. "0,16" or "0-1" */
                char *tok = strtok(line, ",");
                while (tok) {
                    int start, end;
                    if (sscanf(tok, "%d-%d", &start, &end) == 2) {
                        for (int s = start; s <= end && s < ncpu; s++)
                            if (s != cpu) { phys_map[s] = phys_id; seen[s] = true; }
                    } else if (sscanf(tok, "%d", &start) == 1) {
                        if (start < ncpu && start != cpu) {
                            phys_map[start] = phys_id; seen[start] = true;
                        }
                    }
                    tok = strtok(NULL, ",");
                }
            }
            fclose(f);
        }
        phys_id++;
    }
    free(seen);
    *num_physical = phys_id;
    return phys_map;
}

/* Run N independent processes in parallel, one pinned per core.
 * Each child runs the full benchmark lifecycle and sends results back via pipe. */
static int harness_run_parallel(const benchmark_t *bench, run_mode_t mode,
                                 int num_instances, benchmark_stats_t *stats) {
    int (*pipes)[2] = malloc(num_instances * sizeof(int[2]));
    pid_t *pids = malloc(num_instances * sizeof(pid_t));
    if (!pipes || !pids) {
        free(pipes); free(pids);
        return -1;
    }

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

            /* Pin to specific physical core (skip HT siblings) */
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            /* Use physical core mapping if available, else fall back to sequential */
            int pin_cpu = i;
            if (i == 0) {
                /* Discover physical cores once (cached in static for first call) */
                static int *phys_map = NULL;
                static int num_phys = 0;
                if (!phys_map) {
                    phys_map = harness_get_physical_cores(&num_phys);
                }
                if (phys_map && num_phys > 0) {
                    /* Map instance i to physical core i, skipping HT siblings */
                    int found = 0;
                    for (int c = 0; c < (int)sysconf(_SC_NPROCESSORS_ONLN) && found <= i; c++) {
                        if (phys_map[c] == i) { pin_cpu = c; break; }
                    }
                    /* If i >= num_phys, wrap to physical core (i % num_phys) */
                    if (i >= num_phys) {
                        for (int c = 0; c < (int)sysconf(_SC_NPROCESSORS_ONLN); c++) {
                            if (phys_map[c] == (i % num_phys)) { pin_cpu = c; break; }
                        }
                    }
                }
            }
            CPU_SET(pin_cpu, &cpuset);
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
    if (!results) {
        /* Kill children on allocation failure */
        for (int i = 0; i < num_instances; i++) {
            close(pipes[i][0]);
            kill(pids[i], SIGTERM);
            waitpid(pids[i], NULL, 0);
        }
        free(pipes); free(pids);
        return -1;
    }
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
        /* Aggregate: compute per-instance means, then pool as independent samples.
         * For throughput: report sum of per-instance means as the aggregate metric,
         *   but store each instance's mean as an independent statistical sample.
         * For latency: pool all iterations from all instances as independent samples. */
        double all_values[SSB_MAX_ITERATIONS * 32];
        int total_iters = 0;

        if (bench->higher_is_better) {
            /* Throughput: per-instance means as independent samples */
            for (int i = 0; i < num_instances; i++) {
                if (results[i].iter > 0) {
                    double inst_mean = 0;
                    for (int j = 0; j < results[i].iter; j++)
                        inst_mean += results[i].values[j];
                    inst_mean /= results[i].iter;
                    if (total_iters < (int)(sizeof(all_values)/sizeof(all_values[0])))
                        all_values[total_iters++] = inst_mean;
                }
            }
        } else {
            /* Latency: pool all iterations as independent samples */
            for (int i = 0; i < num_instances; i++) {
                for (int j = 0; j < results[i].iter &&
                     total_iters < (int)(sizeof(all_values)/sizeof(all_values[0])); j++) {
                    all_values[total_iters++] = results[i].values[j];
                }
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
    if (!result->subtests && bench_count > 0) {
        free(result);
        return -1;
    }

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

        /* Cooldown between benchmarks in peak mode */
        if (config->mode == SSB_MODE_PEAK && b->cooldown_required) {
            int cooldown = SSB_COOLDOWN_SEC;
            if (cooldown > 0) {
                sleep(cooldown);
            }
        }
    }

    /* Assign normalized scores: use raw mean as self-normalized score.
     * When a reference baseline file is available, scoring_normalize()
     * should be called instead. */
    for (int i = 0; i < result->subtest_count; i++) {
        subtest_result_t *sr = &result->subtests[i];
        sr->normalized_score = sr->stats.mean;
        /* Invert latency metrics so higher is always better for scoring */
        if (sr->bench && !sr->bench->higher_is_better && sr->stats.mean > 0) {
            sr->normalized_score = 1.0 / sr->stats.mean;
        }
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
