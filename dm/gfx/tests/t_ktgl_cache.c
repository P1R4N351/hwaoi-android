// Host unit, benchmark, and Delta #27 fault-injection test for vka_ktgl_cache.h.
#include "../vk_android/vka_ktgl_cache.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

typedef struct { uint64_t key; } mock_sh;
typedef struct { uintptr_t layout, set0; int push; uint32_t ndesc; } mock_layout;

static uint64_t ktgl_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t ktgl_bench_base(const mock_sh *sh, const uint64_t *serials, int n_prog, int n_draw, int n_frames,
                                uint64_t *locks_out, uint64_t *cs_locks_out) {
    struct { uint64_t serial; unsigned gen, bad; void *sh[VKA_KTGL_NSTAGES]; } pm[64] = {0};
    void *lsh[VKA_KTGL_NSTAGES] = {0};
    mock_layout llay = {0};
    int lok = 0;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    uint64_t locks = 0, cs_locks = 0, sink = 0, t0 = ktgl_now_ns();
    for (int f = 0; f < n_frames; f++) {
        for (int d = 0; d < n_draw; d++) {
            int p_idx = d % n_prog;
            uint64_t sr = serials[p_idx];
            unsigned h = (unsigned)((sr ^ (sr >> 6)) & 63u);
            if (pm[h].serial != sr) {
                pthread_mutex_lock(&mu); locks++;
                pthread_mutex_unlock(&mu);
                pm[h].serial = sr; pm[h].gen = 1; pm[h].bad = 0;
                pm[h].sh[0] = (void *)&sh[p_idx % 200];
                pm[h].sh[1] = (void *)&sh[200 + (d % 100)];
            }
            void *cur[VKA_KTGL_NSTAGES] = {pm[h].sh[0], pm[h].sh[1], NULL, NULL, NULL, NULL, NULL, NULL};
            if (!lok || memcmp(lsh, cur, sizeof lsh) != 0) {
                uint64_t h2 = 1469598103934665603ull;
                const uint8_t *bp = (const uint8_t *)cur;
                for (size_t i = 0; i < sizeof cur; i++) h2 = (h2 ^ bp[i]) * 1099511628211ull;
                pthread_mutex_lock(&mu); locks++;
                llay.layout = h2;
                pthread_mutex_unlock(&mu);
                memcpy(lsh, cur, sizeof lsh);
                lok = 1;
            }
            sink += llay.layout;
        }
        for (int c = 0; c < 120; c++) {
            uint64_t csr = 50000ull + (uint64_t)(c % 48);
            unsigned h = (unsigned)((csr ^ (csr >> 6)) & 63u);
            if (pm[h].serial != csr) { pthread_mutex_lock(&mu); locks++; pm[h].serial = csr; pm[h].sh[5] = (void *)&sh[360 + (c % 40)]; pthread_mutex_unlock(&mu); }
            pthread_mutex_lock(&mu); cs_locks++; pthread_mutex_unlock(&mu);
            sink += (uintptr_t)pm[h].sh[5];
        }
    }
    uint64_t dt = ktgl_now_ns() - t0;
    if (!sink) fprintf(stderr, "unexpected 0 sink\n");
    *locks_out = locks;
    *cs_locks_out = cs_locks;
    return dt;
}

static uint64_t ktgl_bench_opt(const mock_sh *sh, const uint64_t *serials, int n_prog, int n_draw, int n_frames,
                               uint64_t *steady_locks_out) {
    static vka_ktgl_pm_cache pm;
    static vka_ktgl_lay_cache lay_c;
    static vka_ktgl_cs_cache cs_c;
    memset(&pm, 0, sizeof pm); memset(&lay_c, 0, sizeof lay_c); memset(&cs_c, 0, sizeof cs_c);
    void *lsh[VKA_KTGL_NSTAGES] = {0};
    mock_layout llay = {0};
    int lok = 0;
    uint64_t steady_locks = 0, sink = 0, t0 = ktgl_now_ns();
    for (int f = 0; f < n_frames; f++) {
        for (int d = 0; d < n_draw; d++) {
            int p_idx = d % n_prog;
            uint64_t sr = serials[p_idx];
            void *st_sh[VKA_KTGL_NSTAGES] = {0};
            uint32_t bad = 0;
            if (!vka_ktgl_pm_get(&pm, sr, 1, 1u << 8, st_sh, &bad, 0)) {
                if (f > 0) steady_locks++;
                st_sh[0] = (void *)&sh[p_idx % 200];
                st_sh[1] = (void *)&sh[200 + (d % 100)];
                vka_ktgl_pm_put(&pm, sr, 1, st_sh, 0);
            }
            mock_layout lay = {0};
            if (lok && memcmp(lsh, st_sh, sizeof lsh) == 0) {
                lay = llay;
            } else if (vka_ktgl_lay_get(&lay_c, (const void *const *)st_sh, &lay, sizeof lay)) {
                memcpy(lsh, st_sh, sizeof lsh); llay = lay; lok = 1;
            } else {
                if (f > 0) steady_locks++;
                lay.layout = (uintptr_t)st_sh[0] ^ (uintptr_t)st_sh[1];
                vka_ktgl_lay_put(&lay_c, (const void *const *)st_sh, &lay, sizeof lay);
                memcpy(lsh, st_sh, sizeof lsh); llay = lay; lok = 1;
            }
            sink += lay.layout;
        }
        for (int c = 0; c < 120; c++) {
            uint64_t csr = 50000ull + (uint64_t)(c % 48);
            void *cs_sh[VKA_KTGL_NSTAGES] = {0};
            uint32_t bad = 0;
            if (!vka_ktgl_pm_get(&pm, csr, 1, 1u << 8, cs_sh, &bad, 0)) {
                if (f > 0) steady_locks++;
                cs_sh[5] = (void *)&sh[360 + (c % 40)];
                vka_ktgl_pm_put(&pm, csr, 1, cs_sh, 0);
            }
            mock_layout clay = {0};
            if (!vka_ktgl_cs_lay_get(&cs_c, cs_sh[5], &clay, sizeof clay)) {
                if (f > 0) steady_locks++;
                clay.layout = (uintptr_t)cs_sh[5];
                vka_ktgl_cs_lay_put(&cs_c, cs_sh[5], &clay, sizeof clay);
            }
            sink += clay.layout;
        }
    }
    uint64_t dt = ktgl_now_ns() - t0;
    if (!sink) fprintf(stderr, "unexpected 0 sink\n");
    *steady_locks_out = steady_locks;
    return dt;
}

