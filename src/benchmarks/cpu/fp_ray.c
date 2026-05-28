#include "benchmark.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define NUM_RAYS 200000
#define NUM_TRIS 500

typedef struct { float x, y, z; } vec3;
typedef struct { vec3 orig, dir; } ray;
typedef struct { vec3 v0, v1, v2; } tri;

typedef struct {
    ray *rays;
    tri *tris;
    int *hits;
} fp_ray_state_t;

static inline vec3 v3_sub(vec3 a, vec3 b) {
    return (vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}
static inline vec3 v3_cross(vec3 a, vec3 b) {
    return (vec3){a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
static inline float v3_dot(vec3 a, vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static int ray_tri_intersect(const ray *r, const tri *t) {
    vec3 e1 = v3_sub(t->v1, t->v0);
    vec3 e2 = v3_sub(t->v2, t->v0);
    vec3 h = v3_cross(r->dir, e2);
    float a = v3_dot(e1, h);
    if (a > -1e-7f && a < 1e-7f) return 0;
    float f = 1.0f / a;
    vec3 s = v3_sub(r->orig, t->v0);
    float u = f * v3_dot(s, h);
    if (u < 0.0f || u > 1.0f) return 0;
    vec3 q = v3_cross(s, e1);
    float v = f * v3_dot(r->dir, q);
    if (v < 0.0f || u + v > 1.0f) return 0;
    float td = f * v3_dot(e2, q);
    return td > 1e-7f ? 1 : 0;
}

static int fp_ray_init(void **state) {
    fp_ray_state_t *s = calloc(1, sizeof(*s));
    if (!s) return -1;
    s->rays = malloc(NUM_RAYS * sizeof(ray));
    s->tris = malloc(NUM_TRIS * sizeof(tri));
    s->hits = malloc(NUM_RAYS * sizeof(int));
    if (!s->rays || !s->tris || !s->hits) {
        free(s->rays); free(s->tris); free(s->hits); free(s);
        return -1;
    }
    srand(time(NULL));
    for (int i = 0; i < NUM_RAYS; i++) {
        s->rays[i].orig = (vec3){0, 0, -5};
        s->rays[i].dir = (vec3){(float)rand()/RAND_MAX - 0.5f,
                                 (float)rand()/RAND_MAX - 0.5f, 1.0f};
    }
    for (int i = 0; i < NUM_TRIS; i++) {
        float cx = ((float)rand()/RAND_MAX - 0.5f) * 2.0f;
        float cy = ((float)rand()/RAND_MAX - 0.5f) * 2.0f;
        float cz = ((float)rand()/RAND_MAX) * 5.0f;
        s->tris[i].v0 = (vec3){cx-0.1f, cy-0.1f, cz};
        s->tris[i].v1 = (vec3){cx+0.1f, cy-0.1f, cz};
        s->tris[i].v2 = (vec3){cx, cy+0.1f, cz};
    }
    *state = s;
    return 0;
}

static int fp_ray_warmup(void *state) {
    fp_ray_state_t *s = (fp_ray_state_t *)state;
    volatile int sink = 0;
    for (int i = 0; i < 1000; i++)
        sink += ray_tri_intersect(&s->rays[i % NUM_RAYS], &s->tris[i % NUM_TRIS]);
    __asm__ __volatile__("" : "+r"(sink));
    return 0;
}

static int fp_ray_measure(void *state, measurement_t *result) {
    fp_ray_state_t *s = (fp_ray_state_t *)state;
    struct timespec t0, t1;
    volatile int sink = 0;
    int64_t total_tests = 0;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < NUM_RAYS; i++) {
        for (int j = 0; j < NUM_TRIS; j++) {
            sink += ray_tri_intersect(&s->rays[i], &s->tris[j]);
            total_tests++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);

    s->hits[0] = sink;
    __asm__ __volatile__("" : "+r"(sink));

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    memset(result, 0, sizeof(*result));
    result->primary_metric = (double)total_tests / elapsed;
    result->wall_seconds = elapsed;
    return 0;
}

static int fp_ray_cleanup(void *state) {
    fp_ray_state_t *s = (fp_ray_state_t *)state;
    free(s->rays); free(s->tris); free(s->hits); free(s);
    return 0;
}

benchmark_t bench_fp_ray = {
    .name = "fp-ray",
    .category = "C2",
    .description = "Ray-triangle intersection (Moller-Trumbore)",
    .tier = 1,
    .primary_metric_name = "intersect/sec",
    .higher_is_better = true,
    .min_iterations = SSB_MIN_ITERATIONS,
    .max_iterations = SSB_MAX_ITERATIONS,
    .convergence_target = SSB_CONVERGENCE_TARGET,
    .min_runtime_sec = SSB_MIN_RUNTIME_SEC,
    .max_runtime_sec = SSB_MAX_RUNTIME_SEC,
    .cooldown_required = true,
    .init = fp_ray_init,
    .warmup = fp_ray_warmup,
    .measure = fp_ray_measure,
    .cleanup = fp_ray_cleanup,
    .num_threads = 1,
};
SSB_BENCHMARK_REGISTER(bench_fp_ray);
