#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>

#define PARSE_ITEMS 500000

typedef struct {
    char *buffer;
    size_t buf_size;
    int64_t *results;
} int_parse_state_t;

static int int_parse_init(void **state) {
    int_parse_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->buf_size = PARSE_ITEMS * 32;
    s->buffer = malloc(s->buf_size);
    s->results = malloc(PARSE_ITEMS * sizeof(int64_t));
    if (!s->buffer || !s->results) {
        free(s->buffer); free(s->results); free(s);
        return -1;
    }
    srand(time(NULL));
    char *p = s->buffer;
    for (int i = 0; i < PARSE_ITEMS; i++) {
        int64_t val = ((int64_t)rand() << 32) | rand();
        p += snprintf(p, 32, "%ld,", val);
    }
    *state = s;
    return 0;
}

static int int_parse_warmup(void *state) {
    int_parse_state_t *s = (int_parse_state_t *)state;
    volatile int64_t sink = 0;
    char *p = s->buffer;
    for (int i = 0; i < PARSE_ITEMS / 10; i++) {
        while (*p && *p != ',') { sink += *p; p++; }
        p++;
    }
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int64_t parse_int_fast(const char **pp) {
    const char *p = *pp;
    int64_t val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (*p == ',') p++;
    *pp = p;
    return val;
}

static int int_parse_measure(void *state, measurement_t *result) {
    int_parse_state_t *s = (int_parse_state_t *)state;
    struct timespec t0, t1;
    volatile int64_t sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    const char *p = s->buffer;
    for (int i = 0; i < PARSE_ITEMS; i++) {
        s->results[i] = parse_int_fast(&p);
        sink ^= s->results[i];
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)PARSE_ITEMS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int int_parse_cleanup(void *state) {
    int_parse_state_t *s = (int_parse_state_t *)state;
    free(s->buffer); free(s->results); free(s);
    return 0;
}

benchmark_t bench_int_parse = {
    .name = "int-parse",
    .category = "C1",
    .description = "Integer string parsing (Dhrystone integer ALU modernized)",
    .tier = 1,
    .primary_metric_name = "items/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = int_parse_init,
    .warmup = int_parse_warmup,
    .measure = int_parse_measure,
    .cleanup = int_parse_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_int_parse);
