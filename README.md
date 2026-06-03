# ServMark

Next-generation server benchmarking framework — 15 categories, 55 benchmarks, architecture-neutral (x86 / ARM64 / RISC-V).

## Quick Start

```bash
sudo ./scripts/install-deps.sh                    # auto-detects package manager
mkdir build && cd build && cmake .. && make -j$(nproc)
./servmark --validate       # pre-flight system check
./servmark --dry-run        # list all benchmarks
./servmark --category C1    # single category
./servmark --threads 8      # 8 parallel instances, SMT-aware pinning
./servmark                  # full default run (peak mode, tier 1)
```

## CLI Options

```
--config <file>              Config file (searches: $XDG_CONFIG_HOME, /etc, cwd)
--mode <peak|sustained>      Run mode (default: peak)
--validate                   System readiness check only
--tier <1|2|3>               Filter by tier (default: 1)
--category <C1..C15>         Filter by category
--benchmark <name>           Run a single benchmark by name
--threads <N>                Parallel instances, SMT-aware (default: auto)
--cpu-pin <spec>             CPU pinning: auto|auto-all|auto-numa|<list>
--numa-topo <spec>           NUMA topology: N|@file|cpu_range:nid,...
--membind <policy>           Memory policy: local|interleave|<node_id>
--mitigations-off            Run with mitigations=off reference
--output-dir <dir>           Output directory, auto-created (default: .)
--reference <file>           Frozen reference for cross-machine scoring
--dry-run                    List benchmarks without running
--list-categories            List category names and weights
--list-topology              Show CPU/NUMA/cache topology
--min-iterations <N>         Override per-benchmark min iterations
--max-iterations <N>         Override per-benchmark max iterations
--convergence <F>            Override convergence SEM/mean target
--max-runtime <sec>          Override per-benchmark max runtime
--cooldown <sec>             Override cooldown between benchmarks
--version                    Print version
--help                       Show usage
```

## CPU / NUMA Binding

Manual control for large multi-socket servers:

```bash
# CPU instance pinning (which CPUs benchmarks run on)
./servmark --cpu-pin auto              # SMT-aware physical cores (default)
./servmark --cpu-pin auto-all          # all logical CPUs (include HT)
./servmark --cpu-pin auto-numa         # one CPU per NUMA node
./servmark --cpu-pin "0,2,4,6"        # explicit CPU list
./servmark --cpu-pin "0-7"            # range
./servmark --cpu-pin "0-3,8-11"       # mixed

# NUMA topology override (which NUMA node each CPU belongs to)
./servmark --numa-topo 2               # auto-split interleaved across 2 nodes
./servmark --numa-topo @topo.cfg       # load from file (# comments ok)
./servmark --numa-topo "18:0,19:0,20:1"  # inline cpu_range:nid pairs

# Memory allocation policy
./servmark --membind local             # bind to CPU's local node (default)
./servmark --membind interleave        # stripe across all nodes
./servmark --membind 0                 # bind explicitly to node 0
```

Topology override rebuilds `system_info.numa_nodes[]` — all NUMA benchmarks see the user-specified layout. Memory policy uses `set_mempolicy()` before benchmark `init()` so all `malloc`/`mmap` obey the binding.

## Requirements

| Dependency | Ubuntu/Debian | CentOS/RHEL/Fedora | openSUSE | Arch | Alpine |
|---|---|---|---|---|---|
| Compiler | `build-essential` | `gcc gcc-c++ make` | `gcc gcc-c++ make` | `gcc make` | `gcc g++ make` |
| CMake (>=3.16) | `cmake` | `cmake` | `cmake` | `cmake` | `cmake` |
| pkg-config | `pkg-config` | `pkgconfig` | `pkg-config` | `pkg-config` | `pkgconfig` |
| hwloc | `libhwloc-dev` | `hwloc-devel` | `hwloc-devel` | `hwloc` | `hwloc-dev` |
| libnuma | `libnuma-dev` | `numactl-devel` | `libnuma-devel` | `numactl` | `numactl-dev` |
| OpenSSL | `libssl-dev` | `openssl-devel` | `libopenssl-devel` | `openssl` | `openssl-dev` |
| libzstd | `libzstd-dev` | `libzstd-devel` | `libzstd-devel` | `zstd` | `zstd-dev` |

