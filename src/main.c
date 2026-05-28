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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#define MAX_BENCH_FILTER 64

static void print_usage(const char *prog) {
    printf("ServMark %s\n", SSB_VERSION);
    printf("Usage: %s [options]\n", prog);
    printf("  --action submit|run    Submit: create run dir. Run: execute benchmarks\n");
    printf("  --rundir <dir>         Run directory path\n");
    printf("  --config <file>        Config file (default: config/default.cfg)\n");
    printf("  --mode <peak|sustained> Run mode\n");
    printf("  --tier <1|2|3>         Tier filter\n");
    printf("  --category <C1..C15>   Category filter\n");
    printf("  --threads <N>          Parallel instances\n");
    printf("  --output-dir <dir>     Output directory\n");
    printf("  --dry-run              List benchmarks\n");
    printf("  --validate             System validation\n");
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static int parse_config(const char *path, run_config_t *cfg, char ***bench_filter, int *bf_count) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Warning: cannot open '%s'\n", path); return -1; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '\0') continue;
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
        } else if (strcmp(key, "copies") == 0) {
            cfg->copies = atoi(value);
        } else if (strcmp(key, "bind") == 0) {
            cfg->bind_count = 0;
            char *save, *tok = strtok_r(value, " \t", &save);
            while (tok && cfg->bind_count < SSB_MAX_BIND) {
                cfg->bind_list[cfg->bind_count++] = atoi(tok);
                tok = strtok_r(NULL, " \t", &save);
            }
        } else if (strcmp(key, "output_dir") == 0) {
            cfg->output_dir = strdup(value);
        } else if (strcmp(key, "tier") == 0) {
            cfg->tier_mask = 1 << atoi(value);
        } else if (strcmp(key, "category") == 0) {
            if (value[0]) cfg->category_filter = strdup(value);
        } else if (strcmp(key, "mitigations_off") == 0) {
            cfg->mitigations_off = (atoi(value) != 0);
        } else if (strcmp(key, "benchmark") == 0) {
            char *c1 = strchr(value, ':');
            if (c1) {
                *c1 = '\0'; char *name = trim(c1 + 1);
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

static void filter_benchmarks(const run_config_t *cfg, const benchmark_t **benchmarks,
                               int count, const benchmark_t **out, int *out_count) {
    int kept = 0;
    for (int i = 0; i < count; i++) {
        const benchmark_t *b = benchmarks[i];
        if (!(cfg->tier_mask & (1 << b->tier))) continue;
        if (cfg->category_filter && strcmp(b->category, cfg->category_filter) != 0) continue;
        if (cfg->bench_filter_count > 0) {
            bool found = false;
            for (int j = 0; j < cfg->bench_filter_count; j++)
                if (strcmp(b->name, cfg->bench_filter[j]) == 0) { found = true; break; }
            if (!found) continue;
        }
        out[kept++] = b;
    }
    *out_count = kept;
}

/* ── submit: SPEC-style with copies + bind ── */
static int action_submit(const char *rundir, const char *config_path,
                         run_config_t *cfg, const char *bin_path) {
    mkdir(rundir, 0755);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cp '%s' '%s/speccmd.cfg' 2>/dev/null", config_path, rundir);
    system(cmd);

    const benchmark_t **benchmarks;
    int total;
    benchmark_get_all(&benchmarks, &total);

    const benchmark_t **selected = malloc(total * sizeof(benchmark_t *));
    int count;
    filter_benchmarks(cfg, benchmarks, total, selected, &count);

    int copies = cfg->copies > 0 ? cfg->copies : 1;
    int n_threads = cfg->num_instances > 0 ? cfg->num_instances : SSB_NUM_CPUS();

    char buf[512];
    snprintf(buf, sizeof(buf), "%s/List.txt", rundir);
    FILE *list = fopen(buf, "w");
    if (list) {
        fprintf(list, "# ServMark Benchmark List\n");
        fprintf(list, "# copies=%d bind_count=%d\n\n", copies, cfg->bind_count);
    }

    for (int i = 0; i < count; i++) {
        const benchmark_t *b = selected[i];
        snprintf(buf, sizeof(buf), "%s/%s", rundir, b->name);
        mkdir(buf, 0755);

        /* Write bench-level run_all.sh */
        snprintf(buf, sizeof(buf), "%s/%s/run_all.sh", rundir, b->name);
        FILE *ra = fopen(buf, "w");
        if (ra) {
            fprintf(ra, "#!/bin/sh\n# Run all copies of %s\n", b->name);
            fprintf(ra, "cd \"$(dirname \"$0\")\"\n");
            fprintf(ra, "for d in copy-*; do\n");
            fprintf(ra, "  [ -x \"$d/run.sh\" ] && cd \"$d\" && ./run.sh && cd ..\n");
            fprintf(ra, "done\n");
            fclose(ra); chmod(buf, 0755);
        }

        /* Generate per-copy directories */
        for (int c = 0; c < copies; c++) {
            snprintf(buf, sizeof(buf), "%s/%s/copy-%04d", rundir, b->name, c);
            mkdir(buf, 0755);

            snprintf(buf, sizeof(buf), "%s/%s/copy-%04d/run.sh", rundir, b->name, c);
            FILE *sh = fopen(buf, "w");
            if (!sh) continue;

            fprintf(sh, "#!/bin/sh\n");
            fprintf(sh, "# ServMark: %s  copy %d/%d\n", b->name, c, copies);

            /* Bind to specific CPU if bind list provided */
            if (c < cfg->bind_count)
                fprintf(sh, "taskset -c %d $$\n", cfg->bind_list[c]);
            else if (cfg->bind_count > 0)
                fprintf(sh, "taskset -c %d $$\n", cfg->bind_list[c % cfg->bind_count]);

            fprintf(sh, "cd \"$(dirname \"$0\")\"\n");
            fprintf(sh, "exec %s \\\n", bin_path);
            fprintf(sh, "  --category %s \\\n", b->category);
            fprintf(sh, "  --threads %d \\\n", n_threads);
            fprintf(sh, "  --tier %d \\\n", __builtin_ctz(cfg->tier_mask));
            fprintf(sh, "  --mode %s \\\n",
                    cfg->mode == SSB_MODE_PEAK ? "peak" : "sustained");
            fprintf(sh, "  --output-dir .\n");
            fclose(sh); chmod(buf, 0755);
        }

        if (list) fprintf(list, "%s\n", b->name);
    }
    if (list) fclose(list);

    /* Metadata */
    snprintf(buf, sizeof(buf), "%s/servmark.json", rundir);
    FILE *meta = fopen(buf, "w");
    if (meta) {
        fprintf(meta, "{\"version\":\"%s\",\"copies\":%d,\"bind_count\":%d,\"benchmarks\":%d}\n",
                SSB_VERSION, copies, cfg->bind_count, count);
        fclose(meta);
    }

    printf("  Run directory: %s\n", rundir);
    printf("  Benchmarks:    %d selected\n", count);
    printf("  Copies:        %d per benchmark\n", copies);
    if (cfg->bind_count > 0)
        printf("  Bind list:     %d CPUs (copy 0 → CPU %d)\n", cfg->bind_count, cfg->bind_list[0]);
    printf("\n  Ready. Run with:\n");
    printf("    servmark --action run --rundir %s\n", rundir);
    free(selected);
    return 0;
}

/* ── run: execute per-copy scripts ── */
static int action_run(const char *rundir) {
    char path[512];
    snprintf(path, sizeof(path), "%s/List.txt", rundir);
    FILE *list = fopen(path, "r");
    if (!list) { fprintf(stderr, "Error: no List.txt in %s\n", rundir); return 1; }

    int total = 0, pass = 0, fail = 0;
    time_t start = time(NULL);

    printf("\n  ServMark %s  |  Run: %s\n", SSB_VERSION, rundir);
    printf("  ===========================================\n\n");

    char line[256];
    while (fgets(line, sizeof(line), list)) {
        char *bname = trim(line);
        if (*bname == '#' || *bname == '\0') continue;

        /* Walk copy directories */
        DIR *d = NULL;
        char bench_dir[512];
        snprintf(bench_dir, sizeof(bench_dir), "%s/%s", rundir, bname);
        d = opendir(bench_dir);
        if (!d) { fprintf(stderr, "  SKIP %s (dir not found)\n", bname); continue; }

        int bench_ok = 0, bench_fail = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "copy-", 5) != 0) continue;
            snprintf(path, sizeof(path), "%s/%s/run.sh", bench_dir, de->d_name);
            if (access(path, X_OK) != 0) continue;

            printf("  %-25s %-12s ", bname, de->d_name); fflush(stdout);

            char log_path[512], cmd[512];
            snprintf(log_path, sizeof(log_path), "%s/%s/log.txt", bench_dir, de->d_name);
            snprintf(cmd, sizeof(cmd), "cd '%s/%s' && ./run.sh > log.txt 2>&1",
                    bench_dir, de->d_name);

            int ret = system(cmd);
            total++;
            if (ret == 0) { printf("OK\n"); pass++; bench_ok++; }
            else { printf("FAILED (exit %d)\n", WEXITSTATUS(ret)); fail++; bench_fail++; }
        }
        closedir(d);
        printf("  → %s: %d pass, %d fail\n\n", bname, bench_ok, bench_fail);
    }
    fclose(list);

    time_t elapsed = time(NULL) - start;
    printf("  ===========================================\n");
    printf("  Total copies: %d  Pass: %d  Fail: %d  Elapsed: %ldm%lds\n",
            total, pass, fail, elapsed / 60, elapsed % 60);
    return fail > 0 ? 1 : 0;
}

