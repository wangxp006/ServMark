#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define DETECT_ITERS 1000000

/*
 * VM detection via DMI sysfs (ARCH-NEUTRAL).
 *
 * Measures the cost of probing /sys/class/dmi/id/product_name to
 * detect hypervisor presence. Uses only sysfs DMI — no x86 CPUID,
 * no architecture-specific instructions. Same code path on all
 * architectures (x86, ARM64, RISC-V).
 *
 * This is a VFS/dentry-cache microbenchmark, not a true VM-exit
 * measurement. For real VM-exit latency see hardware-specific tools.
 */

typedef struct { int dummy; } vm_detect_state_t;

static const char *detect_hypervisor(void) {
    FILE *f = fopen("/sys/class/dmi/id/product_name", "r");
    if (f) {
        char buf[256] = {0};
        if (fgets(buf, sizeof(buf), f)) {
            fclose(f);
            if (strstr(buf, "KVM") || strstr(buf, "kvm")) return "KVM";
            if (strstr(buf, "VMware")) return "VMware";
            if (strstr(buf, "VirtualBox")) return "VirtualBox";
        } else fclose(f);
    }
    return "bare-metal";
}

static int vm_detect_init(void **state) {
    vm_detect_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    *state = s; return 0;
}

static int vm_detect_warmup(void *state) {
    (void)state;
    volatile const char *hv = detect_hypervisor();
    __asm__ __volatile__("" : "+r"(hv));
    return 0;
}

static int vm_detect_measure(void *state, measurement_t *result) {
    (void)state;
    struct timespec t0, t1;
    volatile const char *hv_result = NULL;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < DETECT_ITERS; i++) {
        /* DMI sysfs probe — measures VFS dentry-cache latency.
         * Architecture-neutral: no CPUID, same syscall path everywhere. */
        hv_result = detect_hypervisor();
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(hv_result));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec)/1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e9 / DETECT_ITERS; /* ns/call */
    result->wall_seconds = elapsed;
    return 0;
}

static int vm_detect_cleanup(void *state) { free(state); return 0; }

benchmark_t bench_vm_detect = {
    .name = "vm-detect", .category = "C14",
    .description = "VM detect via DMI sysfs probe (arch-neutral, VFS dentry-cache latency)",
    .tier = 1, .primary_metric_name = "ns/call", .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS, .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC, .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = vm_detect_init, .warmup = vm_detect_warmup,
    .measure = vm_detect_measure, .cleanup = vm_detect_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_vm_detect);