Per-package install: each dependency installed individually — a missing optional package (e.g. hwloc-devel) won't block the rest.

## Project Structure

```
ServMark/
├── inc/                     # Public headers
│   ├── servmark.h           #   Constants, version, inline helpers
│   ├── benchmark.h          #   benchmark_t struct + SSB_BENCHMARK_REGISTER macro
│   ├── harness.h            #   run_config_t, run_result_t, subtest_result_t
│   ├── stats.h              #   Statistics (bootstrap, t-test, normality, outlier)
│   ├── scoring.h            #   Scoring (normalization, pillars, overall)
│   ├── system.h             #   system_info_t, NUMA node, cache info types
│   └── output.h             #   Output generators (JSON, JSONL, HTML, terminal)
├── src/
│   ├── main.c               # Entry point, CLI, config parsing
│   ├── core/                # Core engine (6 files)
│   │   ├── benchmark.c      #   Benchmark registry (global list, max 128)
│   │   ├── harness.c        #   Execution, fork+pipe runner, CPU/NUMA pinning
│   │   ├── stats.c          #   Statistics (bootstrap CI, Anderson-Darling, t-test)
│   │   ├── scoring.c        #   Weighted geometric mean, pillar/overall scores
│   │   ├── system.c         #   System probe (CPU, NUMA, cache, governor, VM detect)
│   │   └── output.c         #   Output writers (JSON, JSONL, HTML, terminal)
│   └── benchmarks/          # 55 benchmarks in 8 modules
│       ├── compute/ (8)     #   C1: Integer ALU, C2: Float/Vector
│       ├── crypto/  (16)    #   C3: AES/SHA/RSA/ECC/ChaCha/zstd/SMx
│       ├── memory/  (8)     #   C4: Latency/Bandwidth, C5: NUMA
│       ├── os/      (11)    #   C6: Process, C8: CtxSwitch, C9: Script, C12: Syscall
│       ├── sync/    (5)     #   C7: Mutex/Spinlock/RWLock/Sem/MPMC
│       ├── io/      (7)     #   C10: File I/O, C11: IPC
│       ├── net/     (5)     #   C13: TCP/UDP/ConnRate/Latency/ZeroCopy
│       └── virt/    (2)     #   C14: VM Detect, C15: Container Lifecycle
├── config/default.cfg       # SPEC-style config
├── scripts/install-deps.sh  # Auto-dependency installer (6 package managers)
└── CMakeLists.txt
```

## Categories & Benchmarks (55 total)

### C1 — Integer Compute (5 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| int-hash | 64-bit hash table insert/lookup/delete (verified) | ops/sec | 1 |
| int-sort | 64-bit LSD radix sort 10M (pre-alloc work buf) | elements/sec | 1 |
| int-parse | Integer string parsing (Dhrystone modernized, verified) | items/sec | 1 |
| int-substr | Naive substring search on 2MB log (branch stress) | byte-scans/sec | 1 |
| vm-cpu-overhead | Sieve of Eratosthenes fixed-work ALU stress | passes/sec | 1 |

### C2 — Float & Vector (5 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| fp-gemm | DGEMM N=512 (neg/denorm init, column checksum) | FLOPS | 1 |
| fp-fft | Radix-2 FFT N=2048 (inverse transform verified) | FLOPS | 1 |
| fp-conv | 3x3 convolution 128x128 (output verified) | FLOPS | 1 |
| fp-distance | Cosine distance dim=768 (query norm precomputed) | FLOPS | 1 |
| fp-ray | Ray-triangle (Moller-Trumbore, hit count verified) | intersect/sec | 1 |

