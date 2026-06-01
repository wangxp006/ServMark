# ServMark

Next-generation OS benchmarking framework. 15 categories, 55 benchmarks across 8 modules — UnixBench-inspired coverage plus modern server extensions (NUMA, virtualization, container density, crypto, network stack, io_uring).

## Quick Start

```bash
# Install dependencies
sudo ./scripts/install-deps.sh

# Build
mkdir build && cd build && cmake .. && make -j$(nproc)

# Run
./servmark --validate       # pre-flight system readiness check
./servmark --dry-run        # list all 55 benchmarks
./servmark --category C1    # run a single category
./servmark --threads 8      # 8 parallel instances, one per physical core
./servmark                  # full run (default: peak mode, tier 1)
```

## Requirements

| Dependency | Ubuntu/Debian | CentOS/RHEL/Fedora | openSUSE | Arch | Alpine |
|---|---|---|---|---|---|
| Compiler toolchain | `build-essential` | `gcc gcc-c++ make` | `gcc gcc-c++ make` | `gcc make` | `gcc g++ make` |
| CMake (>=3.16) | `cmake` | `cmake` | `cmake` | `cmake` | `cmake` |
| pkg-config | `pkg-config` | `pkgconfig` | `pkg-config` | `pkg-config` | `pkgconfig` |
| hwloc | `libhwloc-dev` | `hwloc-devel` | `hwloc-devel` | `hwloc` | `hwloc-dev` |
| libnuma | `libnuma-dev` | `numactl-devel` | `libnuma-devel` | `numactl` | `numactl-dev` |
| OpenSSL (libcrypto) | `libssl-dev` | `openssl-devel` | `libopenssl-devel` | `openssl` | `openssl-dev` |
| libzstd | `libzstd-dev` | `libzstd-devel` | `libzstd-devel` | `zstd` | `zstd-dev` |

Or use the bundled script: `sudo ./scripts/install-deps.sh` — auto-detects the package manager.

## CLI Options

```
--config <file>             Config file (searches: $XDG_CONFIG_HOME/servmark/, /etc/servmark/, then cwd)
--mode <peak|sustained>     Run mode (default: peak)
--validate                  System readiness check only
--tier <1|2|3>              Filter by tier (default: 1)
--category <C1..C15>        Filter by category
--threads <N>               Parallel instances, one per physical core (default: auto)
--mitigations-off           Run with CPU mitigations=off reference pass
--output-dir <dir>          Output directory (default: .)
--reference <file>          Frozen reference file for cross-machine score normalization
--dry-run                   List benchmarks without running
--help                      Show usage
```

## Project Structure

```
ServMark/
├── inc/                     # Public headers
│   ├── servmark.h           #   Constants, version, inline helpers
│   ├── benchmark.h          #   benchmark_t struct + SSB_BENCHMARK_REGISTER macro
│   ├── harness.h            #   run_config_t, run_result_t, subtest_result_t
│   ├── stats.h              #   Statistical API (bootstrap, t-test, normality, outlier)
│   ├── scoring.h            #   Scoring API (normalization, pillars, overall)
│   ├── system.h             #   system_info_t, cache info, NUMA node types
│   └── output.h             #   Output generators (JSON, JSONL, HTML, terminal)
├── src/
│   ├── main.c               # Entry point, CLI parsing, config loading
│   ├── core/                # Core engine (7 files)
│   │   ├── benchmark.c      #   Benchmark registry (global list, max 128)
│   │   ├── harness.c        #   Execution loop, parallel fork+pipe runner, CPU pinning
│   │   ├── stats.c          #   Statistics (bootstrap CI, Anderson-Darling, Welch t-test)
│   │   ├── scoring.c        #   Scoring pipeline, weighted geometric mean, pillars
│   │   ├── system.c         #   System probe (CPU, NUMA, cache, governor, VM detection)
│   │   ├── output.c         #   Output writers (provenance JSON, JSONL, HTML, terminal)
│   │   └── validate.c       #   Pre-flight validation
│   └── benchmarks/          # 55 benchmarks in 8 modules
│       ├── compute/ (9)     #   C1: Integer  +  C2: Float/Vector
│       ├── crypto/  (4)     #   C3: Compression + Crypto
│       ├── memory/  (8)     #   C4: Memory Hierarchy  +  C5: NUMA Topology
│       ├── os/      (11)    #   C6: Process  +  C8: Context Switch  +  C9: Script  +  C12: Syscall
│       ├── sync/    (5)     #   C7: Synchronization + Lock Contention
│       ├── io/      (10)    #   C10: File I/O  +  C11: Pipe & Local IPC
│       ├── net/     (5)     #   C13: Network Stack
│       └── virt/    (3)     #   C14: Virtualization  +  C15: Container Density
├── config/default.cfg       # SPEC-style configuration (benchmark list + defaults)
├── scripts/install-deps.sh  # Auto-dependency installer (6 package managers)
└── CMakeLists.txt           # Build system
```

