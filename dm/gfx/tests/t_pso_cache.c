// Host unit, benchmark, and Delta #27 fault-injection test for vka_pso_cache.h.
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "../vk_android/vka_pso_cache.h"
#include "../vk_android/vka_gkey.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { void *sh[8]; uintptr_t layout; vka_gkey k; } mock_gkey_full;
typedef struct { _Atomic uintptr_t p; _Atomic uint8_t linked, busy; } mock_gval;
typedef struct { const void *cs; uintptr_t layout; uint64_t um, sm; } mock_ckey;

_Static_assert(offsetof(mock_gkey_full, k.va) <= VKA_PSO_FAST_KEY_MAX, "fast key fit");
_Static_assert(sizeof(mock_gkey_full) <= VKA_PSO_FULL_KEY_MAX, "full key fit");

static uint64_t pso_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t pso_fnv(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint64_t h = 1469598103934665603ull;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 29;
    }
    for (; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h ^ (h >> 32);
}

static void fill_gfx_key(mock_gkey_full *fk, uintptr_t *sh, int idx, int vin_static) {
    size_t len = vin_static ? sizeof *fk : offsetof(mock_gkey_full, k.va);
    memset(fk, 0, len);
    fk->sh[0] = (void *)sh[idx % 120];
    fk->sh[1] = (void *)sh[120 + (idx % 96)];
    fk->layout = (uintptr_t)(0x2000u + (unsigned)(idx % 32));
    fk->k.cfmt[0] = 37u + (uint32_t)(idx % 7);
    fk->k.ncolor = 1;
    fk->k.samples = 1;
    fk->k.topo = 3;
    fk->k.vin_static = (uint32_t)vin_static;
    if (vin_static) fk->k.va[3].used = 1, fk->k.va[3].offset = (uint32_t)idx;
}

typedef struct {
    mock_gval gvals[512];
    uint64_t gkeys[512];
    mock_gkey_full gmem[512];
    size_t g_n;
    mock_gval cs_gvals[128];
    mock_ckey cs_keys[128];
    size_t cs_n;
    pthread_mutex_t mu;
    uint64_t mutex_locks;
} pso_mock_store;

static mock_gval *store_gfx_obtain(pso_mock_store *st, const mock_gkey_full *fk, uint64_t h, size_t len) {
    pthread_mutex_lock(&st->mu);
    st->mutex_locks++;
    for (size_t i = 0; i < st->g_n; i++) {
        if (st->gkeys[i] == h && !memcmp(&st->gmem[i], fk, len)) {
            pthread_mutex_unlock(&st->mu);
            return &st->gvals[i];
        }
    }
    size_t id = st->g_n++;
    assert(id < 512);
    st->gkeys[id] = h;
    memcpy(&st->gmem[id], fk, len);
    atomic_store(&st->gvals[id].p, (uintptr_t)(0x100000u + id * 16u));
    atomic_store(&st->gvals[id].busy, 0);
    pthread_mutex_unlock(&st->mu);
    return &st->gvals[id];
}

static mock_gval *store_cs_obtain(pso_mock_store *st, const void *cs, uintptr_t layout) {
    pthread_mutex_lock(&st->mu);
    st->mutex_locks++;
    for (size_t i = 0; i < st->cs_n; i++) {
        if (st->cs_keys[i].cs == cs && st->cs_keys[i].layout == layout) {
            pthread_mutex_unlock(&st->mu);
            return &st->cs_gvals[i];
        }
    }
    size_t id = st->cs_n++;
    assert(id < 128);
    st->cs_keys[id] = (mock_ckey){cs, layout, 0, 0};
    atomic_store(&st->cs_gvals[id].p, (uintptr_t)(0x200000u + id * 16u));
    atomic_store(&st->cs_gvals[id].busy, 0);
    pthread_mutex_unlock(&st->mu);
    return &st->cs_gvals[id];
}

