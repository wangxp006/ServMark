#include "output.h"
#include "system.h"
#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void output_generate_uuid(char buf[37]) {
    snprintf(buf, 37, "%08x-%04x-%04x-%04x-%04x%08x",
            (unsigned)rand(), (unsigned)rand() & 0xffff,
            ((unsigned)rand() & 0x0fff) | 0x4000,
            ((unsigned)rand() & 0x3fff) | 0x8000,
            (unsigned)rand() & 0xffff, (unsigned)rand());
}

int output_provenance(const run_result_t *result, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"servmark_version\": \"%s\",\n", SSB_VERSION);
    fprintf(f, "  \"run_id\": \"%s\",\n", result->run_id);
    fprintf(f, "  \"run_mode\": \"%s\",\n",
            result->config.mode == SSB_MODE_PEAK ? "peak" : "sustained");
    fprintf(f, "  \"overall_score\": %.4f,\n", result->overall_score);
    fprintf(f, "  \"pillars\": {\n");
    fprintf(f, "    \"throughput\": %.4f,\n", result->pillar_throughput);
    fprintf(f, "    \"latency\": %.4f,\n", result->pillar_latency);
    fprintf(f, "    \"efficiency\": %.4f\n", result->pillar_efficiency);
    fprintf(f, "  },\n");
    fprintf(f, "  \"mitigation_tax_pct\": %.1f,\n", result->mitigation_tax_pct * 100.0);
    fprintf(f, "  \"thermal_derating\": %.3f,\n", result->thermal_derating_factor);

    /* System info */
    if (result->sysinfo) {
        fprintf(f, "  \"system\": {\n");
        fprintf(f, "    \"cpu_model\": \"%s\",\n", result->sysinfo->cpu_model);
        fprintf(f, "    \"cpu_cores\": %d,\n", result->sysinfo->cpu_cores_physical);
        fprintf(f, "    \"numa_nodes\": %d,\n", result->sysinfo->numa_node_count);
        fprintf(f, "    \"kernel\": \"%s\",\n", result->sysinfo->kernel_version);
        fprintf(f, "    \"page_size\": %zu\n", result->sysinfo->page_size);
        fprintf(f, "  },\n");
    }

    /* Subtests */
    fprintf(f, "  \"subtests\": [\n");
    for (int i = 0; i < result->subtest_count; i++) {
        subtest_result_t *sr = &result->subtests[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", sr->bench->name);
        fprintf(f, "      \"category\": \"%s\",\n", sr->bench->category);
        fprintf(f, "      \"status\": \"%s\",\n", sr->status);
        fprintf(f, "      \"mean\": %.6f,\n", sr->stats.mean);
        fprintf(f, "      \"cv\": %.4f,\n", sr->stats.cv);
        fprintf(f, "      \"reliability\": \"%s\",\n", sr->stats.reliability);
        fprintf(f, "      \"ci_95\": [%.6f, %.6f],\n", sr->stats.ci_lower, sr->stats.ci_upper);
        fprintf(f, "      \"iterations\": %d\n", sr->stats.iterations);
        fprintf(f, "    }%s\n", (i < result->subtest_count - 1) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");

    fclose(f);
    return 0;
}

int output_jsonl(const run_result_t *result, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    for (int i = 0; i < result->subtest_count; i++) {
        subtest_result_t *sr = &result->subtests[i];
        fprintf(f, "{\"run_id\":\"%s\",\"bench\":\"%s\",\"cat\":\"%s\","
                "\"mean\":%.6f,\"cv\":%.4f,\"rel\":\"%s\",\"n\":%d}\n",
                result->run_id, sr->bench->name, sr->bench->category,
                sr->stats.mean, sr->stats.cv, sr->stats.reliability,
                sr->stats.iterations);
    }
    fclose(f);
    return 0;
}

int output_terminal_summary(const run_result_t *result) {
    time_t elapsed = result->end_time - result->start_time;
    int min = (int)(elapsed / 60);
    int sec = (int)(elapsed % 60);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  ServMark %-48s ║\n", SSB_VERSION);
    printf("║  Mode: %-6s    Elapsed: %dm%02ds                          ║\n",
            result->config.mode == SSB_MODE_PEAK ? "peak" : "sustained",
            min, sec);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Overall:     %.2f                                         ║\n", result->overall_score);
    printf("║  Throughput:  %.2f                                         ║\n", result->pillar_throughput);
    printf("║  Latency:     %.2f                                         ║\n", result->pillar_latency);
    printf("║  Efficiency:  %.2f                                         ║\n", result->pillar_efficiency);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Mitigation Tax: %.1f%%    Thermal Derating: %.1f%%          ║\n",
            result->mitigation_tax_pct * 100.0,
            result->thermal_derating_factor * 100.0);
    printf("╠══════════════════════════════════════════════════════════════╣\n");

    /* Per-category summary */
    for (int i = 0; i < 16; i++) {
        if (result->category_scores[i] > 0) {
            printf("║  C%-2d: %.2f                                               ║\n",
                    i, result->category_scores[i]);
        }
    }
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    return 0;
}

int output_html_report(const run_result_t *result, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<title>ServMark Results</title></head><body>\n");
    fprintf(f, "<h1>ServMark %s Results</h1>\n", SSB_VERSION);
    fprintf(f, "<p>Run ID: %s | Mode: %s</p>\n",
            result->run_id,
            result->config.mode == SSB_MODE_PEAK ? "Peak" : "Sustained");
    fprintf(f, "<table border=\"1\"><tr><th>Benchmark</th><th>Category</th>"
            "<th>Score</th><th>CV</th><th>Reliability</th></tr>\n");

    for (int i = 0; i < result->subtest_count; i++) {
        subtest_result_t *sr = &result->subtests[i];
        fprintf(f, "<tr><td>%s</td><td>%s</td><td>%.4f</td>"
                "<td>%.2f%%</td><td>%s</td></tr>\n",
                sr->bench->name, sr->bench->category,
                sr->stats.mean, sr->stats.cv * 100.0,
                sr->stats.reliability);
    }
    fprintf(f, "</table></body></html>\n");
    fclose(f);
    return 0;
}