## Categories & Benchmarks (55 total)

### compute/ -- C1 Integer + C2 Float/Vector (9 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C1 | int-hash | Integer hash computation throughput | ops/sec |
| C1 | int-sort | 64-bit integer LSD radix sort (10M elements) | elements/sec |
| C1 | int-parse | Integer string parsing throughput | ops/sec |
| C1 | int-regex | Regex pattern matching on integer strings | ops/sec |
| C2 | fp-gemm | Floating-point general matrix multiply | FLOPS |
| C2 | fp-fft | Fast Fourier Transform (radix-2) | FLOPS |
| C2 | fp-conv | 1-D convolution throughput | FLOPS |
| C2 | fp-distance | Euclidean distance computation | FLOPS |
| C2 | fp-ray | Ray-sphere intersection throughput | rays/sec |

### crypto/ -- C3 Compression & Crypto (4 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C3 | crypto-zstd | zstd compression throughput | bytes/sec |
| C3 | crypto-aes | AES-256-GCM sustained encryption (CTX reused across chunks) | bytes/sec |
| C3 | crypto-hash | SHA-256 hashing throughput | bytes/sec |
| C3 | crypto-rsa | RSA-2048 signature throughput | signs/sec |

### memory/ -- C4 Memory Hierarchy + C5 NUMA Topology (8 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C4 | mem-latency | Multi-level pointer chase latency (8KB-256MB, 8 working set sizes) | ns/chase |
| C4 | mem-bandwidth | Sequential memory read bandwidth (256MB, uint64 access) | bytes/sec |
| C4 | mem-stream | STREAM Triad (512MB/array, physical-core-scaled, pinned threads) | bytes/sec |
| C4 | mem-random | Random memory access latency (32MB working set) | ns/access |
| C5 | numa-latency | NUMA node-0 local memory latency (pinned thread, strict-aliasing-safe) | ns/chase |
| C5 | numa-bandwidth | NUMA remote node read bandwidth (pinned threads, uint64 access) | bytes/sec |
| C5 | numa-false-sharing | False sharing penalty ratio (2 threads, same vs different cache line, 64B-aligned) | ratio |
| C5 | numa-migration | NUMA page migration throughput (kernel metric) | pages/sec |

### os/ -- C6 Process + C8 Context Switch + C9 Script + C12 Syscall (11 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C6 | proc-fork-exec | fork+exec+wait loop (UnixBench-style Process Creation) | us/call |
| C6 | proc-pthread | pthread create+join throughput | threads/sec |
| C6 | proc-mmap | mmap/munmap cycle throughput | ops/sec |
| C8 | cswitch-pipe-ping | Pipe-based context switching (two threads, ping-pong, timer starts after thread creation) | switches/sec |
| C8 | cswitch-futex | Futex-based context switching | switches/sec |
| C8 | cswitch-smt | SMT sibling context switch contention | switches/sec |
| C9 | script-shell | Shell script execution throughput | scripts/sec |
| C9 | script-python | Python script execution throughput | scripts/sec |
| C12 | syscall-getpid | getpid() system call overhead | ns/call |
| C12 | syscall-vdso | VDSO clock_gettime() overhead | ns/call |
| C12 | syscall-uring | io_uring submission overhead | ns/call |

### sync/ -- C7 Synchronization & Lock Contention (5 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C7 | sync-mutex | pthread mutex lock/unlock contention | ops/sec |
| C7 | sync-spinlock | Spinlock acquire/release throughput | ops/sec |
| C7 | sync-rwlock | Read-write lock reader/writer throughput | ops/sec |
| C7 | sync-semaphore | POSIX semaphore wait/post throughput | ops/sec |
| C7 | sync-mpmc | Multi-producer multi-consumer queue throughput | ops/sec |

### io/ -- C10 File I/O + C11 Pipe & Local IPC (10 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C10 | fs-copy | File copy throughput (4KB blocks) | bytes/sec |
| C10 | fs-seq-rw | Sequential read/write throughput | bytes/sec |
| C10 | fs-rand-rw | Random read/write IOPS | ops/sec |
| C10 | fs-fsync | fsync() latency throughput | ops/sec |
| C10 | fs-meta | Filesystem metadata operations (create/unlink/stat) | ops/sec |
| C10 | fs-uring | io_uring-based file I/O throughput | bytes/sec |
| C11 | ipc-pipe | Pipe throughput, two-process (parent writes, child reads) | bytes/sec |
| C11 | ipc-unix | UNIX domain socket throughput | bytes/sec |
| C11 | ipc-eventfd | eventfd signal/wait throughput | ops/sec |
| C11 | ipc-signal | Signal send/receive throughput | ops/sec |

