cmake_minimum_required(VERSION 3.16)
project(ServMark VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Baseline compiler flags:
# -O2: conservative optimization, balances performance with reproducibility.
#   NOTE: GCC -O2 does NOT enable auto-vectorization (-ftree-vectorize).
#   Vectorizable benchmarks (fp_gemm, fp_fft) will run as scalar code on
#   GCC, producing ~4-8x lower results vs -O3 or clang -O2. This is a
#   deliberate design choice for baseline cross-compiler comparability.
# -fno-omit-frame-pointer: enables accurate profiling/perf backtraces.
#   x86 penalty ~3-7% (one GPR reserved), ARM64 virtually no penalty.
# -fno-lto: prevents cross-TU optimization, stable per-benchmark isolation.
set(BASELINE_CFLAGS "-O2 -g -fno-omit-frame-pointer -fno-lto")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
set(CMAKE_C_FLAGS_RELEASE "${BASELINE_CFLAGS}")

option(SSB_USE_MARCH_NATIVE "Use -march=native (non-canonical)" OFF)
if(SSB_USE_MARCH_NATIVE)
    add_definitions(-DSSB_MARCH_NATIVE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED hwloc)
add_definitions(-DSSB_USE_HWLOC)
pkg_check_modules(NUMA REQUIRED numa)
pkg_check_modules(LIBCRYPTO REQUIRED libcrypto)
pkg_check_modules(ZSTD REQUIRED libzstd)
find_package(Threads REQUIRED)
set(PTHREAD_LIBRARIES Threads::Threads)
set(MATH_LIBRARIES m)

# Core library
add_library(ssbcore STATIC
    src/core/harness.c src/core/stats.c src/core/system.c
    src/core/output.c src/core/validate.c src/core/scoring.c
    src/core/benchmark.c
)
target_include_directories(ssbcore PUBLIC
    ${CMAKE_SOURCE_DIR}/inc
    ${HWLOC_INCLUDE_DIRS} ${NUMA_INCLUDE_DIRS}
    ${LIBCRYPTO_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS}
)
target_link_libraries(ssbcore PUBLIC
    ${HWLOC_LIBRARIES} ${NUMA_LIBRARIES} ${LIBCRYPTO_LIBRARIES}
    ${ZSTD_LIBRARIES} ${PTHREAD_LIBRARIES} ${MATH_LIBRARIES} dl rt
)

# Benchmark libraries — one per directory, mapped to categories:
#   compute/ (C1+C2): integer + float compute  (9 benchmarks)
#   crypto/  (C3):    compression + encryption  (4 benchmarks)
#   memory/  (C4+C5): memory hierarchy + NUMA   (8 benchmarks)
#   os/      (C6+C8+C9+C12): process, context switch, script, syscall (11)
#   sync/    (C7):    synchronization + locks   (5 benchmarks)
#   io/      (C10+C11): file I/O + local IPC    (10 benchmarks)
#   net/     (C13):   network stack             (5 benchmarks)
#   virt/    (C14+C15): virtualization + containers (3 benchmarks)

add_library(ssbbench_compute STATIC
    src/benchmarks/compute/int_hash.c src/benchmarks/compute/int_sort.c
    src/benchmarks/compute/int_parse.c src/benchmarks/compute/int_regex.c
    src/benchmarks/compute/fp_gemm.c src/benchmarks/compute/fp_fft.c
    src/benchmarks/compute/fp_conv.c src/benchmarks/compute/fp_distance.c
    src/benchmarks/compute/fp_ray.c
)
target_link_libraries(ssbbench_compute ssbcore)

add_library(ssbbench_crypto STATIC
    src/benchmarks/crypto/crypto_zstd.c src/benchmarks/crypto/crypto_aes.c
    src/benchmarks/crypto/crypto_hash.c src/benchmarks/crypto/crypto_rsa.c
    src/benchmarks/crypto/crypto_ed25519_sign.c src/benchmarks/crypto/crypto_x25519_ecdh.c
    src/benchmarks/crypto/crypto_chacha20_poly1305.c src/benchmarks/crypto/crypto_ecdsa_p256_sign.c
)
target_link_libraries(ssbbench_crypto ssbcore)

add_library(ssbbench_memory STATIC
    src/benchmarks/memory/mem_latency.c src/benchmarks/memory/mem_bandwidth.c
    src/benchmarks/memory/mem_stream.c src/benchmarks/memory/mem_random.c
    src/benchmarks/memory/numa_latency.c src/benchmarks/memory/numa_bandwidth.c
    src/benchmarks/memory/numa_false_sharing.c src/benchmarks/memory/numa_migration.c
)
target_link_libraries(ssbbench_memory ssbcore)

add_library(ssbbench_os STATIC
    src/benchmarks/os/proc_fork_exec.c src/benchmarks/os/proc_pthread.c
    src/benchmarks/os/proc_mmap.c
    src/benchmarks/os/cswitch_pipe.c src/benchmarks/os/cswitch_futex.c
    src/benchmarks/os/cswitch_smt.c
    src/benchmarks/os/script_shell.c src/benchmarks/os/script_python.c
    src/benchmarks/os/syscall_getpid.c src/benchmarks/os/syscall_vdso.c
    src/benchmarks/os/syscall_uring.c
)
target_link_libraries(ssbbench_os ssbcore)

add_library(ssbbench_sync STATIC
    src/benchmarks/sync/sync_mutex.c src/benchmarks/sync/sync_spinlock.c
    src/benchmarks/sync/sync_rwlock.c src/benchmarks/sync/sync_semaphore.c
    src/benchmarks/sync/sync_mpmc.c
)
target_link_libraries(ssbbench_sync ssbcore)

add_library(ssbbench_io STATIC
    src/benchmarks/io/fs_copy.c src/benchmarks/io/fs_seq_rw.c
    src/benchmarks/io/fs_rand_rw.c src/benchmarks/io/fs_fsync.c
    src/benchmarks/io/fs_meta.c src/benchmarks/io/fs_uring.c
    src/benchmarks/io/ipc_pipe.c src/benchmarks/io/ipc_unix.c
    src/benchmarks/io/ipc_eventfd.c src/benchmarks/io/ipc_signal.c
)
target_link_libraries(ssbbench_io ssbcore)

add_library(ssbbench_net STATIC
    src/benchmarks/net/net_tcp.c src/benchmarks/net/net_udp.c
    src/benchmarks/net/net_conn_rate.c src/benchmarks/net/net_latency.c
    src/benchmarks/net/net_zero_copy.c
)
target_link_libraries(ssbbench_net ssbcore)

add_library(ssbbench_virt STATIC
    src/benchmarks/virt/vm_detect.c src/benchmarks/virt/vm_cpu_overhead.c
    src/benchmarks/virt/ctr_lifecycle.c
)
target_link_libraries(ssbbench_virt ssbcore)

# Main executable - use whole-archive to keep benchmark constructors
add_executable(servmark src/main.c)
target_link_libraries(servmark
    -Wl,--whole-archive
    ssbbench_compute ssbbench_crypto ssbbench_memory
    ssbbench_os ssbbench_sync ssbbench_io
    ssbbench_net ssbbench_virt
    -Wl,--no-whole-archive
    ssbcore
)

install(TARGETS servmark DESTINATION bin)
