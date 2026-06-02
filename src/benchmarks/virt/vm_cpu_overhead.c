#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * CPU compute throughput benchmark (Sieve of Eratosthenes).
 * Fixed-work compute test useful for bare-metal vs VM comparison.
 * Uses no virtualization-specific code — measures raw CPU throughput.
 */
#define SIEVE_LIMIT 2000000
#define SIEVE_PASSES 10

typedef struct { char *is_prime; } vm_cpu_overhead_state_t;

static int sieve(char *ip, int lim) {
    memset(ip, 1, lim+1); ip[0]=ip[1]=0;
    int c=0;
    for (int p=2; p*p<=lim; p++)
        if (ip[p])
            for (int i=p*p; i<=lim; i+=p) ip[i]=0;
    for (int i=2; i<=lim; i++) if (ip[i]) c++;
    return c;
}

static int vm_cpu_overhead_init(void **state) {
    vm_cpu_overhead_state_t *s = calloc(1,sizeof(*s));
    if (!s) return -1;
    s->is_prime = malloc(SIEVE_LIMIT+1);
    if (!s->is_prime) { free(s); return -1; }
    *state=s; return 0;
}

static int vm_cpu_overhead_warmup(void *state) {
    vm_cpu_overhead_state_t *s = (vm_cpu_overhead_state_t *)state;
    volatile int r = sieve(s->is_prime, SIEVE_LIMIT/4);
    __asm__ __volatile__("" : "+r"(r)); return 0;
}

static int vm_cpu_overhead_measure(void *state, measurement_t *result) {
    vm_cpu_overhead_state_t *s = (vm_cpu_overhead_state_t *)state;
    struct timespec t0, t1;
    volatile int tp = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int pass=0; pass<SIEVE_PASSES; pass++)
        tp += sieve(s->is_prime, SIEVE_LIMIT);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(tp));
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result,0,sizeof(*result));
    result->primary_metric = (double)SIEVE_PASSES / el;
    result->wall_seconds = el;
    return 0;
}

static int vm_cpu_overhead_cleanup(void *state) {
    vm_cpu_overhead_state_t *s = (vm_cpu_overhead_state_t *)state;
    free(s->is_prime); free(s); return 0;
}

benchmark_t bench_vm_cpu_overhead = {
    .name="vm-cpu-overhead", .category="C14",
    .description="CPU compute throughput (Sieve of Eratosthenes, bare-metal vs VM baseline)",
    .tier=1, .primary_metric_name="passes/sec", .higher_is_better=true,
    .min_iterations=SSB_MIN_ITERATIONS, .max_iterations=SSB_MAX_ITERATIONS,
    .convergence_target=SSB_CONVERGENCE_TARGET,
    .min_runtime_sec=SSB_MIN_RUNTIME_SEC, .max_runtime_sec=SSB_MAX_RUNTIME_SEC,
    .cooldown_required=true,
    .init=vm_cpu_overhead_init, .warmup=vm_cpu_overhead_warmup,
    .measure=vm_cpu_overhead_measure, .cleanup=vm_cpu_overhead_cleanup,
    .num_threads=1,
};
SSB_BENCHMARK_REGISTER(bench_vm_cpu_overhead);
