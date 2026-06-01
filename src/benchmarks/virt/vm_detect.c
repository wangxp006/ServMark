#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define DETECT_ITERS 1000000

/* Architecture-neutral. x86 uses CPUID, others use DMI sysfs + fallback. */
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

typedef struct {
    int dummy;
} vm_detect_state_t;

static const char *detect_hypervisor(void) {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(0x40000000, &eax, &ebx, &ecx, &edx)) {
        uint32_t r[3] = {ebx, ecx, edx};
        if (r[0]==0x4B4D564B && r[1]==0x564B4D56 && r[2]==0x0000004B) return "KVM";
        if (r[0]==0x61774D56 && r[1]==0x4D566572 && r[2]==0x65726177) return "VMware";
        if (r[0]==0x7263694D && r[1]==0x666F736F && r[2]==0x76482074) return "Hyper-V";
        if (r[0]==0x6E655800 && r[1]==0x4D4D5665 && r[2]==0x65586E00) return "Xen";
        return "unknown-hv";
    }
#endif
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
    *state = s;
    return 0;
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
    volatile int sink = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

#if defined(__x86_64__) || defined(__i386__)
    for (int i = 0; i < DETECT_ITERS; i++) {
        unsigned int eax, ebx, ecx, edx;
        __get_cpuid(0x40000000, &eax, &ebx, &ecx, &edx);
        sink += eax;
    }
#else
    /* ARM64/RISC-V: measure getpid() as portable VM-tax indicator */
    for (int i = 0; i < DETECT_ITERS; i++)
        sink += getpid();
#endif

    clock_gettime(CLOCK_MONOTONIC, &t1);
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = elapsed * 1e9 / DETECT_ITERS;
    result->wall_seconds = elapsed;
    return 0;
}

static int vm_detect_cleanup(void *state) {
    free(state);
    return 0;
}

benchmark_t bench_vm_detect = {
    .name = "vm-detect",
    .category = "C14",
    .description = "VM detection + hypervisor leaf call cost (arch-neutral)",
    .tier = 1,
    .primary_metric_name = "ns/call",
    .higher_is_better = false,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = false,
    .init = vm_detect_init,
    .warmup = vm_detect_warmup,
    .measure = vm_detect_measure,
    .cleanup = vm_detect_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_vm_detect);
