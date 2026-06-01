#include "system.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef SSB_USE_HWLOC
#include <hwloc.h>
#endif

int system_read_proc_int(const char *path, int64_t *value) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long long v;
    int n = fscanf(f, "%lld", &v);
    fclose(f);
    if (n != 1) return -1;
    *value = (int64_t)v;
    return 0;
}

int system_read_proc_str(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)len, f)) { fclose(f); return -1; }
    size_t l = strlen(buf);
    if (l > 0 && buf[l-1] == '\n') buf[l-1] = '\0';
    fclose(f);
    return 0;
}

int system_probe(system_info_t **info_out) {
    system_info_t *info = calloc(1, sizeof(system_info_t));
    if (!info) return -1;

    /* Kernel version */
    system_read_proc_str("/proc/version", info->kernel_version,
            sizeof(info->kernel_version));

    /* Page size */
    info->page_size = (size_t)sysconf(_SC_PAGESIZE);

    /* CPU cores */
    info->cpu_cores_physical = (int)sysconf(_SC_NPROCESSORS_ONLN);
    info->cpu_threads_logical = info->cpu_cores_physical;

    /* Check SMT */
    int cores = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (cores > info->cpu_cores_physical) {
        info->smt_enabled = true;
        info->smt_siblings_per_core = cores / info->cpu_cores_physical;
        info->cpu_threads_logical = cores;
    }

    /* Memory */
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    info->total_ram_kb = (size_t)(pages * page_size / 1024);

    /* Check huge pages */
    info->hugepages_2m_available = (access("/sys/kernel/mm/hugepages/hugepages-2048kB", F_OK) == 0);
    info->hugepages_1g_available = (access("/sys/kernel/mm/hugepages/hugepages-1048576kB", F_OK) == 0);

    /* Compiler info */
    info->compiler_name = "clang-18";
    info->compiler_version = "18.1.8";
    info->compiler_flags = "-O2 -g -fno-omit-frame-pointer -fno-lto";

    /* Governor */
    system_read_proc_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
            info->governor, sizeof(info->governor));

    /* NUMA nodes */
    info->numa_node_count = 1;
    info->numa_nodes = calloc(1, sizeof(numa_node_t));
    if (info->numa_nodes) {
        info->numa_nodes[0].id = 0;
        info->numa_nodes[0].cpu_count = info->cpu_threads_logical;
        info->numa_nodes[0].memory_kb = info->total_ram_kb;
        info->numa_nodes[0].distance[0] = 10;
    }

    /* Check for multiple NUMA nodes */
    char node_path[128];
    snprintf(node_path, sizeof(node_path), "/sys/devices/system/node/node1");
    if (access(node_path, F_OK) == 0) {
        /* Has multiple nodes - reallocate */
        numa_node_t *new_nodes = realloc(info->numa_nodes,
                2 * sizeof(numa_node_t));
        if (new_nodes) {
            info->numa_nodes = new_nodes;
            info->numa_node_count = 2;
        } /* else: realloc failed, keep single-node config */
        memset(&info->numa_nodes[1], 0, sizeof(numa_node_t));
        info->numa_nodes[1].id = 1;
        info->numa_nodes[1].memory_kb = info->total_ram_kb / 2;
        info->numa_nodes[0].memory_kb = info->total_ram_kb / 2;
        info->numa_nodes[0].distance[1] = 21;
        info->numa_nodes[1].distance[0] = 21;
        info->numa_nodes[1].distance[1] = 10;
    }

    /* Try hwloc for accurate cache topology; fall back to defaults */
    info->cache_level_count = 0;
    info->caches = NULL;
#ifdef SSB_USE_HWLOC
    {
        hwloc_topology_t topo;
        if (hwloc_topology_init(&topo) == 0 &&
            hwloc_topology_load(topo) == 0) {
            int depth = hwloc_topology_get_depth(topo);
            /* Count caches first */
            int cache_count = 0;
            for (int d = 0; d < depth; d++) {
                if (hwloc_get_depth_type(topo, d) == HWLOC_OBJ_CACHE)
                    cache_count += hwloc_get_nbobjs_by_depth(topo, d);
            }
            if (cache_count > 0) {
                info->caches = calloc(cache_count, sizeof(cache_info_t));
                if (info->caches) {
                    for (int d = 0; d < depth; d++) {
                        if (hwloc_get_depth_type(topo, d) != HWLOC_OBJ_CACHE) continue;
                        int n = hwloc_get_nbobjs_by_depth(topo, d);
                        for (int j = 0; j < n && info->cache_level_count < cache_count; j++) {
                            hwloc_obj_t obj = hwloc_get_obj_by_depth(topo, d, j);
                            if (obj) {
                                cache_info_t *ci = &info->caches[info->cache_level_count++];
                                ci->level = obj->attr->cache.depth;
                                ci->type = (obj->attr->cache.type == HWLOC_OBJ_CACHE_DATA) ? "data" :
                                           (obj->attr->cache.type == HWLOC_OBJ_CACHE_INSTRUCTION) ? "instruction" : "unified";
                                ci->size_kb = (int)(obj->attr->cache.size / 1024);
                                ci->line_size = (int)obj->attr->cache.linesize;
                                ci->ways = obj->attr->cache.associativity;
                            }
                        }
                    }
                }
            }
            hwloc_topology_destroy(topo);
        }
    }