static uint64_t run_bench(int use_memo, pso_mock_store *st, uintptr_t *sh, int n_frames,
                          uint64_t *cold_locks, uint64_t *steady_locks, uint64_t *sink_out) {
    enum { N_GFX = 320, N_DRAWS = 800, N_CS = 48, N_DISP = 160 };
    static vka_pso_fast_cache fast_c;
    static vka_pso_full_cache full_c;
    static vka_pso_cs_cache cs_c;
    memset(&fast_c, 0, sizeof fast_c);
    memset(&full_c, 0, sizeof full_c);
    memset(&cs_c, 0, sizeof cs_c);
    mock_gkey_full fk, last;
    mock_gval *last_g = NULL;
    size_t last_len = 0;
    uint64_t t0 = 0, sink = 0;
    for (int f = 0; f < n_frames; f++) {
        if (f == 1) { *cold_locks = st->mutex_locks; t0 = pso_now_ns(); }
        for (int d = 0; d < N_DRAWS; d++) {
            int idx = (d * 7 + f) % N_GFX;
            size_t len = offsetof(mock_gkey_full, k.va);
            fill_gfx_key(&fk, sh, idx, 0);
            mock_gval *g = NULL;
            if (last_g && last_len == len && !memcmp(&fk, &last, len)) {
                g = last_g;
            } else {
                uint64_t h = pso_fnv(&fk, len);
                g = use_memo ? vka_pso_gfx_get(&fast_c, &full_c, &fk, (uint32_t)len, h) : NULL;
                if (!g || atomic_load_explicit(&g->busy, memory_order_acquire)) {
                    g = store_gfx_obtain(st, &fk, h, len);
                    if (use_memo) vka_pso_gfx_put(&fast_c, &full_c, &fk, (uint32_t)len, h, g);
                }
                memcpy(&last, &fk, len); last_len = len; last_g = g;
            }
            sink ^= atomic_load_explicit(&g->p, memory_order_acquire);
        }
        for (int c = 0; c < N_DISP; c++) {
            int cid = (c * 5 + f) % N_CS;
            const void *cs = (const void *)sh[240 + cid];
            uintptr_t lay = (uintptr_t)(0x4000u + (unsigned)(cid % 8));
            mock_gval *g = use_memo ? vka_pso_cs_get(&cs_c, cs, lay, 0, 0) : NULL;
            if (!g || atomic_load_explicit(&g->busy, memory_order_acquire)) {
                g = store_cs_obtain(st, cs, lay);
                if (use_memo && !atomic_load_explicit(&g->busy, memory_order_acquire))
                    vka_pso_cs_put(&cs_c, cs, lay, 0, 0, g);
            }
            sink ^= atomic_load_explicit(&g->p, memory_order_acquire);
        }
    }
    uint64_t dt = pso_now_ns() - t0;
    *steady_locks = st->mutex_locks - *cold_locks;
    *sink_out = sink;
    return dt;
}

