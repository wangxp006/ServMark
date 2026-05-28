cmake_minimum_required(VERSION 3.16)
project(ServSysBench VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BASELINE_CFLAGS "-O2 -g -fno-omit-frame-pointer -fno-lto")
set(CMAKE_C_FLAGS_RELEASE "${BASELINE_CFLAGS}")

option(SSB_USE_MARCH_NATIVE "Use -march=native (non-canonical)" OFF)
if(SSB_USE_MARCH_NATIVE)
    add_definitions(-DSSB_MARCH_NATIVE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED hwloc)
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
    ${CMAKE_SOURCE_DIR}/include
    ${HWLOC_INCLUDE_DIRS} ${NUMA_INCLUDE_DIRS}
    ${LIBCRYPTO_INCLUDE_DIRS} ${ZSTD_INCLUDE_DIRS}
)
target_link_libraries(ssbcore PUBLIC
    ${HWLOC_LIBRARIES} ${NUMA_LIBRARIES} ${LIBCRYPTO_LIBRARIES}
    ${ZSTD_LIBRARIES} ${PTHREAD_LIBRARIES} ${MATH_LIBRARIES} dl rt
)

# Benchmark libraries
add_library(ssbbench_cpu STATIC
    src/benchmarks/cpu/int_hash.c src/benchmarks/cpu/int_sort.c
    src/benchmarks/cpu/int_parse.c src/benchmarks/cpu/int_regex.c
    src/benchmarks/cpu/fp_gemm.c src/benchmarks/cpu/fp_fft.c
    src/benchmarks/cpu/fp_conv.c src/benchmarks/cpu/fp_distance.c
    src/benchmarks/cpu/fp_ray.c src/benchmarks/cpu/crypto_zstd.c
    src/benchmarks/cpu/crypto_aes.c src/benchmarks/cpu/crypto_hash.c
    src/benchmarks/cpu/crypto_rsa.c
)
target_link_libraries(ssbbench_cpu ssbcore)

add_library(ssbbench_memory STATIC
    src/benchmarks/memory/mem_latency.c src/benchmarks/memory/mem_bandwidth.c
    src/benchmarks/memory/mem_stream.c src/benchmarks/memory/mem_random.c
    src/benchmarks/memory/numa_latency.c src/benchmarks/memory/numa_bandwidth.c
    src/benchmarks/memory/numa_false_sharing.c src/benchmarks/memory/numa_migration.c
)
target_link_libraries(ssbbench_memory ssbcore)

add_library(ssbbench_process STATIC
    src/benchmarks/process/proc_fork_exec.c src/benchmarks/process/proc_pthread.c
    src/benchmarks/process/proc_mmap.c src/benchmarks/process/sync_mutex.c
    src/benchmarks/process/sync_spinlock.c src/benchmarks/process/sync_rwlock.c
    src/benchmarks/process/sync_semaphore.c src/benchmarks/process/sync_mpmc.c
    src/benchmarks/process/cswitch_pipe.c src/benchmarks/process/cswitch_futex.c
    src/benchmarks/process/cswitch_smt.c src/benchmarks/process/script_shell.c
    src/benchmarks/process/script_python.c
)
target_link_libraries(ssbbench_process ssbcore)

add_library(ssbbench_io STATIC
    src/benchmarks/io/fs_copy.c src/benchmarks/io/fs_seq_rw.c
    src/benchmarks/io/fs_rand_rw.c src/benchmarks/io/fs_fsync.c
    src/benchmarks/io/fs_meta.c src/benchmarks/io/fs_uring.c
    src/benchmarks/io/ipc_pipe.c src/benchmarks/io/ipc_unix.c
    src/benchmarks/io/ipc_eventfd.c src/benchmarks/io/ipc_signal.c
    src/benchmarks/io/syscall_getpid.c src/benchmarks/io/syscall_vdso.c
    src/benchmarks/io/syscall_uring.c
)
target_link_libraries(ssbbench_io ssbcore)

add_library(ssbbench_net_virt STATIC
    src/benchmarks/net_virt/net_tcp.c src/benchmarks/net_virt/net_udp.c
    src/benchmarks/net_virt/net_conn_rate.c src/benchmarks/net_virt/net_latency.c
    src/benchmarks/net_virt/net_zero_copy.c src/benchmarks/net_virt/vm_detect.c
    src/benchmarks/net_virt/vm_cpu_overhead.c src/benchmarks/net_virt/ctr_lifecycle.c
)
target_link_libraries(ssbbench_net_virt ssbcore)

# Main executable - use whole-archive to keep benchmark constructors
add_executable(servsysbench src/main.c)
target_link_libraries(servsysbench
    -Wl,--whole-archive
    ssbbench_cpu ssbbench_memory ssbbench_process ssbbench_io ssbbench_net_virt
    -Wl,--no-whole-archive
    ssbcore
)

install(TARGETS servsysbench DESTINATION bin)
install(DIRECTORY reference/ DESTINATION share/servsysbench/reference)
