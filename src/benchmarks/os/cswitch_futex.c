#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SWITCHES 1000000
#define NUM_THREADS 2

/*
 * Futex-based context switching benchmark.
 *
 * Two threads ping-pong via futex wait/wake. Correct handoff protocol:
 * each side toggles futex_word so the other side actually sleeps.
 *
 * Futex syscalls provide full kernel-side memory barriers, so we use
 * relaxed ordering for futex_word stores — the syscall itself orders.
 * Counter uses relaxed: no cross-thread ordering needed, only counts.
 */

typedef struct {
    _Atomic int futex_word;
    _Atomic int ready;
    _Atomic int64_t switches;
    pthread_t thread;
} cswitch_futex_state_t;

static void *futex_thread(void *arg) {
    cswitch_futex_state_t *s = (cswitch_futex_state_t *)arg;
    atomic_store_explicit(&s->ready, 1, memory_order_release);
    for (int i = 0; i < SWITCHES / 2; i++) {
        syscall(SYS_futex, &s->futex_word, FUTEX_WAIT, 0, NULL, NULL, 0);
        atomic_store_explicit(&s->futex_word, 0, memory_order_relaxed);
        syscall(SYS_futex, &s->futex_word, FUTEX_WAKE, 1, NULL, NULL, 0);
        atomic_fetch_add_explicit(&s->switches, 2, memory_order_relaxed);
    }
    return NULL;
}

static int cswitch_futex_init(void **state) {
    cswitch_futex_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    atomic_init(&s->futex_word, 0);
    *state = s;
    return 0;
}

static int cswitch_futex_warmup(void *state) {
    cswitch_futex_state_t *s = (cswitch_futex_state_t *)state;
    atomic_store_explicit(&s->futex_word, 1, memory_order_relaxed);
    syscall(SYS_futex, &s->futex_word, FUTEX_WAKE, 1, NULL, NULL, 0);
    return 0;
}

static int cswitch_futex_measure(void *state, measurement_t *result) {
    cswitch_futex_state_t *s = (cswitch_futex_state_t *)state;
    struct timespec t0, t1;
    int ret;

    atomic_store_explicit(&s->ready, 0, memory_order_relaxed);
    atomic_store_explicit(&s->switches, 0, memory_order_relaxed);
    atomic_store_explicit(&s->futex_word, 0, memory_order_relaxed);

    ret = pthread_create(&s->thread, NULL, futex_thread, s);
    if (ret != 0) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    while (!atomic_load_explicit(&s->ready, memory_order_acquire))
        ;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < SWITCHES / 2; i++) {
        atomic_store_explicit(&s->futex_word, 1, memory_order_relaxed);
        syscall(SYS_futex, &s->futex_word, FUTEX_WAKE, 1, NULL, NULL, 0);
        syscall(SYS_futex, &s->futex_word, FUTEX_WAIT, 1, NULL, NULL, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    pthread_join(s->thread, NULL);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)atomic_load_explicit(&s->switches, memory_order_relaxed) / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int cswitch_futex_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_cswitch_futex = {
    .name = "cswitch-futex",
    .category = "C8",
    .description = "Futex-based context switching (futex wait/wake ping-pong)",
    .tier = 1, .primary_metric_name = "switches/sec", .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS, .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC, .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = cswitch_futex_init, .warmup = cswitch_futex_warmup,
    .measure = cswitch_futex_measure, .cleanup = cswitch_futex_cleanup,
    .num_threads = NUM_THREADS,
};
SSB_BENCHMARK_REGISTER(bench_cswitch_futex);