static int test_layer_gen_and_barriers(int fault) {
    vka_ktgl_pm_cache pm = {0};
    mock_sh sh0 = {.key = 0x1234ull};
    void *in[VKA_KTGL_NSTAGES] = {&sh0, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    void *out[VKA_KTGL_NSTAGES] = {0};
    uint32_t bad = 0;
    vka_ktgl_pm_put(&pm, 777ull, 1u, in, 1u << 8);
    assert(vka_ktgl_pm_get(&pm, 777ull, 1u, 1u << 8, out, &bad, 0) == 1);
    if (vka_ktgl_pm_get(&pm, 777ull, 2u, 1u << 8, out, &bad, fault) != 0) {
        fprintf(stderr, "FAIL: stale VKA_BAD_MISSING entry survived layer_gen increment (fault=%d)\n", fault);
        return 1;
    }
    vka_ktgl_bar_spec s1 = vka_ktgl_qdx_bar_spec(1, fault);
    vka_ktgl_bar_spec s2 = vka_ktgl_qdx_bar_spec(2, fault);
    if (s1.src_stage != 0x00001000u || s1.dst_stage != 0x00000800u ||
        s2.src_stage != 0x00000800u || s2.dst_stage != 0x00000002u) {
        fprintf(stderr, "FAIL: qdx barrier stages too wide or wrong (fault=%d s1=0x%x->0x%x s2=0x%x->0x%x)\n",
                fault, s1.src_stage, s1.dst_stage, s2.src_stage, s2.dst_stage);
        return 1;
    }
    return 0;
}

int main(void) {
    const char *fe = getenv("T_KTGL_CACHE_FAULT");
    int fault = fe ? atoi(fe) : 0;
    if (test_layer_gen_and_barriers(fault) != 0) return 1;

    enum { NSH = 400, NPROG = 400, NDRAW = 800, NFRAMES = 500 };
    static mock_sh sh[NSH];
    static uint64_t serials[NPROG];
    for (int i = 0; i < NSH; i++) sh[i].key = 0x100000ull + (uint64_t)i;
    for (int i = 0; i < NPROG; i++) serials[i] = 1000ull + (uint64_t)i * 64ull;   /* modulo-64 collision stress */

    uint64_t base_locks = 0, base_cs_locks = 0, opt_steady = 0;
    uint64_t base_ns = ktgl_bench_base(sh, serials, NPROG, NDRAW, NFRAMES, &base_locks, &base_cs_locks);
    uint64_t opt_ns = ktgl_bench_opt(sh, serials, NPROG, NDRAW, NFRAMES, &opt_steady);
    double total_ops = (double)NFRAMES * (double)(NDRAW + 120);
    printf("aoi_ktgl_cache: frames=%d draws=%d cs=%d base_locks=%llu base_cs_locks=%llu opt_steady_locks=%llu base_ns_op=%.2f opt_ns_op=%.2f speedup=%.2fx\n",
           NFRAMES, NDRAW, 120, (unsigned long long)base_locks, (unsigned long long)base_cs_locks,
           (unsigned long long)opt_steady, base_ns / total_ops, opt_ns / total_ops,
           opt_ns ? (double)base_ns / (double)opt_ns : 0.0);
    assert(opt_steady * 100ull < base_locks + base_cs_locks);
    assert(base_locks > 100000);
    return 0;
}
