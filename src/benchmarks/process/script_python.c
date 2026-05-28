#include "servsysbench/benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

#define PYTHON_ITERS 50

typedef struct {
    char *script_path;
} script_python_state_t;

static const char *python_script =
    "import json\n"
    "import sys\n"
    "data = {}\n"
    "for i in range(200):\n"
    "    data[f'key_{i}'] = {'val': i * i, 'name': f'item_{i}', 'tags': [1,2,3]}\n"
    "s = json.dumps(data)\n"
    "d = json.loads(s)\n"
    "total = sum(v['val'] for v in d.values())\n"
    "print(total, file=sys.stdout)\n";

static int script_python_init(void **state) {
    script_python_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->script_path = strdup("/tmp/ssb_python_test.py");
    FILE *f = fopen(s->script_path, "w");
    if (!f) { free(s); return -1; }
    fprintf(f, "%s", python_script);
    fclose(f);
    *state = s;
    return 0;
}

static int script_python_warmup(void *state) {
    script_python_state_t *s = (script_python_state_t *)state;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("python3", "python3", s->script_path, NULL);
        execlp("python", "python", s->script_path, NULL);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return 0;
}

static int script_python_measure(void *state, measurement_t *result) {
    script_python_state_t *s = (script_python_state_t *)state;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < PYTHON_ITERS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("python3", "python3", s->script_path, NULL);
            execlp("python", "python", s->script_path, NULL);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)PYTHON_ITERS / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int script_python_cleanup(void *state) {
    script_python_state_t *s = (script_python_state_t *)state;
    unlink(s->script_path);
    free(s->script_path);
    free(s);
    return 0;
}

benchmark_t bench_script_python = {
    .name = "script-python",
    .category = "C9",
    .description = "Python script throughput (JSON serialize + dict ops)",
    .tier = 1,
    .primary_metric_name = "scripts/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = script_python_init,
    .warmup = script_python_warmup,
    .measure = script_python_measure,
    .cleanup = script_python_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_script_python);