### net/ -- C13 Network Stack (5 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C13 | net-tcp | TCP stream throughput (loopback) | bytes/sec |
| C13 | net-udp | UDP datagram throughput (loopback) | bytes/sec |
| C13 | net-conn-rate | TCP connection accept/close rate | conns/sec |
| C13 | net-latency | TCP round-trip latency | us/RTT |
| C13 | net-zero-copy | Zero-copy sendfile throughput | bytes/sec |

### virt/ -- C14 Virtualization + C15 Container (3 benchmarks)

| Category | Benchmark | Description | Primary Metric |
|----------|-----------|-------------|----------------|
| C14 | vm-detect | Hypervisor detection accuracy and speed (CPUID + DMI, fixed KVM signature) | ns/detect |
| C14 | vm-cpu-overhead | CPU virtualization overhead (cpuid trap cost) | ns/trap |
| C15 | ctr-lifecycle | Container lifecycle throughput (start/stop) | containers/sec |

## Architecture

### Benchmark Registration (self-registering plugin pattern)

Each benchmark `.c` file defines a `benchmark_t` struct with function pointers (`init`, `warmup`, `measure`, `cleanup`). At the bottom:

```c
SSB_BENCHMARK_REGISTER(bench_mem_latency);
```

This creates a GCC `__attribute__((constructor))` function that calls `benchmark_register()` at load time, populating a global array (max 128 entries). The linker must use `--whole-archive` for benchmark static libraries, otherwise "unreferenced" object files get discarded.

### Execution Flow

1. `main.c` loads config, then CLI args override config values
2. `harness_run()` probes the system via hwloc/sysfs, filters benchmarks by tier/category
3. **Warmup**: each benchmark's `measure()` runs 2x (results discarded) to stabilize caches/TLB
4. **Iteration loop**: `measure()` runs repeatedly until SEM/mean <= 2% convergence or time/count limits (uses nanosecond `CLOCK_MONOTONIC` + `CLOCK_THREAD_CPUTIME_ID` guard)
5. **Cooldown**: `sleep(SSB_COOLDOWN_SEC)` between benchmarks in peak mode when `cooldown_required=true`

**Two execution modes:**

| Mode | Trigger | Behavior |
|------|---------|----------|
| Single instance | `num_threads != 1` benchmarks (e.g. mem_stream, sync_mutex) | Runs in-process |
| Parallel instances | `--threads N` + `num_threads == 1` | Forks N child processes, SMT-aware pinning to physical cores via `sched_setaffinity()`, results via pipe |

### Parallel Aggregation

- **Throughput** (`higher_is_better = true`): per-instance means stored as independent statistical samples
- **Latency** (`higher_is_better = false`): all iterations from all instances pooled as independent samples
- **SMT-aware pinning**: reads `thread_siblings_list` to pin only to physical cores, skipping HT siblings. Falls back to sequential pinning if sysfs unavailable.

### Scoring Pipeline

```
Raw Measurements -> stats_compute() -> normalized_score -> Category Scores -> Pillars -> Overall
```

1. **Stats**: mean, stddev, SEM, CV, 95% percentile bootstrap CI, reliability label (see Statistical Methods below)
2. **Normalization**: `normalized_score = inverted mean` for latency benchmarks (so higher=better), self-normalized by default. With `--reference`, scores are normalized against frozen baselines via `scoring_load_reference()`.
3. **Category scores**: geometric mean of sub-benchmark scores
4. **Three pillars** (weighted geometric mean):

| Pillar | Categories (weights) | Description |
|--------|---------------------|-------------|
| Throughput | C1(12%),C2(10%),C3(5%),C4(8%),C10(10%),C13(5%) | Compute, memory, I/O, network throughput |
| Latency | C6(8%),C7(10%),C8(6%),C11(5%),C12(5%) | Process, sync, context switch, IPC, syscall latency |
| Efficiency | C5(5%),C14(3%),C15(2%) | NUMA efficiency, virtualization, container overhead |

5. **Overall score** = geometric mean of the three pillars

### Statistical Methods

| Method | Function | Notes |
|--------|----------|-------|
| Percentile bootstrap CI | `stats_bootstrap_percentile()` | 10,000 resamples, 95% CI (BCa-compatible alias kept) |
| Anderson-Darling normality | `stats_anderson_darling()` | A**2* statistic with Stephens small-sample correction |
| Welch's t-test | `stats_welch_ttest()` | t-distribution CDF via incomplete beta (Lentz continued fraction) |
| GESD outlier detection | `stats_detect_outliers()` | Rosner (1983) critical values via t-distribution quantiles |
| Benjamini-Hochberg FDR | `stats_benjamini_hochberg()` | Multiple comparison correction |
| Mann-Whitney U test | `stats_mann_whitney()` | With Hodges-Lehmann median-difference estimator |