### C3 — Compression & Crypto (16 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| crypto-zstd | zstd compress level 3 64MB | bytes/sec | 1 |
| crypto-aes | AES-256-GCM encrypt | bytes/sec | 1 |
| crypto-aes-gcm-decrypt | AES-256-GCM decrypt + tag verify | bytes/sec | 2 |
| crypto-hash | SHA-256 hash 256MB | bytes/sec | 1 |
| crypto-hash-l2 | SHA-256 1MB L2-resident (pure engine) | bytes/sec | 2 |
| crypto-rsa | RSA-2048 SHA-256 sign (PKCS#1 v1.5) | signatures/sec | 1 |
| crypto-rsa-verify | RSA-2048 SHA-256 verify | verifications/sec | 2 |
| crypto-ecdsa-p256-sign | ECDSA P-256 SHA-256 sign | signatures/sec | 1 |
| crypto-ecdsa-p256-verify | ECDSA P-256 SHA-256 verify | verifications/sec | 2 |
| crypto-ed25519-sign | Ed25519 sign (SSH/WireGuard/TLS 1.3) | signatures/sec | 1 |
| crypto-x25519-ecdh | X25519 ECDH key exchange (TLS 1.3) | exchanges/sec | 1 |
| crypto-chacha20-poly1305 | ChaCha20-Poly1305 AEAD (WireGuard/QUIC) | bytes/sec | 1 |
| crypto-sm2-sign | SM2 SM3 sign (GM/T 0003, China standard) | signatures/sec | 2 |
| crypto-sm3 | SM3 hash 256MB (GM/T 0004) | bytes/sec | 2 |
| crypto-sm4 | SM4 CTR encrypt (GM/T 0002) | bytes/sec | 2 |

All crypto benchmarks use OpenSSL EVP API. Hardware acceleration auto-detected: AES-NI+SHA-NI (x86), ARMv8 Crypto (ARM64), pure software (RISC-V). No architecture `#ifdef` — OpenSSL handles dispatch internally.

### C4 — Memory Hierarchy (4 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| mem-latency | Multi-level pointer chase (8KB-256MB, runtime CLS stride) | ns/chase | 1 |
| mem-bandwidth | Sequential read bandwidth 256MB (volatile anti-vectorize) | bytes/sec | 1 |
| mem-stream | STREAM Triad (auto-scaled NCPU threads) | bytes/sec | 1 |
| mem-random | Random access 32MB (L3 on modern servers) | ns/access | 1 |

### C5 — NUMA Topology (4 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| numa-latency | Node 0 local memory latency (runtime CLS stride) | ns/chase | 1 |
| numa-bandwidth | Remote node read bandwidth (per-thread pinning) | bytes/sec | 1 |
| numa-false-sharing | False sharing ratio (runtime CLS, relaxed atomics) | ratio | 1 |
| numa-migration | Page migration throughput (runtime page size) | migrations/sec | 1 |

### C6 — Process Lifecycle (3 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| proc-fork-exec | fork+exec+wait loop (UnixBench equivalent) | us/call | 1 |
| proc-pthread | pthread create/join latency | us/thread | 1 |
| proc-mmap | mmap/munmap anonymous (MAP_POPULATE, runtime page size) | us/op | 1 |

### C7 — Synchronization (5 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| sync-mutex | pthread_mutex contention (auto-scaled threads) | ops/sec | 1 |
| sync-spinlock | pthread_spinlock contention (auto-scaled threads) | ops/sec | 1 |
| sync-rwlock | pthread_rwlock reader/writer (auto-scaled) | ops/sec | 1 |
| sync-semaphore | sem_post/wait wakeup (auto-scaled waiters) | wakeups/sec | 1 |
| sync-mpmc | Lock-free MPMC queue (acquire/release atomics) | ops/sec | 1 |

### C8 — Context Switching (3 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| cswitch-pipe-ping | Pipe-based ping-pong (UnixBench equivalent) | switches/sec | 1 |
| cswitch-futex | Futex wait/wake ping-pong (correct handoff) | switches/sec | 1 |
| cswitch-smt | SMT cache line bouncing (2 threads, relaxed atomics) | ops/sec | 1 |

### C9 — Script Runtime (2 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| script-shell | Shell script throughput (fork+exec /bin/sh) | scripts/sec | 1 |
| script-python | Python script throughput (py3 startup) | scripts/sec | 1 |

### C10 — File I/O (6 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| fs-copy | File copy 4KB buffer (UnixBench equivalent) | bytes/sec | 1 |
| fs-seq-rw | Sequential read 128MB (Direct I/O, O_DIRECT aligned) | bytes/sec | 1 |
| fs-rand-rw | Random 4KB read (O_DIRECT, precomputed offsets) | IOPS | 1 |
| fs-fsync | fdatasync latency (128KB write, no metadata flush) | us/fsync | 1 |
| fs-meta | Metadata stat() throughput (50K files) | stats/sec | 1 |
| fs-uring | Random 4KB pread() (io_uring placeholder, sync only) | IOPS | 2 |

### C11 — IPC (3 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| ipc-pipe | Pipe throughput 512B (two-process, UnixBench-style) | bytes/sec | 1 |
| ipc-unix | AF_UNIX socketpair stream (MSG_WAITALL validated) | bytes/sec | 1 |
| ipc-signal | kill() signal send (SIGUSR1, syscall-cost dominant) | signals/sec | 1 |

### C12 — Syscall Overhead (4 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| syscall-getpid | getpid() syscall (UnixBench equivalent) | ns/call | 1 |
| syscall-vdso | vDSO clock_gettime (userspace fast path) | ns/call | 1 |
| syscall-raw | Raw syscall(SYS_getpid) baseline | calls/sec | 2 |
| ipc-eventfd | eventfd write/read (syscall-pair microbenchmark) | roundtrips/sec | 1 |

### C13 — Network Stack (5 benchmarks)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| net-tcp | TCP loopback stream 64KB (NODELAY both, 4MB buffers) | bytes/sec | 1 |
| net-udp | UDP loopback 1472B (16MB rcvbuf, 100ms timeout) | packets/sec | 1 |
| net-conn-rate | TCP conn rate (backlog=4096, actual count) | conns/sec | 1 |
| net-latency | TCP RTT 64B (NODELAY+QUICKACK, no Nagle/delACK) | us/RR | 1 |
| net-zero-copy | sendfile() file->TCP socket (real socket sink) | bytes/sec | 1 |

### C14 — Virtualization (1 benchmark)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| vm-detect | VM detect via DMI sysfs (arch-neutral, no CPUID) | ns/call | 1 |

### C15 — Container (1 benchmark)

| Benchmark | Description | Metric | Tier |
|-----------|-------------|--------|------|
| ctr-lifecycle | Namespace clone (NS+UTS+NET+PID, pre-alloc stack) | us/container | 1 |

## Architecture

### Benchmark Registration (self-registering plugin pattern)

```c
benchmark_t bench_fp_fft = {
    .name = "fp-fft", .category = "C2",
    .description = "Radix-2 FFT N=2048", .tier = 1,
    .init = fp_fft_init, .warmup = fp_fft_warmup,
    .measure = fp_fft_measure, .cleanup = fp_fft_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fp_fft);
```

`SSB_BENCHMARK_REGISTER` emits a GCC `__attribute__((constructor))` that auto-registers at load time. No central benchmark list — create a `.c` file, add to the module's `CMakeLists.txt`.

### Execution Flow

1. `main.c` loads config, CLI overrides config values
2. `harness_run()` probes system, filters benchmarks by tier/category/name
3. **Benchmark warmup**: `bench->warmup()` (lightweight priming), then 2x full `measure()` (discarded)
4. **Convergence loop**: `measure()` repeats until SEM/mean ≤ 2%, 5-31 iterations, 10-180s limits
5. **Cooldown**: `sleep(cooldown_sec)` between benchmarks in peak mode (default 30s)

| Mode | Trigger | Behavior |
|------|---------|----------|
| Single instance | `num_threads != 1` | Runs in-process (benchmark spawns threads) |
| Parallel instances | `--threads N` + `num_threads == 1` | Forks N children, SMT-aware pinning, pipe results |

### Scoring Pipeline

```
Raw Measurements → stats_compute() → normalized → Category Scores → Pillars → Overall
```

| Pillar | Categories (weights) |
|--------|---------------------|
| Throughput | C1(12%), C2(10%), C3(5%), C4(8%), C10(10%), C13(5%) |
| Latency | C6(8%), C7(10%), C8(6%), C11(5%), C12(5%) |
| Efficiency | C5(5%), C14(3%), C15(2%) |

**Overall Score** = geometric mean of three pillars. Latency benchmarks inverted (higher=better). Self-normalized default; `--reference` enables cross-machine baselines.

### Statistical Methods

| Method | Notes |
|--------|-------|
| Percentile bootstrap CI | 10,000 resamples, 95% CI |
| Anderson-Darling normality | Stephens small-sample correction |
| Welch's t-test | t-distribution via Lentz continued fraction |
| GESD outlier detection | Rosner critical values |
| Mann-Whitney U | Hodges-Lehmann median-difference |
| Benjamini-Hochberg FDR | Multiple comparison correction |

### System Probe

`system_probe()` via three-tier fallback (hwloc → sysfs → hardcoded):

- **CPU**: model, ISA, physical/logical cores, SMT, frequency/governor
- **NUMA**: dynamic node count (up to 64), multi-group cpumap (>64-bit), meminfo, distance matrix
- **Cache**: level/type/size/line/associativity per level
- **Memory**: total RAM, hugepage availability
- **OS**: kernel, distro, libc version
- **VM**: DMI product_name probe (arch-neutral)

### Output

| Format | File |
|--------|------|
| Provenance JSON | `<run-id>.provenance.json` |
| Results JSONL | `<run-id>.results.jsonl` |
| HTML Report | `<run-id>.report.html` |
| Terminal | stdout box-drawn table |

## Configuration

SPEC CPU 2016 compatible: `key = value`, `#` comments. CLI overrides config.

```ini
runmode         = peak
threads         = 0                    # 0 = auto-detect
output_dir      = .
tier            = 1
category        =                     # empty = all
cpu_pin         = auto                # auto|auto-all|auto-numa|<list>
numa_topo       =                     # N|@file|cpu_range:nid,...
membind         = local               # local|interleave|<nid>
mitigations_off = 0
reportable      = 1                   # skip CV >= 10%

# Per-benchmark overrides (0 = benchmark default)
min_iterations  = 5
max_iterations  = 31
convergence     = 0.02
max_runtime     = 180
cooldown_sec    = 30

# Benchmark filter
benchmark = C1 : int-sort : 64-bit LSD radix sort
```

Config search: `$XDG_CONFIG_HOME/servmark/` → `/etc/servmark/` → `config/default.cfg`.

`march_native` / `isa_baseline` are build-time only — set via `cmake -DSSB_USE_MARCH_NATIVE=ON`.

## Compiler Flag Rationale

| Flag | Reason |
|------|--------|
| `-O2` | Conservative: GCC -O2 disables auto-vectorization, producing consistent scalar baseline. Float benchmarks 4-8× slower vs -O3 — intentional for reproducible scoring. |
| `-fno-omit-frame-pointer` | Accurate perf backtraces. |
| `-fno-lto` | Stable per-benchmark isolation. |
| `-D_GNU_SOURCE` | sched_setaffinity, CPU_SET, sysconf. |

## Architecture Fairness

All 55 benchmarks execute identical C11 code paths on x86, ARM64, and RISC-V:

- **Zero architecture `#ifdef`** — no `__x86_64__`, `__aarch64__`, `__riscv` conditionals
- **No x86 intrinsics** — OpenSSL EVP handles crypto acceleration dispatch internally
- **Runtime cache line sizes** — `sysconf(_SC_LEVEL1_DCACHE_LINESIZE)` throughout
- **Runtime page sizes** — `sysconf(_SC_PAGESIZE)` throughout
- **Weak-memory-friendly** — `_Atomic` with `memory_order_acquire/release/relaxed`; no `seq_cst` in hot paths
- **SMT detection** — runtime `thread_siblings_list` read, no hardcoded HT assumptions
- **NUMA cpumap** — multi-group hex parse supports >64-core servers
- **Dataset sizing** — explicitly chosen relative to cache levels, not absolute sizes
