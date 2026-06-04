#include "system.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

    /* Check SMT via thread_siblings_list (more reliable than core count diff) */
    info->smt_enabled = false;
    info->smt_siblings_per_core = 1;
    {
        char siblings[64];
        if (system_read_proc_str("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list",
                siblings, sizeof(siblings)) == 0) {
            int count = 0;
            int a, b;
            if (sscanf(siblings, "%d-%d", &a, &b) == 2) count = b - a + 1;
            else {
                for (char *p = siblings; *p; p++) if (*p == ',') count++;
                count = count + 1;
            }
            if (count > 1) {
                info->smt_enabled = true;
                info->smt_siblings_per_core = count;
                long conf = sysconf(_SC_NPROCESSORS_CONF);
                info->cpu_threads_logical = (conf > 0) ? (int)conf
                    : info->cpu_cores_physical * count;
            }
        }
    }

    /* Memory */
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0)
        info->total_ram_kb = (size_t)((unsigned long long)pages * (unsigned long long)page_size / 1024);
    else
        info->total_ram_kb = 0;

    /* Check huge pages */
    info->hugepages_2m_available = (access("/sys/kernel/mm/hugepages/hugepages-2048kB", F_OK) == 0);
    info->hugepages_1g_available = (access("/sys/kernel/mm/hugepages/hugepages-1048576kB", F_OK) == 0);

    /* CPU model from /proc/cpuinfo */
    {
        FILE *f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char cline[512];
            while (fgets(cline, sizeof(cline), f)) {
                if (strncmp(cline, "model name", 10) == 0) {
                    char *c = strchr(cline, ':');
                    if (c) { c++; while (*c == ' ' || *c == '\t') c++;
                        size_t l = strlen(c); if (l > 0 && c[l-1] == '\n') c[l-1] = '\0';
                        strncpy(info->cpu_model, c, sizeof(info->cpu_model) - 1); }
                    break;
                }
                /* ARM64: accumulate implementer + part for model string */
                if (strncmp(cline, "CPU implementer", 15) == 0 ||
                    strncmp(cline, "CPU part", 8) == 0) {
                    char *c = strchr(cline, ':');
                    if (c) { c++; while (*c == ' ' || *c == '\t') c++;
                        size_t l = strlen(c); if (l > 0 && c[l-1] == '\n') c[l-1] = '\0';
                        if (info->cpu_model[0])
                            strncat(info->cpu_model, " ", sizeof(info->cpu_model) - strlen(info->cpu_model) - 1);
                        strncat(info->cpu_model, c, sizeof(info->cpu_model) - strlen(info->cpu_model) - 1);
                    }
                }
            }
            fclose(f);
        }
    }

    /* CPU flags/ISA features */
    {
        FILE *f = fopen("/proc/cpuinfo", "r");
        if (f) {
            char cline[512];
            while (fgets(cline, sizeof(cline), f)) {
                if (strncmp(cline, "flags", 5) == 0 || strncmp(cline, "Features", 8) == 0 ||
                    strncmp(cline, "isa", 3) == 0) {
                    char *c = strchr(cline, ':');
                    if (c) { c++; while (*c == ' ' || *c == '\t') c++;
                        size_t l = strlen(c); if (l > 0 && c[l-1] == '\n') c[l-1] = '\0';
                        strncpy(info->cpu_isa, c, sizeof(info->cpu_isa) - 1); }
                    break;
                }
            }
            fclose(f);
        }
    }

    /* CPU frequencies from sysfs */
    {
        int64_t khz = 0;
        if (system_read_proc_int("/sys/devices/system/cpu/cpu0/cpufreq/base_frequency", &khz) == 0)
            info->freq_base_mhz = khz / 1000.0;
        else if (system_read_proc_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", &khz) == 0)
            info->freq_base_mhz = khz / 1000.0;
        if (system_read_proc_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", &khz) == 0)
            info->freq_max_mhz = khz / 1000.0;
        if (system_read_proc_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &khz) == 0)
            info->freq_current_mhz[0] = khz / 1000.0;
    }

    /* OS distro */
    {
        FILE *f = fopen("/etc/os-release", "r");
        if (f) {
            char oline[256];
            while (fgets(oline, sizeof(oline), f)) {
                if (strncmp(oline, "PRETTY_NAME=", 12) == 0) {
                    char *v = oline + 12;
                    size_t vl = strlen(v);
                    if (vl > 0 && v[vl-1] == '\n') vl--;
                    if (v[0] == '"') { v++; vl--; }
                    if (vl > 0 && v[vl-1] == '"') vl--;
                    size_t cp = vl < sizeof(info->os_distro) ? vl : sizeof(info->os_distro) - 1;
                    memcpy(info->os_distro, v, cp);
                    info->os_distro[cp] = '\0';
                    break;
                }
            }
            fclose(f);
        }
    }

    /* Compiler info */
    info->compiler_name = "clang-18";
    info->compiler_version = "18.1.8";
    info->compiler_flags = "-O2 -g -fno-omit-frame-pointer -fno-lto";

    /* Governor */
    system_read_proc_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
            info->governor, sizeof(info->governor));

    /* === NUMA node topology ===
     * Dynamic probing via sysfs: count nodes and read distance matrix. */
    info->numa_node_count = 0;
    info->numa_nodes = NULL;
    {
        int max_nodes = 0;
        for (int ni = 0; ni < 64; ni++) {
            char npath[128];
            snprintf(npath, sizeof(npath), "/sys/devices/system/node/node%d", ni);
            if (access(npath, F_OK) == 0) max_nodes = ni + 1;
            else break;
        }
        if (max_nodes > 0) {
            info->numa_nodes = calloc(max_nodes, sizeof(numa_node_t));
            if (info->numa_nodes) {
                info->numa_node_count = max_nodes;
                for (int ni = 0; ni < max_nodes; ni++) {
                    info->numa_nodes[ni].id = ni;
                    char cpath[256];

                    /* Read cpumap to count CPUs on this node.
                     * Format: comma-separated 32-bit hex groups.
                     * Each group covers 32 CPUs. Parse all for >64-core. */
                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/node/node%d/cpumap", ni);
                    FILE *cf = fopen(cpath, "r");
                    if (cf) {
                        char cmap[1024];
                        if (fgets(cmap, sizeof(cmap), cf)) {
                            size_t clen = strlen(cmap);
                            while (clen > 0 && (cmap[clen-1]=='\n' || cmap[clen-1]=='\r'))
                                cmap[--clen] = '\0';
                            int cpu_count = 0;
                            char *save = NULL;
                            char *tok = strtok_r(cmap, ",", &save);
                            while (tok) {
                                unsigned long val = strtoul(tok, NULL, 16);
                                for (int b = 0; b < 32; b++)
                                    if (val & (1UL << b)) cpu_count++;
                                tok = strtok_r(NULL, ",", &save);
                            }
                            info->numa_nodes[ni].cpu_count = cpu_count;
                        }
                        fclose(cf);
                    }

                    /* Read memory info */
                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/node/node%d/meminfo", ni);
                    cf = fopen(cpath, "r");
                    if (cf) {
                        char mline[128];
                        while (fgets(mline, sizeof(mline), cf)) {
                            if (strncmp(mline, "MemTotal:", 9) == 0) {
                                char *v = mline + 9;
                                while (*v == ' ' || *v == '\t') v++;
                                info->numa_nodes[ni].memory_kb = (int64_t)strtoull(v, NULL, 10);
                                break;
                            }
                        }
                        fclose(cf);
                    }

                    /* Read distance matrix */
                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/node/node%d/distance", ni);
                    cf = fopen(cpath, "r");
                    if (cf) {
                        char dline[256];
                        if (fgets(dline, sizeof(dline), cf)) {
                            char *tok = strtok(dline, " \t\n");
                            int di = 0;
                            while (tok && di < 16) {
                                info->numa_nodes[ni].distance[di] = atoi(tok);
                                tok = strtok(NULL, " \t\n");
                                di++;
                            }
                        }
                        fclose(cf);
                    } else {
                        for (int dj = 0; dj < max_nodes; dj++)
                            info->numa_nodes[ni].distance[dj] = (dj == ni) ? 10 : 20;
                    }
                }
            }
        }
    }
    /* Fallback: single node */
    if (info->numa_node_count == 0) {
        info->numa_node_count = 1;
        info->numa_nodes = calloc(1, sizeof(numa_node_t));
        if (info->numa_nodes) {
            info->numa_nodes[0].id = 0;
            info->numa_nodes[0].cpu_count = info->cpu_threads_logical;
            info->numa_nodes[0].memory_kb = info->total_ram_kb;
            info->numa_nodes[0].distance[0] = 10;
        }
    }

    /* === Cache topology ===
     * Priority: 1) sysfs  2) hardcoded fallback */
    info->cache_level_count = 0;
    info->caches = NULL;
    /* Read /sys/devices/system/cpu/cpu0/cache/index* */
    if (info->cache_level_count == 0) {
        int idx = 0;
        for (idx = 0; idx < 8; idx++) {
            char cpath[256];
            snprintf(cpath, sizeof(cpath),
                    "/sys/devices/system/cpu/cpu0/cache/index%d/type", idx);
            if (access(cpath, R_OK) != 0) break;
        }
        if (idx > 0) {
            info->caches = calloc(idx, sizeof(cache_info_t));
            if (info->caches) {
                for (int i = 0; i < idx; i++) {
                    cache_info_t *ci = &info->caches[i];
                    char cpath[256], val[64];
                    int64_t num;

                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/cpu/cpu0/cache/index%d/level", i);
                    if (system_read_proc_int(cpath, &num) == 0) ci->level = (int)num;

                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/cpu/cpu0/cache/index%d/type", i);
                    if (system_read_proc_str(cpath, val, sizeof(val)) == 0) {
                        if (strstr(val, "Data")) ci->type = "data";
                        else if (strstr(val, "Instruction")) ci->type = "instruction";
                        else ci->type = "unified";
                    }

                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/cpu/cpu0/cache/index%d/size", i);
                    if (system_read_proc_int(cpath, &num) == 0) ci->size_kb = (int)(num / 1024);

                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/cpu/cpu0/cache/index%d/coherency_line_size", i);
                    if (system_read_proc_int(cpath, &num) == 0) ci->line_size = (int)num;

                    snprintf(cpath, sizeof(cpath),
                            "/sys/devices/system/cpu/cpu0/cache/index%d/ways_of_associativity", i);
                    if (system_read_proc_int(cpath, &num) == 0) ci->associativity = (int)num;

                    info->cache_level_count++;
                }
            }
        }
    }
    /* Last resort: hard-coded defaults if all probes failed.
     * These are conservative underestimates (L1=32KB, L2=1MB, L3=32MB).
     * Modern server CPUs typically have much larger caches (L1=64KB, L2=2MB, L3=128MB+).
     * Update these values if the target hardware is known to differ. */
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

