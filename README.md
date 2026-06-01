# ServMark

Next-generation OS benchmarking framework. 15 categories, 55 benchmarks, UnixBench-inspired coverage plus modern extensions (NUMA, virtualization, container density, crypto, network stack, io_uring).

## Quick Start

```bash
mkdir build && cd build
cmake .. && make -j$(nproc)
./servmark --validate       # pre-flight checks
./servmark --dry-run        # list all 55 benchmarks
./servmark --category C1    # run one category
./servmark --threads 8      # 8 parallel instances, one per core
```

## Requirements

- CMake ≥ 3.16, C11 compiler
- hwloc, libnuma, OpenSSL, libzstd, pthreads

## Categories

| ID | Category | UnixBench Mapping | Benchmarks |
|----|----------|-------------------|------------|
| C1 | Integer Compute | Dhrystone 2 | int-hash, int-sort, int-parse, int-regex |
| C2 | Float & Vector | Whetstone | fp-gemm, fp-fft, fp-conv, fp-distance, fp-ray |
| C3 | Compression & Crypto | *new* | crypto-zstd, crypto-aes, crypto-hash, crypto-rsa |
| C4 | Memory Hierarchy | *new* | mem-latency, mem-bandwidth, mem-stream, mem-random |
| C5 | NUMA Topology | *new* | numa-latency, numa-bandwidth, numa-false-sharing, numa-migration |
| C6 | Process Lifecycle | Process Creation | proc-fork-exec, proc-pthread, proc-mmap |
| C7 | Sync & Lock Contention | Pipe Ctx Switch (concurrency view) | sync-mutex, sync-spinlock, sync-rwlock, sync-semaphore, sync-mpmc |
| C8 | Context Switching | Pipe Ctx Switch (pure switch view) | cswitch-pipe-ping, cswitch-futex, cswitch-smt |
| C9 | Script & Language Runtime | Shell Scripts | script-shell, script-python |
| C10 | File I/O | File Copy | fs-copy, fs-seq-rw, fs-rand-rw, fs-fsync, fs-meta, fs-uring |
| C11 | Pipe & Local IPC | Pipe Throughput | ipc-pipe, ipc-unix, ipc-eventfd, ipc-signal |
| C12 | System Call Overhead | System Call Overhead | syscall-getpid, syscall-vdso, syscall-uring |
| C13 | Network Stack | *new* | net-tcp, net-udp, net-conn-rate, net-latency, net-zero-copy |
| C14 | Virtualization Overhead | *new* | vm-detect, vm-cpu-overhead |
| C15 | Container Density | *new* | ctr-lifecycle |

## Features

- **55 benchmarks**, 15 categories, architecture-fair C11
- **Per-core parallel instances** (`--threads N`) with CPU pinning
- **Convergence-based stopping** (SEM/mean ≤ 2%), BCa bootstrap CI
- **Dual run modes**: peak (locked frequency) + sustained (real-world)
- **Dual mitigation runs**: mitigated score + mitigations=off reference
- **Output**: provenance JSON, JSONL, HTML report, terminal summary
- **Geometric mean** aggregation across all score levels

## CLI Options

```
--mode <peak|sustained>   Run mode (default: peak)
--validate                System readiness check
--threads <N>             Parallel instances, one per core
--category <C1..C15>      Filter by category
--tier <1|2|3>            Filter by tier
--output-dir <dir>        Output directory
--dry-run                 List benchmarks without running
```
