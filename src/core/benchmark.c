#include "servmark.h"
#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_BENCHMARKS 128

static const benchmark_t *_benchmarks[MAX_BENCHMARKS];
static int _benchmark_count = 0;

int benchmark_register(const benchmark_t *bench) {
    if (_benchmark_count >= MAX_BENCHMARKS) { fprintf(stderr, "Warning: max benchmarks (%d) reached, ignoring %s\n", MAX_BENCHMARKS, bench->name); return -1; }
    _benchmarks[_benchmark_count++] = bench;
    return 0;
}

int benchmark_get_all(const benchmark_t ***bench_list, int *count) {
    if (!bench_list || !count) return -1;
    *bench_list = _benchmarks;
    *count = _benchmark_count;
    return 0;
}

int benchmark_get_by_category(const char *category,
        const benchmark_t ***bench_list, int *count) {
    if (!bench_list || !count || !category) return -1;
    const benchmark_t **filtered = calloc(_benchmark_count, sizeof(benchmark_t *));
    if (!filtered) { *bench_list = NULL; *count = 0; return -1; }
    int n = 0;
    for (int i = 0; i < _benchmark_count; i++) {
        if (strcmp(_benchmarks[i]->category, category) == 0) {
            filtered[n++] = _benchmarks[i];
        }
    }
    *bench_list = filtered;
    *count = n;
    return 0;
}

int benchmark_get_by_tier(int tier,
        const benchmark_t ***bench_list, int *count) {
    if (!bench_list || !count) return -1;
    const benchmark_t **filtered = calloc(_benchmark_count, sizeof(benchmark_t *));
    if (!filtered) { *bench_list = NULL; *count = 0; return -1; }
    int n = 0;
    for (int i = 0; i < _benchmark_count; i++) {
        if (_benchmarks[i]->tier == tier) {
            filtered[n++] = _benchmarks[i];
        }
    }
    *bench_list = filtered;
    *count = n;
    return 0;
}
