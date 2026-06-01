#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#define LOG_SIZE (2 * 1024 * 1024)
#define PATTERN_COUNT 200

typedef struct {
    char *haystack;
    char **patterns;
    int *pattern_lens;
} int_regex_state_t;

static int simple_match(const char *haystack, int hs_len, const char *needle, int ndl_len) {
    if (ndl_len == 0) return 0;
    for (int i = 0; i <= hs_len - ndl_len; i++) {
        int j;
        for (j = 0; j < ndl_len; j++) {
            if (haystack[i + j] != needle[j]) break;
        }
        if (j == ndl_len) return 1;
    }
    return 0;
}

static int int_regex_init(void **state) {
    int_regex_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->haystack = malloc(LOG_SIZE + 1);
    s->patterns = malloc(PATTERN_COUNT * sizeof(char *));
    s->pattern_lens = malloc(PATTERN_COUNT * sizeof(int));
    if (!s->haystack || !s->patterns || !s->pattern_lens) {
        free(s->haystack); free(s->patterns); free(s->pattern_lens); free(s);
        return -1;
    }

    /* Generate synthetic log data */
    const char *levels[] = {"INFO", "WARN", "ERROR", "DEBUG"};
    const char *msgs[] = {"processing request", "connection timeout", "disk full",
                          "auth failed", "cache hit", "retry attempt", "shutdown"};
    char *p = s->haystack;
    int remaining = LOG_SIZE;
    while (remaining > 80) {
        int n = snprintf(p, remaining, "[%d] %s: %s id=%d latency=%dus\n",
                rand(), levels[rand() % 4], msgs[rand() % 7],
                rand() % 100000, rand() % 5000);
        p += n; remaining -= n;
    }

    /* Generate search patterns */
    for (int i = 0; i < PATTERN_COUNT; i++) {
        s->patterns[i] = malloc(32);
        snprintf(s->patterns[i], 32, "id=%d", rand() % 100000);
        s->pattern_lens[i] = strlen(s->patterns[i]);
    }
    *state = s;
    return 0;
}

static int int_regex_warmup(void *state) {
    int_regex_state_t *s = (int_regex_state_t *)state;
    volatile int sink = 0;
    for (int i = 0; i < 20; i++) {
        sink += simple_match(s->haystack, LOG_SIZE, s->patterns[i], s->pattern_lens[i]);
    }
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int int_regex_measure(void *state, measurement_t *result) {
    int_regex_state_t *s = (int_regex_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < PATTERN_COUNT; i++) {
        sink += simple_match(s->haystack, LOG_SIZE, s->patterns[i], s->pattern_lens[i]);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (s->haystack ? LOG_SIZE * (double)PATTERN_COUNT / elapsed : 1.0);
    result->wall_seconds = elapsed;
    return 0;
}

static int int_regex_cleanup(void *state) {
    int_regex_state_t *s = (int_regex_state_t *)state;
    free(s->haystack);
    for (int i = 0; i < PATTERN_COUNT; i++) free(s->patterns[i]);
    free(s->patterns); free(s->pattern_lens); free(s);
    return 0;
}

benchmark_t bench_int_regex = {
    .name = "int-regex",
    .category = "C1",
    .description = "Naive regex match on log text (Dhrystone branch path modernized)",
    .tier = 1,
    .primary_metric_name = "byte-scans/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = int_regex_init,
    .warmup = int_regex_warmup,
    .measure = int_regex_measure,
    .cleanup = int_regex_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_int_regex);