### System Probe

`system_probe()` populates `system_info_t` with:

- **CPU**: model name (`/proc/cpuinfo`), ISA flags, core counts (physical and logical), SMT detection via `thread_siblings_list`
- **Frequency**: base, max, and current from cpufreq sysfs; governor status
- **NUMA**: dynamic node count (up to 64) with cpumap, meminfo, and distance matrix from sysfs
- **Cache**: three-tier fallback: hwloc (preferred) -> sysfs (`/sys/.../cache/indexN/`) -> hardcoded defaults
- **Memory**: total RAM, huge page availability
- **Virtualization**: DMI vendor/product detection (KVM, VMware, Hyper-V, Xen, AWS Nitro, GCE, OCI, DigitalOcean, Alibaba Cloud)
- **OS**: kernel version, distribution (`/etc/os-release`)

### Output Formats

| Format | File | Contents |
|--------|------|----------|
| Provenance JSON | `<run-id>.provenance.json` | Full system info + per-benchmark stats + scores |
| Results JSONL | `<run-id>.results.jsonl` | One JSON object per benchmark per line |
| HTML Report | `<run-id>.report.html` | Sortable table dashboard |
| Terminal Summary | stdout | Box-drawn table with overall + pillar scores |
| UUID | Embedded in run-id | UUID4 via `/dev/urandom` |

### Key Constants (`inc/servmark.h`)

| Constant | Value | Meaning |
|----------|-------|---------|
| `SSB_VERSION` | `"1.0.0"` | Project version |
| `SSB_MAX_ITERATIONS` | 31 | Max benchmark iterations |
| `SSB_MIN_ITERATIONS` | 5 | Min benchmark iterations |
| `SSB_CONVERGENCE_TARGET` | 0.02 | SEM/mean ratio target |
| `SSB_MIN_RUNTIME_SEC` | 10 | Minimum seconds per benchmark |
| `SSB_MAX_RUNTIME_SEC` | 180 | Maximum seconds per benchmark |
| `SSB_COOLDOWN_SEC` | 30 | Cooldown between benchmarks (peak mode) |
| `SSB_BOOTSTRAP_RESAMPLES` | 10000 | Bootstrap resamples for CI |
| `SSB_CV_STABLE` | 0.02 | CV threshold for "stable" label |
| `SSB_CV_MODERATE` | 0.05 | CV threshold for "moderate" label |
| `SSB_CV_HIGH` | 0.10 | CV threshold for "high" (discarded when exceeded) |

## Configuration

Config file format: `key = value`, `#` for comments. CLI arguments override config file values.

```ini
# Run mode
runmode = peak
threads = 0                # 0 = auto-detect
output_dir = .
tier = 1
category =                 # empty = all, or C1..C15
mitigations_off = 0

# Benchmark filter: category : name : description
benchmark = C1 : int-sort : 64-bit integer LSD radix sort 10M elements
```

Config search path: `$XDG_CONFIG_HOME/servmark/default.cfg` -> `/etc/servmark/default.cfg` -> `config/default.cfg`

## Adding a New Benchmark

1. Create `src/benchmarks/<module>/new_bench.c`
2. Implement `init(void **state)`, `warmup(void *state)`, `measure(void *state, measurement_t *result)`, `cleanup(void *state)`
3. Define a `benchmark_t` struct with `.category`, `.tier`, `.description`, `.higher_is_better`, `.num_threads`, and the four function pointers
4. Call `SSB_BENCHMARK_REGISTER(your_struct)` at the bottom of the file
5. Add the source file to the appropriate `ssbbench_*` static library in `CMakeLists.txt`
6. Add the benchmark entry to `config/default.cfg`

## Compiler Flag Rationale

| Flag | Reason |
|------|--------|
| `-O2` | Conservative optimization. GCC -O2 does NOT enable auto-vectorization -- vectorizable benchmarks run as scalar code, producing consistent cross-compiler baseline results. |
| `-fno-omit-frame-pointer` | Enables accurate `perf` backtraces. x86 penalty ~3-7% (one GPR reserved), ARM64 virtually no penalty. |
| `-fno-lto` | Prevents cross-TU optimization for stable per-benchmark isolation. |
| `-D_GNU_SOURCE` | Required for `sched_setaffinity`, `CPU_SET`, `sysconf` extensions. |
