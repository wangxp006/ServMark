#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

#define SCRIPT_ITERS 200

typedef struct {
    char *script_path;
} script_shell_state_t;

static const char *shell_script =
    "#!/bin/sh\n"
    "count=0\n"
    "while [ $count -lt 50 ]; do\n"
    "  echo \"hello world $count\" | sed 's/world/shell/' > /dev/null\n"
    "  x=$(($count * 3 + 7))\n"
    "  count=$((count + 1))\n"
    "done\n";

static int script_shell_init(void **state) {
    script_shell_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    /* Write script to temp file */
    s->script_path = strdup("/tmp/ssb_shell_test.sh");
    FILE *f = fopen(s->script_path, "w");
    if (!f) { free(s); return -1; }
    fprintf(f, "%s", shell_script);
    fclose(f);
    chmod(s->script_path, 0755);
    *state = s;
    return 0;
}

static int script_shell_warmup(void *state) {
    script_shell_state_t *s = (script_shell_state_t *)state;
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", s->script_path, NULL);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return 0;
}

static int script_shell_measure(void *state, measurement_t *result) {
    script_shell_state_t *s = (script_shell_state_t *)state;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < SCRIPT_ITERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("/bin/sh", "sh", s->script_path, NULL);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)SCRIPT_ITERS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int script_shell_cleanup(void *state) {
    script_shell_state_t *s = (script_shell_state_t *)state;
    unlink(s->script_path);
    free(s->script_path);
    free(s);
    return 0;
}

benchmark_t bench_script_shell = {
    .name = "script-shell",
    .category = "C9",
    .description = "Shell script throughput (UnixBench Shell Scripts exact equivalent)",
    .tier = 1,
    .primary_metric_name = "scripts/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = script_shell_init,
    .warmup = script_shell_warmup,
    .measure = script_shell_measure,
    .cleanup = script_shell_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_script_shell);