static int verify_semantics(int fault) {
    static vka_pso_fast_cache fast_c;
    static vka_pso_full_cache full_c;
    static vka_pso_cs_cache cs_c;
    uintptr_t sh[300];
    for (int i = 0; i < 300; i++) sh[i] = 0x500000ull + (uint64_t)i * 64u;
    mock_gkey_full k1, k2;
    size_t len = offsetof(mock_gkey_full, k.va);
    fill_gfx_key(&k1, sh, 1, 0);
    fill_gfx_key(&k2, sh, 1, 0);
    k2.k.att[0].en = 1;
    mock_gval g1 = {0}, g2 = {0};
    atomic_store(&g1.p, 0x1111u);
    atomic_store(&g2.p, 0x2222u);
    uint32_t cmp_len = fault == 1 ? 64u : (uint32_t)len;
    uint64_t h1 = pso_fnv(&k1, cmp_len);
    uint64_t h2 = pso_fnv(&k2, cmp_len);
    vka_pso_gfx_put(&fast_c, &full_c, &k1, cmp_len, h1, &g1);
    void *hit = vka_pso_gfx_get(&fast_c, &full_c, &k2, cmp_len, h2);
    if (hit == &g1) { fprintf(stderr, "FAIL: alias matched g1 on key difference (fault=%d)\n", fault); return 1; }
    vka_pso_gfx_put(&fast_c, &full_c, &k2, (uint32_t)len, h2, &g2);
    assert(vka_pso_gfx_get(&fast_c, &full_c, &k2, (uint32_t)len, h2) == &g2);
    /* GPL upgrade in place: live atomic read through cached gval must observe new pipeline. */
    uintptr_t snap = atomic_load(&g1.p);
    atomic_store_explicit(&g1.p, 0x9999u, memory_order_release);
    mock_gval *ghit = vka_pso_gfx_get(&fast_c, &full_c, &k1, (uint32_t)len, h1);
    uintptr_t observed = fault == 2 ? snap : atomic_load_explicit(&ghit->p, memory_order_acquire);
    if (observed != 0x9999u) { fprintf(stderr, "FAIL: stale pipeline %#llx after GPL upgrade (fault=%d)\n", (unsigned long long)observed, fault); return 1; }
    /* Busy sync entry must not satisfy sync cs hit without fallback. */
    mock_gval cg = {0};
    atomic_store(&cg.busy, 1);
    if (fault == 3) vka_pso_cs_put(&cs_c, (const void *)sh[10], 0x3333u, 0, 0, &cg);
    void *chit = vka_pso_cs_get(&cs_c, (const void *)sh[10], 0x3333u, 0, 0);
    if (chit && atomic_load(&((mock_gval *)chit)->busy)) { fprintf(stderr, "FAIL: busy CS entry cached while busy=1 (fault=%d)\n", fault); return 1; }
    /* Static VIN full-key path check. */
    fill_gfx_key(&k1, sh, 7, 1);
    size_t flen = sizeof k1;
    uint64_t fh = pso_fnv(&k1, flen);
    vka_pso_gfx_put(&fast_c, &full_c, &k1, (uint32_t)flen, fh, &g1);
    assert(vka_pso_gfx_get(&fast_c, &full_c, &k1, (uint32_t)flen, fh) == &g1);
    return 0;
}

int main(void) {
    const char *fe = getenv("T_PSO_CACHE_FAULT");
    int fault = fe ? atoi(fe) : 0;
    if (verify_semantics(fault) != 0) return 1;
    uintptr_t sh[300];
    for (int i = 0; i < 300; i++) sh[i] = 0x700000ull + (uint64_t)i * 64u;
    pso_mock_store b_st = {.mu = PTHREAD_MUTEX_INITIALIZER};
    pso_mock_store o_st = {.mu = PTHREAD_MUTEX_INITIALIZER};
    enum { FRAMES = 500, OPS_PER_FRAME = 960 };
    uint64_t bc = 0, bs = 0, oc = 0, os = 0, b_sink = 0, o_sink = 0;
    uint64_t b_ns = run_bench(0, &b_st, sh, FRAMES, &bc, &bs, &b_sink);
    uint64_t o_ns = run_bench(1, &o_st, sh, FRAMES, &oc, &os, &o_sink);
    assert(b_sink == o_sink);
    assert(os == 0);
    uint64_t steady_ops = (uint64_t)(FRAMES - 1) * OPS_PER_FRAME;
    printf("pso_cache_bench: frames=%d (%llu steady ops: 800 gfx + 160 cs/frame)\n", FRAMES, (unsigned long long)steady_ops);
    printf("  baseline: steady_locks=%llu time_ms=%.3f ns_per_op=%.2f\n", (unsigned long long)bs, b_ns / 1e6, (double)b_ns / steady_ops);
    printf("  cached  : steady_locks=%llu time_ms=%.3f ns_per_op=%.2f speedup=%.2fx\n", (unsigned long long)os, o_ns / 1e6, (double)o_ns / steady_ops, (double)b_ns / (o_ns ? o_ns : 1));
    printf("t_pso_cache PASS (fault=%d)\n", fault);
    return 0;
}
