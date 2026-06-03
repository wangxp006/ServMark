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
#ifdef __linux__
#include <numaif.h>   /* mbind() for NUMA memory policy */
#endif

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
        benchmark_stats_t *stats, const run_config_t *overrides) {
    (void)mode;
    double values[SSB_MAX_ITERATIONS];
    int iter;
    int ret = run_benchmark_instance(bench, values, &iter);

    /* Apply config overrides: clamp iterations to configured min/max */
    if (ret == 0 && overrides) {
        if (overrides->max_iterations > 0 && iter > overrides->max_iterations)
            iter = overrides->max_iterations;
        if (overrides->min_iterations > 0 && iter < overrides->min_iterations)
            iter = overrides->min_iterations;
    }

    if (ret != 0) return ret;
    stats_compute(values, iter, stats);
    return 0;
}


/* Forward declaration */
static int *harness_get_physical_cores(int *num_physical);

/* Parse a CPU pin spec string into an array of CPU IDs.
 * Formats: "auto" -> list=NULL, count=0 (auto-detect)
 *          "0,2,4,6" -> [0,2,4,6], "0-7" -> [0..7]
 *          "0-3,8-11" -> [0,1,2,3,8,9,10,11]
 * Returns malloc'd array, caller must free. */
static int *harness_parse_cpu_spec(const char *spec, int *count_out) {
    if (!spec || !spec[0]) { *count_out = 0; return NULL; }
    int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);

    /* Auto modes: return NULL with count = mode flag (negative) */
    if (strcmp(spec, "auto") == 0) {
        *count_out = 0; return NULL;           /* SMT-aware physical cores */
    }
    if (strcmp(spec, "auto-all") == 0) {
        *count_out = -1;                       /* all logical CPUs */
        int *list = calloc(ncpu, sizeof(int));
        if (list) { for (int i = 0; i < ncpu; i++) list[i] = i; }
        *count_out = ncpu;
        return list;
    }
    if (strcmp(spec, "auto-numa") == 0) {
        *count_out = -2;                       /* one CPU per NUMA node */
        /* Fall through: handled in harness_run_parallel via system_info */
        int *list = harness_get_physical_cores(&ncpu);
        *count_out = (list && ncpu > 0) ? ncpu : 0;
        return list;
    }

    /* Explicit CPU list parsing */
    int *list = calloc(512, sizeof(int));
    if (!list) { *count_out = 0; return NULL; }
    int count = 0;
    char *buf = strdup(spec);
    if (!buf) { free(list); *count_out = 0; return NULL; }
    char *tok = strtok(buf, ",");
    while (tok && count < 512) {
        while (*tok == ' ' || *tok == '\t') tok++;
        int start, end;
        if (sscanf(tok, "%d-%d", &start, &end) == 2) {
            for (int c = start; c <= end && c < ncpu && count < 512; c++)
                list[count++] = c;
        } else if (sscanf(tok, "%d", &start) == 1) {
            if (start >= 0 && start < ncpu) list[count++] = start;
        }
        tok = strtok(NULL, ",");
    }
    free(buf);
    *count_out = count;
    return list;
}

/* Parse per-CPU NUMA binding spec. Format: "cpu_range:nid,..."
 * Examples: "18:0,19:0,20:1"  "0-15:0,16-31:1"
 * Fills cpu_to_numa[] where cpu_to_numa[cpu_id] = node_id (-1 = unset).
 * Returns nums of bound CPUs, or 0 if spec is NULL/empty. */