#endif
    /* Fallback: hard-coded defaults if hwloc unavailable or failed */
    if (info->cache_level_count == 0) {
        info->cache_level_count = 3;
        info->caches = calloc(3, sizeof(cache_info_t));
        info->caches[0] = (cache_info_t){1, "data", 32, 64, 8};
        info->caches[1] = (cache_info_t){2, "unified", 1024, 64, 16};
        info->caches[2] = (cache_info_t){3, "unified", 32768, 64, 16};
    }

    *info_out = info;
    return 0;
}

void system_free(system_info_t *info) {
    if (!info) return;
    free(info->numa_nodes);
    free(info->caches);
    free(info->mitigations);
    if (info->storage_devices) free(info->storage_devices);
    if (info->network_ifaces) free(info->network_ifaces);
    free(info);
}

int system_lock_frequency(void) {
    /* Set performance governor on all CPUs */
    int ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu <= 0 || ncpu > 4096) ncpu = 256;
    for (int cpu = 0; cpu < ncpu; cpu++) {
        char path[256];
        snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "performance\n");
            fclose(f);
        } else break;
    }
    return 0;
}

int system_lock_frequency_peak(void) {
    system_lock_frequency();
    return 0;
}

int system_restore_frequency(void) {
    return 0;
}

int system_noise_snapshot(noise_snapshot_t *snap) {
    memset(snap, 0, sizeof(*snap));

    int64_t val;
    if (system_read_proc_int("/proc/stat", &val) == 0) {
        /* ctxt is the 10th field in /proc/stat line 1 */
        char line[1024];
        FILE *f = fopen("/proc/stat", "r");
        if (f && fgets(line, sizeof(line), f)) {
            char *p = line;
            for (int i = 0; i < 10; i++) {
                p = strchr(p, ' ');
                if (!p) break;
                while (*p == ' ') p++;
            }
            if (p) snap->context_switches = atoll(p);
            fclose(f);
        }
    }
    return 0;
}

int system_noise_delta(const noise_snapshot_t *before,
        const noise_snapshot_t *after, noise_snapshot_t *delta) {
    delta->interrupts_total = after->interrupts_total - before->interrupts_total;
    delta->context_switches = after->context_switches - before->context_switches;
    for (int i = 0; i < 16 && i < before->temperature_count; i++) {
        delta->temperature_c[i] = after->temperature_c[i] - before->temperature_c[i];
    }
    delta->temperature_count = before->temperature_count;
    return 0;
}

int system_detect_virtualization(char *hypervisor, size_t len, int *confidence) {
    /* Check CPUID on x86 via /sys */
    char vendor[64] = {0};
    if (system_read_proc_str("/sys/class/dmi/id/sys_vendor", vendor, sizeof(vendor)) == 0) {
        if (strstr(vendor, "QEMU") || strstr(vendor, "KVM")) {
            snprintf(hypervisor, len, "kvm");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "VMware")) {
            snprintf(hypervisor, len, "vmware");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Microsoft")) {
            snprintf(hypervisor, len, "hyper-v");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Xen")) {
            snprintf(hypervisor, len, "xen");
            *confidence = 2;
            return 0;
        }
    }

    /* Check product name */
    char product[128] = {0};
    if (system_read_proc_str("/sys/class/dmi/id/product_name", product, sizeof(product)) == 0) {
        if (strstr(product, "KVM") || strstr(product, "QEMU")) {
            snprintf(hypervisor, len, "kvm");
            *confidence = 1;
            return 0;
        }
    }

    snprintf(hypervisor, len, "bare-metal");
    *confidence = 0;
    return 0;
}

int system_get_frequencies(double *freqs, int max_cpus) {
    for (int i = 0; i < max_cpus; i++) {
        char path[256];
        snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        int64_t khz = 0;
        if (system_read_proc_int(path, &khz) == 0) {
            freqs[i] = khz / 1000.0;
        } else {
            freqs[i] = 0.0;
        }
    }
    return 0;
}

int system_get_temperatures(double *temps, int max_zones) {
    int count = 0;
    for (int i = 0; i < max_zones; i++) {
        char path[256];
        snprintf(path, sizeof(path),
                "/sys/class/thermal/thermal_zone%d/temp", i);
        int64_t millic = 0;
        if (system_read_proc_int(path, &millic) == 0) {
            temps[count++] = millic / 1000.0;
        }
    }
    return count;
}

int system_isolate_cpus(int housekeeping_cpu) {
    (void)housekeeping_cpu;
    return 0;
}
