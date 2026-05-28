#ifndef SERVSYSBENCH_H
#define SERVSYSBENCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define SSB_VERSION "1.0.0"
#define SSB_MAX_ITERATIONS 31
#define SSB_MIN_ITERATIONS 5
#define SSB_CONVERGENCE_TARGET 0.02
#define SSB_MIN_RUNTIME_SEC 10
#define SSB_MAX_RUNTIME_SEC 180
#define SSB_COOLDOWN_SEC 30
#define SSB_MAX_COOLDOWN_SEC 120
#define SSB_CV_STABLE 0.02
#define SSB_CV_MODERATE 0.05
#define SSB_CV_HIGH 0.10
#define SSB_BOOTSTRAP_RESAMPLES 10000
#define SSB_NUM_CPUS() ((int)sysconf(_SC_NPROCESSORS_ONLN))
#define SSB_PAGE_SIZE() ((size_t)sysconf(_SC_PAGESIZE))

typedef struct benchmark_s benchmark_t;
typedef struct harness_s harness_t;
typedef struct system_info_s system_info_t;
typedef struct run_result_s run_result_t;

#endif