/* ── main ── */
int main(int argc, char *argv[]) {
    srand((unsigned)time(NULL));

    run_config_t config = {
        .mode = SSB_MODE_PEAK, .mitigations_off = false, .tier_mask = 2,
        .output_dir = ".", .reference_file = NULL, .category_filter = NULL,
        .dry_run = false, .num_instances = 0, .copies = 1, .bind_count = 0,
        .bench_filter = NULL, .bench_filter_count = 0,
    };
    bool validate_only = false;
    const char *config_path = "config/default.cfg";
    const char *action = NULL;
    const char *rundir = NULL;
    char **bench_filter = malloc(MAX_BENCH_FILTER * sizeof(char *));
    int bench_filter_count = 0;

    static struct option long_opts[] = {
        {"action", required_argument, 0, 'A'}, {"rundir", required_argument, 0, 'D'},
        {"config", required_argument, 0, 'C'}, {"mode", required_argument, 0, 'm'},
        {"validate", no_argument, 0, 'v'}, {"tier", required_argument, 0, 't'},
        {"category", required_argument, 0, 'c'}, {"threads", required_argument, 0, 'T'},
        {"mitigations-off", no_argument, 0, 'x'}, {"output-dir", required_argument, 0, 'o'},
        {"reference", required_argument, 0, 'r'}, {"dry-run", no_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'}, {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "A:D:C:m:vt:c:T:xo:r:nh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'A': action = optarg; break;
        case 'D': rundir = optarg; break;
        case 'C': config_path = optarg; break;
        case 'm':
            if (strcmp(optarg, "sustained") == 0) config.mode = SSB_MODE_SUSTAINED;
            else config.mode = SSB_MODE_PEAK;
            break;
        case 'v': validate_only = true; break;
        case 't': config.tier_mask = 1 << atoi(optarg); break;
        case 'c': config.category_filter = optarg; break;
        case 'T': config.num_instances = atoi(optarg); break;
        case 'x': config.mitigations_off = true; break;
        case 'o': config.output_dir = optarg; break;
        case 'r': config.reference_file = optarg; break;
        case 'n': config.dry_run = true; break;
        case 'h': print_usage(argv[0]); free(bench_filter); return 0;
        default: print_usage(argv[0]); free(bench_filter); return 1;
        }
    }

    parse_config(config_path, &config, &bench_filter, &bench_filter_count);
    if (config.num_instances <= 0) config.num_instances = SSB_NUM_CPUS();
    config.bench_filter = bench_filter;
    config.bench_filter_count = bench_filter_count;

    /* ── Action: submit ── */
    if (action && strcmp(action, "submit") == 0) {
        if (!rundir) {
            char auto_rundir[256];
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            snprintf(auto_rundir, sizeof(auto_rundir), "runs/%04d%02d%02d-%02d%02d",
                    t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min);
            rundir = strdup(auto_rundir);
        }
        char bin_path[512];
        ssize_t len = readlink("/proc/self/exe", bin_path, sizeof(bin_path)-1);
        if (len > 0) bin_path[len] = '\0';
        else strcpy(bin_path, "./servmark");
        int r = action_submit(rundir, config_path, &config, bin_path);
        for (int j = 0; j < bench_filter_count; j++) free(bench_filter[j]);
        free(bench_filter); return r;
    }

    /* ── Action: run ── */
    if (action && strcmp(action, "run") == 0) {
        if (!rundir) { fprintf(stderr, "Error: --rundir required with --action run\n");
            free(bench_filter); return 1; }
        int r = action_run(rundir);
        for (int j = 0; j < bench_filter_count; j++) free(bench_filter[j]);
        free(bench_filter); return r;
    }

    /* ── Direct execution ── */
    if (validate_only) { validate_system(stdout); free(bench_filter); return 0; }
    if (config.dry_run) {
        const benchmark_t **benchmarks; int count;
        benchmark_get_all(&benchmarks, &count);
        for (int i = 0; i < count; i++) {
            const benchmark_t *b = benchmarks[i];
            if (bench_filter_count > 0) {
                bool found = false;
                for (int j = 0; j < bench_filter_count; j++)
                    if (strcmp(b->name, bench_filter[j]) == 0) { found = true; break; }
                if (!found) continue;
            }
            printf("  [%s] %-30s \"%s\" (Tier %d)\n", b->category, b->name, b->description, b->tier);
        }
        free(bench_filter); return 0;
    }

    printf("\n  ServMark %s  |  Mode: %s  |  Tier %d  |  Instances: %d",
            SSB_VERSION, config.mode == SSB_MODE_PEAK ? "peak" : "sustained",
            __builtin_ctz(config.tier_mask), config.num_instances);
    if (bench_filter_count > 0) printf("  |  Benchmarks: %d", bench_filter_count);
    printf("\n  ===========================================\n\n");

    run_result_t *result = NULL;
    if (harness_run(&config, &result) != 0 || !result) {
        fprintf(stderr, "Error: benchmark run failed\n");
        free(bench_filter); return 1;
    }

    scoring_compute_pillars(result, &result->pillar_throughput,
            &result->pillar_latency, &result->pillar_efficiency);
    result->overall_score = scoring_overall_score(
            result->pillar_throughput, result->pillar_latency, result->pillar_efficiency);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.provenance.json", config.output_dir, result->run_id);
    output_provenance(result, path);
    printf("  Provenance: %s\n", path);

    snprintf(path, sizeof(path), "%s/%s.results.jsonl", config.output_dir, result->run_id);
    output_jsonl(result, path);
    printf("  Results:    %s\n", path);

    snprintf(path, sizeof(path), "%s/%s.report.html", config.output_dir, result->run_id);
    output_html_report(result, path);
    printf("  Report:     %s\n", path);

    output_terminal_summary(result);
    harness_free_result(result);
    free(bench_filter);
    return 0;
}