static int harness_parse_numa_bind(const char *spec, int *cpu_to_numa, int max_cpu) {
    if (!spec || !spec[0]) return 0;
    for (int i = 0; i < max_cpu; i++) cpu_to_numa[i] = -1;
    int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int bound = 0;
    char *buf = NULL;

    /* Shorthand: single integer "N" = auto-split N nodes evenly */
    char *endp = NULL;
    long nnodes = strtol(spec, &endp, 10);
    if (endp && *endp == '\0' && nnodes > 0 && nnodes <= 16) {
        int cpus_per = (ncpu + (int)nnodes - 1) / (int)nnodes;
        for (int i = 0; i < ncpu && i < max_cpu; i++) {
            cpu_to_numa[i] = i / cpus_per;
            if (cpu_to_numa[i] >= (int)nnodes) cpu_to_numa[i] = (int)nnodes - 1;
            bound++;
        }
        return bound;
    }

    /* Load from file: "@path/to/topo.txt" */
    if (spec[0] == '@') {
        FILE *f = fopen(spec + 1, "r");
        if (!f) { fprintf(stderr, "[numa-topo] cannot open '%s'\n", spec + 1); return 0; }
        char line[4096]; char *combined = NULL; size_t clen = 0;
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == '\0') continue;
            size_t plen = strlen(p);
            while (plen > 0 && (p[plen-1] == '\n' || p[plen-1] == '\r')) plen--;
            char *add = realloc(combined, clen + plen + 2);
            if (!add) { free(combined); fclose(f); return 0; }
            combined = add;
            if (clen > 0) combined[clen++] = ',';
            memcpy(combined + clen, p, plen);
            clen += plen; combined[clen] = '\0';
        }
        fclose(f);
        if (!combined) return 0;
        buf = strdup(combined); free(combined);
        if (!buf) return 0;
        /* Fall through to parsing below using the combined string */
        char *save = NULL;
        char *tok = strtok_r(buf, ",", &save);
        while (tok) {
            while (*tok == ' ' || *tok == '\t') tok++;
            char *colon = strchr(tok, ':');
            if (colon) {
                *colon = '\0';
                int nid = atoi(colon + 1);
                int start, end;
                if (sscanf(tok, "%d-%d", &start, &end) == 2) {
                    for (int c = start; c <= end && c < ncpu && c < max_cpu; c++)
                    { cpu_to_numa[c] = nid; bound++; }
                } else if (sscanf(tok, "%d", &start) == 1) {
                    if (start >= 0 && start < ncpu && start < max_cpu)
                    { cpu_to_numa[start] = nid; bound++; }
                }
            }
            tok = strtok_r(NULL, ",", &save);
        }
        free(buf);
        return bound;
    }

    /* Inline format: "18:0,19:0,0-15:1" */
    buf = strdup(spec);
    if (!buf) return 0;
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        /* Split by colon: "18:0" or "0-15:0" */
        char *colon = strchr(tok, ':');
        if (colon) {
            *colon = '\0';
            char *cpu_part = tok;
            char *node_part = colon + 1;
            int nid = atoi(node_part);
            /* Parse CPU part: single id or range */
            int start, end;
            if (sscanf(cpu_part, "%d-%d", &start, &end) == 2) {
                for (int c = start; c <= end && c < ncpu && c < max_cpu; c++) {
                    cpu_to_numa[c] = nid; bound++;
                }
            } else if (sscanf(cpu_part, "%d", &start) == 1) {
                if (start >= 0 && start < ncpu && start < max_cpu) {
                    cpu_to_numa[start] = nid; bound++;
                }
            }
        }
        tok = strtok_r(NULL, ",", &save);
    }
    free(buf);
    return bound;
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
                                 int num_instances, benchmark_stats_t *stats,
                                 const run_config_t *config) {
    int (*pipes)[2] = malloc(num_instances * sizeof(int[2]));
    pid_t *pids = malloc(num_instances * sizeof(pid_t));
    if (!pipes || !pids) {
        free(pipes); free(pids);
        return -1;
    }

    /* One-time init: auto-detect physical cores + parse manual CPU/NUMA config.
     * Static so children inherit via fork()'s copy of the data segment. */
    static int *s_phys_map = NULL;
    static int s_num_phys = 0;
    static int *s_manual_pins = NULL;
    static int s_manual_pin_count = 0;
    static int s_cpu_to_numa[512];          /* per-CPU NUMA node (-1=unset) */
    static int s_membind_node = -1;         /* explicit mbind node, -1=local */
    static bool s_membind_interleave = false;

    if (!s_phys_map && !s_manual_pins) {
        s_phys_map = harness_get_physical_cores(&s_num_phys);

        /* Manual CPU pinning */
        if (config && config->cpu_pin_spec) {
            s_manual_pins = harness_parse_cpu_spec(config->cpu_pin_spec,
                                                    &s_manual_pin_count);
            if (s_manual_pin_count > 0)
                fprintf(stderr, "[cpu-pin] manual: %d CPUs\n", s_manual_pin_count);
        }

        /* NUMA topology override */
        int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
        for (int i = 0; i < ncpu && i < 512; i++) s_cpu_to_numa[i] = -1;
        if (config && config->numa_topo_spec && config->numa_topo_spec[0]) {
            int nbound = harness_parse_numa_bind(config->numa_topo_spec,
                                                  s_cpu_to_numa, ncpu);
            if (nbound > 0)
                fprintf(stderr, "[numa-topo] override: %d CPUs mapped\n", nbound);
        }

        /* Memory binding policy */
        s_membind_node = -1;
        s_membind_interleave = false;
        if (config && config->membind_spec && config->membind_spec[0]) {
            if (strcmp(config->membind_spec, "local") == 0) {
                s_membind_node = -1; /* mbind to CPU's local node */
            } else if (strcmp(config->membind_spec, "interleave") == 0) {
                s_membind_interleave = true;
            } else {
                s_membind_node = atoi(config->membind_spec);
                fprintf(stderr, "[membind] policy: node %d\n", s_membind_node);
            }
        }

        /* Validation: warn if pinned CPUs have no NUMA assignment */
        if (s_manual_pins && s_manual_pin_count > 0 && s_cpu_to_numa[0] != -1) {
            int missing = 0;
            for (int i = 0; i < s_manual_pin_count; i++) {
                int cpu = s_manual_pins[i];
                if (cpu < ncpu && s_cpu_to_numa[cpu] < 0) missing++;
            }
            if (missing > 0)
                fprintf(stderr, "[warn] %d pinned CPUs have no NUMA node assigned"
                        " (use --numa-topo)\n", missing);
        }
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

            /* Pin to specific CPU core.
             * Priority: 1) manual --cpu-pin spec  2) SMT-aware auto  3) sequential
             * s_* statics are set by parent before fork and inherited by children. */
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            int pin_cpu = i;

            if (s_manual_pins && s_manual_pin_count > 0) {
                pin_cpu = s_manual_pins[i % s_manual_pin_count];
            } else if (s_phys_map && s_num_phys > 0) {
                int phys_id = i % s_num_phys;
                for (int c = 0; c < (int)sysconf(_SC_NPROCESSORS_ONLN); c++) {
                    if (s_phys_map[c] == phys_id) { pin_cpu = c; break; }
                }
            }
            CPU_SET(pin_cpu, &cpuset);
            sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

            /* NUMA memory policy: apply mbind based on membind_spec or numa_topo */
            {
                int nid = -1;
                int node_count = 0;
                unsigned long nodemask = 0;
                int mode = MPOL_DEFAULT;

                if (s_membind_interleave) {
                    mode = MPOL_INTERLEAVE;
                    /* Use all nodes from numa_topo or detected */
                    int nc = (int)sysconf(_SC_NPROCESSORS_ONLN);
                    for (int i = 0; i < nc; i++) {
                        int nn = (s_cpu_to_numa[i] >= 0) ? s_cpu_to_numa[i] : -1;
                        if (nn >= 0) nodemask |= (1UL << nn);
                    }
                    if (nodemask == 0) nodemask = ~0UL; /* all nodes */
                } else if (s_membind_node >= 0) {
                    nid = s_membind_node;
                    mode = MPOL_BIND;
                    nodemask = 1UL << nid;
                } else if (s_cpu_to_numa[pin_cpu] >= 0) {
                    /* --numa-topo set: bind memory to this CPU's NUMA node */
                    nid = s_cpu_to_numa[pin_cpu];
                    mode = MPOL_BIND;
                    nodemask = 1UL << nid;
                }

                if (mode != MPOL_DEFAULT) {
                    long pagesize = sysconf(_SC_PAGESIZE);
                    /* mbind on the entire address space (best-effort via MPOL_MF_MOVE) */
                    unsigned long addr = 0;
                    unsigned long len = (1UL << 47); /* try to cover full VA */
                    mbind((void*)addr, len, mode, &nodemask, sizeof(nodemask)*8,
                          MPOL_MF_STRICT | MPOL_MF_MOVE);
                    (void)pagesize; /* silence unused warning */
                }
            }

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

    /* Override NUMA CPU topology from user-specified numa-topo spec */
    if (config->numa_topo_spec && config->numa_topo_spec[0] &&
        result->sysinfo && result->sysinfo->numa_nodes) {
        int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
        int *c2n = calloc(ncpu, sizeof(int));
        if (c2n) {
            for (int i = 0; i < ncpu; i++) c2n[i] = -1;
            int bound = harness_parse_numa_bind(config->numa_topo_spec, c2n, ncpu);
            if (bound > 0) {
                /* Count distinct NUMA nodes + CPUs per node */
                int max_nid = -1;
                for (int i = 0; i < ncpu; i++)
                    if (c2n[i] > max_nid) max_nid = c2n[i];
                int n_nodes = max_nid + 1;
                /* Free old numa_nodes, rebuild from user spec */
                free(result->sysinfo->numa_nodes);
                result->sysinfo->numa_node_count = n_nodes;
                result->sysinfo->numa_nodes = calloc(n_nodes, sizeof(numa_node_t));
                if (result->sysinfo->numa_nodes) {
                    for (int n = 0; n < n_nodes; n++) {
                        result->sysinfo->numa_nodes[n].id = n;
                        /* Count CPUs for this node */
                        for (int i = 0; i < ncpu; i++)
                            if (c2n[i] == n)
                                result->sysinfo->numa_nodes[n].cpu_count++;
                        result->sysinfo->numa_nodes[n].cpu_list =
                            malloc(result->sysinfo->numa_nodes[n].cpu_count * sizeof(int));
                        int ci = 0;
                        for (int i = 0; i < ncpu; i++)
                            if (c2n[i] == n)
                                result->sysinfo->numa_nodes[n].cpu_list[ci++] = i;
                        /* Set self-distance=10, others=20 as default */
                        for (int d = 0; d < 16; d++)
                            result->sysinfo->numa_nodes[n].distance[d] = (d == n) ? 10 : 20;
                    }
                }
                fprintf(stderr, "[numa-bind] topology override: %d CPUs → %d nodes\n",
                        bound, n_nodes);
            }
            free(c2n);
        }
    }

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
            ret = harness_run_parallel(b, config->mode, n, &sr->stats, config);
        else
            ret = harness_run_single(b, config->mode, &sr->stats, config);

        sr->status = (ret == 0) ? "completed" : "failed";
        result->subtest_count++;

        /* Cooldown between benchmarks in peak mode */
        if (config->mode == SSB_MODE_PEAK && b->cooldown_required) {
            int cooldown = config->cooldown_sec > 0 ? config->cooldown_sec : SSB_COOLDOWN_SEC;
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

        /* Reportable filter: skip high-CV benchmarks from scoring */
        if (config->reportable && sr->stats.cv >= SSB_CV_HIGH) {
            sr->normalized_score = 0.0;
            continue;
        }

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