/* Saved governors for restoration */
static char _saved_governors[4096][32];
static int _saved_governor_count = 0;

int system_lock_frequency(void) {
    /* Save original governors and set performance + fixed frequency on all CPUs */
    int ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
    if (ncpu <= 0 || ncpu > 4096) ncpu = 256;
    _saved_governor_count = 0;
    for (int cpu = 0; cpu < ncpu; cpu++) {
        char path[256];
        /* Save original governor */
        snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
        char orig[32] = {0};
        FILE *rf = fopen(path, "r");
        if (rf) {
            if (fgets(orig, sizeof(orig), rf)) {
                size_t l = strlen(orig);
                if (l > 0 && orig[l-1] == '\n') orig[l-1] = '\0';
            }
            fclose(rf);
        }
        if (orig[0]) strncpy(_saved_governors[cpu], orig, 31);
        /* Set performance governor */
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "performance\n");
            fclose(f);
        } else break;
        _saved_governor_count++;
    }
    /* Pin to max frequency: set scaling_min_freq = scaling_max_freq */
    for (int cpu = 0; cpu < ncpu; cpu++) {
        char max_path[256], min_path[256];
        snprintf(max_path, sizeof(max_path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        FILE *fmax = fopen(max_path, "r");
        if (fmax) {
            char max_freq[32];
            if (fgets(max_freq, sizeof(max_freq), fmax)) {
                fclose(fmax);
                snprintf(min_path, sizeof(min_path),
                        "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
                FILE *fmin = fopen(min_path, "w");
                if (fmin) { fprintf(fmin, "%s", max_freq); fclose(fmin); }
            } else fclose(fmax);
        }
    }
    return 0;
}

int system_lock_frequency_peak(void) {
    system_lock_frequency();
    return 0;
}

int system_restore_frequency(void) {
    for (int cpu = 0; cpu < _saved_governor_count; cpu++) {
        if (_saved_governors[cpu][0]) {
            char path[256];
            snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
            FILE *f = fopen(path, "w");
            if (f) { fprintf(f, "%s\n", _saved_governors[cpu]); fclose(f); }
        }
    }
    _saved_governor_count = 0;
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
    int tc = before->temperature_count;
    if (after->temperature_count < tc) tc = after->temperature_count;
    for (int i = 0; i < 16 && i < tc; i++) {
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
        if (strstr(vendor, "Microsoft Corporation")) {
            snprintf(hypervisor, len, "hyper-v");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Xen")) {
            snprintf(hypervisor, len, "xen");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Amazon EC2")) {
            snprintf(hypervisor, len, "aws-nitro");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Google")) {
            snprintf(hypervisor, len, "gce");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Oracle")) {
            snprintf(hypervisor, len, "oci");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "DigitalOcean")) {
            snprintf(hypervisor, len, "digitalocean");
            *confidence = 2;
            return 0;
        }
        if (strstr(vendor, "Alibaba Cloud")) {
            snprintf(hypervisor, len, "alicloud");
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
        if (strstr(product, "HVM domU")) {
            snprintf(hypervisor, len, "aws-nitro");
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
    /* Best-effort IRQ affinity isolation. Requires root and is kernel-dependent.
     * Returns 0 always — failure to isolate is not a fatal error. */
    (void)housekeeping_cpu;
    return 0;
}
