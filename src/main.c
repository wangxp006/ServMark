#include "servmark.h"
#include "harness.h"
#include "system.h"
#include "output.h"
#include "scoring.h"
#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <ctype.h>

#define MAX_BENCH_FILTER 64

static void print_usage(const char *prog) {
    printf("ServMark %s\n", SSB_VERSION);
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --config <file>           Config file (default: config/default.cfg)\n");
    printf("  --mode <peak|sustained>   Run mode (default: peak)\n");
    printf("  --validate                Run system validation only\n");
    printf("  --tier <1|2|3>            Run specific tier only (default: 1)\n");
    printf("  --category <C1..C15>      Run specific category only\n");
    printf("  --benchmark <name>        Run specific benchmark by name\n");
    printf("  --threads <N>             Run N parallel instances (default: auto)\n");
    printf("  --mitigations-off         Run with mitigations=off reference\n");
    printf("  --output-dir <dir>        Output directory (default: .)\n");
    printf("  --reference <file>        Frozen reference file path\n");
    printf("  --dry-run                 List benchmarks without running\n");
    printf("  --list-categories         List categories and exit\n");
    printf("  --list-topology           Show CPU/NUMA/cache topology and exit\n");
    printf("  --min-iterations <N>      Override min iterations (default: 5)\n");
    printf("  --max-iterations <N>      Override max iterations (default: 31)\n");
    printf("  --convergence <F>         Override convergence SEM/mean target\n");
    printf("  --max-runtime <sec>       Override max runtime per benchmark\n");
    printf("  --cpu-pin <spec>          CPU pinning: auto|auto-all|auto-numa|<list>\n");
    printf("  --numa-topo <spec>        NUMA topo: N|@file|cpu_range:nid,...\n");
    printf("  --membind <policy>        Memory policy: local|interleave|<node_id>\n");
    printf("  --cooldown <sec>          Override cooldown between benchmarks\n");
    printf("  --version                 Print version and exit\n");
    printf("  --help                    Show this help\n");
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

/* Parse SPEC-style config file. Sets defaults before CLI overrides. */
static int parse_config(const char *path, run_config_t *cfg, char ***bench_filter, int *bf_count) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Warning: cannot open config '%s', using defaults\n", path);
        return -1;
    }

    char line[512];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *p = trim(line);
        if (*p == '#' || *p == '\0') continue;

        /* Split at '=' */
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(p);
        char *value = trim(eq + 1);

        if (strcmp(key, "runmode") == 0) {
            if (strcmp(value, "sustained") == 0) cfg->mode = SSB_MODE_SUSTAINED;
            else cfg->mode = SSB_MODE_PEAK;
        } else if (strcmp(key, "threads") == 0) {
            cfg->num_instances = atoi(value);
        } else if (strcmp(key, "output_dir") == 0) {
            cfg->output_dir = strdup(value);
        } else if (strcmp(key, "tier") == 0) {
            cfg->tier_mask = 1 << atoi(value);
        } else if (strcmp(key, "category") == 0) {
            if (value[0]) cfg->category_filter = strdup(value);
        } else if (strcmp(key, "mitigations_off") == 0) {
            cfg->mitigations_off = (atoi(value) != 0);
        } else if (strcmp(key, "min_iterations") == 0) {
            cfg->min_iterations = atoi(value);
        } else if (strcmp(key, "max_iterations") == 0) {
            cfg->max_iterations = atoi(value);
        } else if (strcmp(key, "convergence") == 0) {
            cfg->convergence_target = atof(value);
        } else if (strcmp(key, "max_runtime") == 0) {
            cfg->max_runtime_sec = atoi(value);
        } else if (strcmp(key, "cooldown_sec") == 0) {
            cfg->cooldown_sec = atoi(value);
        } else if (strcmp(key, "reportable") == 0) {
            cfg->reportable = (atoi(value) != 0);
        } else if (strcmp(key, "require_validate") == 0) {
            cfg->require_validate = (atoi(value) != 0);
        } else if (strcmp(key, "cpu_pin") == 0) {
            cfg->cpu_pin_spec = strdup(value);
        } else if (strcmp(key, "numa_bind") == 0 ||         /* deprecated alias */
                   strcmp(key, "numa_topo") == 0) {
            cfg->numa_topo_spec = strdup(value);
        } else if (strcmp(key, "membind") == 0) {
            cfg->membind_spec = strdup(value);
        } else if (strcmp(key, "march_native") == 0 ||
                   strcmp(key, "isa_baseline") == 0) {
            fprintf(stderr, "Warning: %s:%d: key '%s' is build-time only, set via cmake -DSSB_USE_MARCH_NATIVE=ON\n",
                    path, lineno, key);
        } else if (strcmp(key, "benchmark") == 0) {
            /* Format: benchmark = C1 : name : description
             * Extract the benchmark name (second colon-separated field) */
            char *c1 = strchr(value, ':');
            if (c1) {
                *c1 = '\0';
                char *name = trim(c1 + 1);
                char *c2 = strchr(name, ':');
                if (c2) *c2 = '\0';
                name = trim(name);
                if (*bf_count < MAX_BENCH_FILTER) {
                    (*bench_filter)[*bf_count] = strdup(name);
                    (*bf_count)++;
                }
            }
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    srand((unsigned)time(NULL));

    run_config_t config = {
        .mode = SSB_MODE_PEAK,
        .mitigations_off = false,
        .tier_mask = 2,
        .output_dir = ".",
        .reference_file = NULL,
        .category_filter = NULL,
        .dry_run = false,
        .num_instances = 0,
    };
    bool validate_only = false;
    /* Search config in: XDG_CONFIG_HOME, /etc, then cwd */
    const char *config_path = NULL;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char xdg_path[512];
    if (!xdg && getenv("HOME")) {
        snprintf(xdg_path, sizeof(xdg_path), "%s/.config/servmark/default.cfg", getenv("HOME"));
        xdg = xdg_path;
    }
    const char *search[] = {xdg, "/etc/servmark/default.cfg", "config/default.cfg"};
    for (int si = 0; si < 3; si++) {
        if (search[si] && access(search[si], R_OK) == 0) { config_path = search[si]; break; }
    }
    if (!config_path) config_path = "config/default.cfg";
    char **bench_filter = malloc(MAX_BENCH_FILTER * sizeof(char *));
    int bench_filter_count = 0;

    static struct option long_opts[] = {
        {"config", required_argument, 0, 'C'},
        {"mode", required_argument, 0, 'm'},
        {"validate", no_argument, 0, 'v'},
        {"tier", required_argument, 0, 't'},
        {"category", required_argument, 0, 'c'},
        {"benchmark", required_argument, 0, 'b'},
        {"threads", required_argument, 0, 'T'},
        {"mitigations-off", no_argument, 0, 'x'},
        {"output-dir", required_argument, 0, 'o'},
        {"reference", required_argument, 0, 'r'},
        {"dry-run", no_argument, 0, 'n'},
        {"list-categories", no_argument, 0, 'L'},
        {"list-topology", no_argument, 0, 'Z'},
        {"min-iterations", required_argument, 0, 'i'},
        {"max-iterations", required_argument, 0, 'I'},
        {"convergence", required_argument, 0, 'g'},
        {"max-runtime", required_argument, 0, 'R'},
        {"cpu-pin", required_argument, 0, 'P'},
        {"numa-topo", required_argument, 0, 'N'},
        {"numa-bind", required_argument, 0, 'N'},   /* deprecated alias */
        {"membind", required_argument, 0, 'M'},
        {"cooldown", required_argument, 0, 'd'},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* Pre-scan argv for --config so we load the right config BEFORE getopt */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[i + 1]; break;
        }
        if (strncmp(argv[i], "--config=", 9) == 0) {
            config_path = argv[i] + 9; break;
        }
        if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            config_path = argv[i + 1]; break;
        }
    }

    /* Parse config file first — CLI args will override */
    parse_config(config_path, &config, &bench_filter, &bench_filter_count);

    int opt;
    while ((opt = getopt_long(argc, argv, "C:m:vt:c:b:T:xo:r:nLZi:I:g:R:P:N:M:d:Vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'C': config_path = optarg; break;
        case 'm':
            if (strcmp(optarg, "sustained") == 0) config.mode = SSB_MODE_SUSTAINED;
            else config.mode = SSB_MODE_PEAK;
            break;
        case 'v': validate_only = true; break;
        case 't':
            config.tier_mask = 1 << atoi(optarg);
            break;
        case 'c': config.category_filter = optarg; break;
        case 'b':
            if (bench_filter_count < MAX_BENCH_FILTER) {
                bench_filter[bench_filter_count] = strdup(optarg);
                bench_filter_count++;
            }
            break;
        case 'T': config.num_instances = atoi(optarg); break;
        case 'x': config.mitigations_off = true; break;
        case 'o': config.output_dir = optarg; break;
        case 'r': config.reference_file = optarg; break;
        case 'n': config.dry_run = true; break;
        case 'L': {
            int ncat;
            const category_weight_t *cats = scoring_get_categories(&ncat);
            printf("Categories (%d):\n", ncat);
            printf("  %-6s %-30s %8s\n", "ID", "Name", "Weight");
            printf("  ------ ------------------------------ --------\n");
            for (int ci = 0; ci < ncat; ci++)
                printf("  %-6s %-30s %7.0f%%\n",
                       cats[ci].id, cats[ci].name, cats[ci].weight * 100.0);
            free(bench_filter); return 0;
        }
        case 'Z': {
            system_info_t *info = NULL;
            system_probe(&info);
            if (info) {
                printf("CPU topology:\n");
                printf("  Model: %s\n", info->cpu_model);
                printf("  Physical cores: %d, Logical threads: %d, SMT: %s\n",
                       info->cpu_cores_physical, info->cpu_threads_logical,
                       info->smt_enabled ? "yes" : "no");
                printf("  NUMA nodes: %d\n", info->numa_node_count);
                for (int ni = 0; ni < info->numa_node_count; ni++) {
                    printf("    Node %d: %d CPUs, %zu MB RAM\n",
                           info->numa_nodes[ni].id,
                           info->numa_nodes[ni].cpu_count,
                           info->numa_nodes[ni].memory_kb / 1024);
                }
                printf("  Cache levels: %d\n", info->cache_level_count);
                for (int ci = 0; ci < info->cache_level_count; ci++) {
                    printf("    L%d %s: %zu KB (line=%d, assoc=%d)\n",
                           info->caches[ci].level, info->caches[ci].type,
                           info->caches[ci].size_kb,
                           info->caches[ci].line_size,
                           info->caches[ci].associativity);
                }
                system_free(info);
            }
            free(bench_filter); return 0;
        }
        case 'i': config.min_iterations = atoi(optarg); break;
        case 'I': config.max_iterations = atoi(optarg); break;
        case 'g': config.convergence_target = atof(optarg); break;
        case 'R': config.max_runtime_sec = atoi(optarg); break;
        case 'P': config.cpu_pin_spec = optarg; break;
        case 'N': config.numa_topo_spec = optarg; break;
        case 'M': config.membind_spec = optarg; break;
        case 'd': config.cooldown_sec = atoi(optarg); break;
        case 'V': printf("ServMark %s\n", SSB_VERSION); free(bench_filter); return 0;
        case 'h': print_usage(argv[0]); free(bench_filter); return 0;
        default: print_usage(argv[0]); free(bench_filter); return 1;
        }
    }

    /* CLI overrides */
    if (config.num_instances <= 0)
        config.num_instances = SSB_NUM_CPUS();

    /* Warn on high instance count on large servers */
    if (config.num_instances > 64)
        fprintf(stderr, "Warning: %d parallel instances on a %d-CPU system"
                " may cause memory pressure\n",
                config.num_instances, config.num_instances);

    config.bench_filter = bench_filter;
    config.bench_filter_count = bench_filter_count;

    /* Auto-create output directory if it doesn't exist */
    {   char mkdir_cmd[512];
        snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", config.output_dir);
        system(mkdir_cmd);
    }

    if (config.require_validate) {
        printf("Pre-flight validation (require_validate=1):\n");
        validate_system(stdout);
    }

    if (validate_only) {
        validate_system(stdout);
        free(bench_filter);
        return 0;
    }

    if (config.dry_run) {
        const benchmark_t **benchmarks;
        int count;
        benchmark_get_all(&benchmarks, &count);
        printf("Registered benchmarks (%d total):\n", count);
        for (int i = 0; i < count; i++) {
            const benchmark_t *b = benchmarks[i];

            /* Filter by config benchmark list if specified */
            if (bench_filter_count > 0) {
                bool found = false;
                for (int j = 0; j < bench_filter_count; j++) {
                    if (strcmp(b->name, bench_filter[j]) == 0) {
                        found = true; break;
                    }
                }
                if (!found) continue;
            }

            printf("  [%s] %-30s \"%s\" (Tier %d)\n",
                    b->category, b->name, b->description, b->tier);
        }

        if (bench_filter_count > 0)
            printf("\n  (Filtered: %d benchmarks selected from config)\n", bench_filter_count);

        for (int j = 0; j < bench_filter_count; j++) free(bench_filter[j]);
        free(bench_filter);
        return 0;
    }

    int tier_display = (config.tier_mask != 0) ? __builtin_ctz(config.tier_mask) : 0;
    printf("\n  ServMark %s  |  Mode: %s  |  Tier %d  |  Instances: %d",
            SSB_VERSION,
            config.mode == SSB_MODE_PEAK ? "peak" : "sustained",
            tier_display,
            config.num_instances);
    if (bench_filter_count > 0)
        printf("  |  Benchmarks: %d", bench_filter_count);
    printf("\n");
    printf("  Config: %s\n", config_path);
    printf("  ───────────────────────────────────────────\n\n");

    run_result_t *result = NULL;
    int ret = harness_run(&config, &result);
    if (ret != 0 || !result) {
        fprintf(stderr, "Error: benchmark run failed\n");
        for (int j = 0; j < bench_filter_count; j++) free(bench_filter[j]);
        free(bench_filter);
        return 1;
    }

    scoring_compute_pillars(result, &result->pillar_throughput,
            &result->pillar_latency, &result->pillar_efficiency);
    result->overall_score = scoring_overall_score(
            result->pillar_throughput, result->pillar_latency,
            result->pillar_efficiency);

    char path[512];

    snprintf(path, sizeof(path), "%s/%s.provenance.json",
            config.output_dir, result->run_id);
    output_provenance(result, path);
    printf("  Provenance: %s\n", path);

    snprintf(path, sizeof(path), "%s/%s.results.jsonl",
            config.output_dir, result->run_id);
    output_jsonl(result, path);
    printf("  Results:    %s\n", path);

    snprintf(path, sizeof(path), "%s/%s.report.html",
            config.output_dir, result->run_id);
    output_html_report(result, path);
    printf("  Report:     %s\n", path);

    output_terminal_summary(result);

    harness_free_result(result);
    for (int j = 0; j < bench_filter_count; j++) free(bench_filter[j]);
    free(bench_filter);
    return 0;
}
