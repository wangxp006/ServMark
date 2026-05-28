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

static void print_usage(const char *prog) {
    printf("ServMark %s\n", SSB_VERSION);
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --mode <peak|sustained>  Run mode (default: peak)\n");
    printf("  --validate               Run system validation only\n");
    printf("  --tier <1|2|3>           Run specific tier only (default: 1)\n");
    printf("  --category <C1..C15>     Run specific category only\n");
    printf("  --threads <N>            Run N parallel instances (one per core)\n");
    printf("  --mitigations-off        Run with mitigations=off reference\n");
    printf("  --output-dir <dir>       Output directory (default: .)\n");
    printf("  --reference <file>       Frozen reference file path\n");
    printf("  --dry-run                List benchmarks without running\n");
    printf("  --help                   Show this help\n");
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

    static struct option long_opts[] = {
        {"mode", required_argument, 0, 'm'},
        {"validate", no_argument, 0, 'v'},
        {"tier", required_argument, 0, 't'},
        {"category", required_argument, 0, 'c'},
        {"threads", required_argument, 0, 'T'},
        {"mitigations-off", no_argument, 0, 'x'},
        {"output-dir", required_argument, 0, 'o'},
        {"reference", required_argument, 0, 'r'},
        {"dry-run", no_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:vt:c:T:xo:r:nh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'm':
            if (strcmp(optarg, "sustained") == 0) config.mode = SSB_MODE_SUSTAINED;
            else config.mode = SSB_MODE_PEAK;
            break;
        case 'v': validate_only = true; break;
        case 't':
            config.tier_mask = 1 << atoi(optarg);
            break;
        case 'c': config.category_filter = optarg; break;
        case 'T': config.num_instances = atoi(optarg); break;
        case 'x': config.mitigations_off = true; break;
        case 'o': config.output_dir = optarg; break;
        case 'r': config.reference_file = optarg; break;
        case 'n': config.dry_run = true; break;
        case 'h': print_usage(argv[0]); return 0;
        default: print_usage(argv[0]); return 1;
        }
    }

    if (config.num_instances <= 0)
        config.num_instances = SSB_NUM_CPUS();

    if (validate_only) {
        validate_system(stdout);
        return 0;
    }

    if (config.dry_run) {
        const benchmark_t **benchmarks;
        int count;
        benchmark_get_all(&benchmarks, &count);
        printf("Registered benchmarks (%d total):\n", count);
        for (int i = 0; i < count; i++) {
            const benchmark_t *b = benchmarks[i];
            printf("  [%s] %-30s \"%s\" (Tier %d)\n",
                    b->category, b->name, b->description, b->tier);
        }
        return 0;
    }

    printf("\n  ServMark %s  |  Mode: %s  |  Tier %d  |  Instances: %d\n",
            SSB_VERSION,
            config.mode == SSB_MODE_PEAK ? "peak" : "sustained",
            __builtin_ctz(config.tier_mask),
            config.num_instances);
    printf("  ───────────────────────────────────────────\n\n");

    run_result_t *result = NULL;
    int ret = harness_run(&config, &result);
    if (ret != 0 || !result) {
        fprintf(stderr, "Error: benchmark run failed\n");
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
    return 0;
}
