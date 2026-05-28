#ifndef SSB_SYSTEM_H
#define SSB_SYSTEM_H
#include "servsysbench.h"
#include <stdio.h>

typedef struct { int id, cpu_count, *cpu_list; size_t memory_kb; int distance[16]; } numa_node_t;
typedef struct { int level; const char *type; size_t size_kb; int line_size, associativity; } cache_info_t;
typedef struct { const char *name, *status; bool affected, mitigated; } mitigation_t;

struct system_info_s {
    char cpu_model[256], cpu_isa[64];
    int cpu_cores_physical, cpu_threads_logical;
    bool smt_enabled; int smt_siblings_per_core;
    double freq_base_mhz, freq_max_mhz, freq_current_mhz[256];
    char governor[32];
    size_t total_ram_kb, page_size;
    bool hugepages_2m_available, hugepages_1g_available;
    int numa_node_count; numa_node_t *numa_nodes;
    int cache_level_count; cache_info_t *caches;
    char kernel_version[128], kernel_cmdline[1024], os_distro[128], libc_version[64];
    int mitigation_count; mitigation_t *mitigations;
    int storage_device_count;
    struct { char device[64], model[128], device_class[32]; size_t capacity_gb; } *storage_devices;
    int network_iface_count;
    struct { char name[32], driver[64], speed[32]; bool offloads_enabled; } *network_ifaces;
    const char *compiler_name, *compiler_version, *compiler_flags;
};

typedef struct { int64_t interrupts_total, context_switches; double temperature_c[16]; int temperature_count; } noise_snapshot_t;

int system_probe(system_info_t **info);
void system_free(system_info_t *info);
int system_lock_frequency(void);
int system_lock_frequency_peak(void);
int system_restore_frequency(void);
int system_read_proc_int(const char *path, int64_t *value);
int system_read_proc_str(const char *path, char *buf, size_t len);
int system_noise_snapshot(noise_snapshot_t *snap);
int system_noise_delta(const noise_snapshot_t *before, const noise_snapshot_t *after, noise_snapshot_t *delta);
int system_isolate_cpus(int housekeeping_cpu);
int system_get_frequencies(double *freqs, int max_cpus);
int system_get_temperatures(double *temps, int max_zones);
int system_detect_virtualization(char *hypervisor, size_t len, int *confidence);
#endif
int validate_system(FILE *out);
