#define _POSIX_C_SOURCE 200809L
#define W19_TEST_HOOKS 1
#include "../nvn/nvn_hle_fast.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

int g_w19_fault = 0;
volatile uint64_t g_w19_sink = 0;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static int legacy_shp_lower(const _Atomic uint64_t *lo, int n, uint64_t a) {
    int L = 0, H = n;
    while (L < H) {
        int m = (L + H) / 2;
        if (atomic_load_explicit(&lo[m], memory_order_relaxed) < a) L = m + 1;
        else H = m;
    }
    return L;
}

static int legacy_shp_find(const _Atomic uint64_t *lo, const _Atomic uint64_t *hi,
                           const _Atomic int *n_p, const _Atomic uint32_t *seq_p,
                           uint64_t a, uint64_t sz, uint64_t *out_lo, uint64_t *out_hi,
                           uint64_t *gap_lo, uint64_t *gap_hi, uint32_t *seq_out) {
    uint64_t e = a + (sz ? sz : 1u);
    if (e < a) e = UINT64_MAX;
    for (;;) {
        uint32_t s = atomic_load_explicit(seq_p, memory_order_acquire);
        if (s & 1u) continue;
        int c = atomic_load_explicit(n_p, memory_order_relaxed);
        int idx = legacy_shp_lower(lo, c, e);
        int i = idx - 1, hit = 0;
        uint64_t l = 0, h = 0;
        if (i >= 0) {
            l = atomic_load_explicit(&lo[i], memory_order_relaxed);
            h = atomic_load_explicit(&hi[i], memory_order_relaxed);
            hit = h > a;
        }
        uint64_t next_lo = idx < c ? atomic_load_explicit(&lo[idx], memory_order_relaxed) : UINT64_MAX;
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(seq_p, memory_order_relaxed) == s) {
            if (hit && out_lo && out_hi) { *out_lo = l; *out_hi = h; }
            if (gap_lo && gap_hi) { *gap_lo = i >= 0 ? h : 0u; *gap_hi = next_lo; }
            if (seq_out) *seq_out = s;
            return hit;
        }
    }
}

static int build_205_shader_pools(_Atomic uint64_t lo[256], _Atomic uint64_t hi[256],
                                  _Atomic int *n_p, _Atomic uint32_t *seq_p,
                                  nvn_shp_summary *sum) {
    int n = 205;
    uint64_t base = 0x1000000000ull;
    for (int i = 0; i < n; i++) {
        uint64_t sz = (i == 100) ? (244ull << 20) : ((uint64_t)(1u + (i % 4u)) << 19);
        atomic_store_explicit(&lo[i], base, memory_order_relaxed);
        atomic_store_explicit(&hi[i], base + sz, memory_order_relaxed);
        base += sz + (i % 3u == 0u ? (32ull << 20) : (2ull << 20));
    }
    atomic_store_explicit(n_p, n, memory_order_relaxed);
    uint32_t s = atomic_load_explicit(seq_p, memory_order_relaxed);
    atomic_store_explicit(seq_p, s + 1u, memory_order_release);
    nvn_shp_summary_rebuild(sum, lo, hi, n);
    atomic_store_explicit(seq_p, s + 2u, memory_order_release);
    return n;
}

static int test_shp_equiv(void) {
    static _Atomic uint64_t lo[256], hi[256];
    _Atomic int n = 0;
    _Atomic uint32_t seq = 2u;
    nvn_shp_summary sum = {UINT64_MAX, 0, {0, 0, 0, 0}};
    nvn_shp_neg_cache neg = {0};
    if (!nvn_shp_fast_out(&sum, &neg, &seq, 0x2000000000ull, 64u)) return 1;
    (void)build_205_shader_pools(lo, hi, &n, &seq, &sum);
    uint64_t min_l = atomic_load_explicit(&lo[0], memory_order_relaxed);
    uint64_t max_h = atomic_load_explicit(&hi[204], memory_order_relaxed);
    uint64_t probes[10][2] = {
        {0x0080000000ull, 4096u},
        {max_h, 256u},
        {atomic_load_explicit(&hi[0], memory_order_relaxed), 64u},
        {atomic_load_explicit(&hi[99], memory_order_relaxed), 0u},
        {atomic_load_explicit(&lo[100], memory_order_relaxed) + 4096u, 128u},
        {atomic_load_explicit(&lo[100], memory_order_relaxed) + (240ull << 20), 64u},
        {min_l - 64u, 64u},
        {min_l - 64u, 128u},
        {atomic_load_explicit(&hi[100], memory_order_relaxed) + 128u, 0u},
        {atomic_load_explicit(&hi[150], memory_order_relaxed) - 16u, 32u},
    };
    for (int r = 0; r < 3; r++) {
        for (int p = 0; p < 10; p++) {
            uint64_t l = 0, h = 0, gl = 0, gh = 0;
            uint32_t s = 0;
            int ref = legacy_shp_find(lo, hi, &n, &seq, probes[p][0], probes[p][1], &l, &h, &gl, &gh, &s);
            int fast_out = nvn_shp_fast_out(&sum, &neg, &seq, probes[p][0], probes[p][1]);
            if (fast_out) {
                if (ref != 0) { fprintf(stderr, "FAIL: shp_fast_out false skip p=%d (FAULT=%d)\n", p, g_w19_fault); return 1; }
            } else if (!ref) {
                nvn_shp_neg_put(&neg, s, gl, gh, probes[p][0]);
            }
        }
    }
    if (!nvn_shp_fast_out(&sum, &neg, &seq, atomic_load_explicit(&hi[0], memory_order_relaxed), 64u)) return 1;
    uint32_t s = atomic_load_explicit(&seq, memory_order_relaxed);
    atomic_store_explicit(&seq, s + 2u, memory_order_release);
    atomic_store_explicit(&lo[205], 0x2000000000ull, memory_order_relaxed);
    atomic_store_explicit(&hi[205], 0x2000100000ull, memory_order_relaxed);
    atomic_store_explicit(&n, 206, memory_order_relaxed);
    nvn_shp_summary_rebuild(&sum, lo, hi, 206);
    if (nvn_shp_fast_out(&sum, &neg, &seq, 0x2000001000ull, 64u)) return 1;
    return 0;
}

typedef struct { const void *p; uint64_t gen; unsigned flags; int sparse; } sim_pool;

static int test_plive_equiv(void) {
    nvn_pl_sync sync = {0, 1u};
    nvn_plive_memo4 memo = {0};
    sim_pool p = {&p, 42u, 0x208u, 1};
    uint32_t w0 = 0; uint64_t e0 = 0;
    nvn_pl_snap(&sync, &w0, &e0);
    nvn_plive_fast_put(&memo, &sync, w0, e0, (uintptr_t)p.p, p.gen, p.flags, p.sparse);
    unsigned fl = 0; int sp = 0; uint64_t g = 0;
    if (!nvn_plive_fast_get(&memo, &sync, (uintptr_t)p.p, &fl, &sp, &g)) return 1;
    if (g != 42u || fl != 0x208u || sp != 1) return 1;

    nvn_pl_wbegin(&sync);
    if (nvn_plive_fast_get(&memo, &sync, (uintptr_t)p.p, &fl, &sp, &g)) {
        fprintf(stderr, "FAIL: plive hit while writer active (FAULT=%d)\n", g_w19_fault);
        return 1;
    }
    p.sparse = 0;
    nvn_pl_wend(&sync);
    if (nvn_plive_fast_get(&memo, &sync, (uintptr_t)p.p, &fl, &sp, &g)) {
        fprintf(stderr, "FAIL: stale plive hit after vpool transition (FAULT=%d)\n", g_w19_fault);
        return 1;
    }
    nvn_pl_snap(&sync, &w0, &e0);
    nvn_plive_fast_put(&memo, &sync, w0, e0, (uintptr_t)p.p, p.gen, p.flags, p.sparse);
    if (!nvn_plive_fast_get(&memo, &sync, (uintptr_t)p.p, &fl, &sp, &g) || sp != 0) return 1;

    nvn_pl_wbegin(&sync);
    p.gen = 0;
    nvn_pl_wend(&sync);
    if (nvn_plive_fast_get(&memo, &sync, (uintptr_t)p.p, &fl, &sp, &g)) {
        fprintf(stderr, "FAIL: stale plive hit after finalize (FAULT=%d)\n", g_w19_fault);
        return 1;
    }
    return 0;
}

static int test_hostseg_equiv(void) {
    nvn_hostseg_summary sum = {0};
    uint64_t w[4] = {0, 0, 0, 0};
    nvn_hostseg_summary_publish(&sum, 0u, UINTPTR_MAX, 0u, w);
    if (nvn_hostseg_may_overlap(&sum, 0x30000000ull, 0x30010000ull)) return 1;

    nvn_hostseg_mark_chunks(w, 0x40000000ull, 0x40020000ull);
    nvn_hostseg_summary_publish(&sum, 1u, 0x40000000ull, 0x40020000ull, w);
    if (nvn_hostseg_may_overlap(&sum, 0x3f000000ull, 0x3f010000ull)) return 1;
    if (nvn_hostseg_may_overlap(&sum, 0x40000000ull, 0x40020000ull) != 1) {
        fprintf(stderr, "FAIL: hostseg overlapping reclaim skipped (FAULT=%d)\n", g_w19_fault);
        return 1;
    }
    if (nvn_hostseg_may_overlap(&sum, 0x3ff00000ull, 0x40200000ull) != 1) return 1;
    return 0;
}

static void bench_shp_bind(void) {
    static _Atomic uint64_t lo[256], hi[256];
    _Atomic int n = 0;
    _Atomic uint32_t seq = 2u;
    nvn_shp_summary sum = {UINT64_MAX, 0, {0, 0, 0, 0}};
    nvn_shp_neg_cache neg = {0};
    build_205_shader_pools(lo, hi, &n, &seq, &sum);

    uint64_t addrs[4] = {
        atomic_load_explicit(&hi[0], memory_order_relaxed) + 4096u,
        atomic_load_explicit(&hi[99], memory_order_relaxed) + 4096u,
        atomic_load_explicit(&hi[140], memory_order_relaxed) + 4096u,
        atomic_load_explicit(&hi[204], memory_order_relaxed) + 4096u,
    };
    const int ITERS = 400000;
    uint64_t leg_acc = 0, fast_acc = 0;
    double t0 = now_ms();
    for (int i = 0; i < ITERS; i++) {
        uint64_t a = addrs[i & 3] + ((uint64_t)(i & 63) * 64ull);
        uint64_t l = 0, h = 0;
        leg_acc += (uint64_t)legacy_shp_find(lo, hi, &n, &seq, a, 256u, &l, &h, NULL, NULL, NULL);
    }
    double ms_leg = now_ms() - t0;

    double t1 = now_ms();
    for (int i = 0; i < ITERS; i++) {
        uint64_t a = addrs[i & 3] + ((uint64_t)(i & 63) * 64ull);
        if (!nvn_shp_fast_out(&sum, &neg, &seq, a, 256u)) {
            uint64_t l = 0, h = 0, gl = 0, gh = 0;
            uint32_t s = 0;
            int hit = legacy_shp_find(lo, hi, &n, &seq, a, 256u, &l, &h, &gl, &gh, &s);
            if (!hit) nvn_shp_neg_put(&neg, s, gl, gh, a);
            fast_acc += (uint64_t)hit;
        }
    }
    double ms_fast = now_ms() - t1;
    g_w19_sink ^= leg_acc ^ fast_acc;
    printf("aoi_w19_shp_bind: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, ms_leg, ms_fast, ms_leg / (ms_fast > 0.001 ? ms_fast : 0.001),
           (unsigned long long)(leg_acc ^ fast_acc));
}

typedef struct { const void *p; uint64_t gen; unsigned flags; int sparse; } leg_pl_ent;

static void init_plive_table(leg_pl_ent tab[], uint32_t cap, uintptr_t keys[64], nvn_hostseg_summary *hsum) {
    uint64_t hw[4] = {0, 0, 0, 0};
    nvn_hostseg_summary_publish(hsum, 0u, UINTPTR_MAX, 0u, hw);
    memset(tab, 0, (size_t)cap * sizeof *tab);
    for (int i = 0; i < 64; i++) {
        keys[i] = 0x50000000ull + (uintptr_t)i * 4096u;
        uint32_t idx = (uint32_t)(((keys[i] * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1u));
        while (tab[idx].p) idx = (idx + 1u) & (cap - 1u);
        tab[idx] = (leg_pl_ent){(const void *)keys[i], (uint64_t)(i + 1), 0x10u, i & 1};
    }
}

static uint64_t leg_plive_probe(leg_pl_ent tab[], uint32_t cap, pthread_mutex_t *mu,
                                pthread_mutex_t *hmu, uintptr_t k) {
    pthread_mutex_lock(mu);
    uint32_t idx = (uint32_t)(((k * 0x9E3779B97F4A7C15ull) >> 32) & (cap - 1u));
    while (tab[idx].p && (uintptr_t)tab[idx].p != k) idx = (idx + 1u) & (cap - 1u);
    uint64_t v = tab[idx].gen + (uint64_t)tab[idx].sparse;
    uint32_t f = tab[idx].flags;
    pthread_mutex_unlock(mu);
    pthread_mutex_lock(hmu);
    v += (uint64_t)(f & 1u);
    pthread_mutex_unlock(hmu);
    return v;
}

static void bench_plive_hostseg(void) {
    enum { TAB_N = 1024, ITERS = 200000 };
    static leg_pl_ent tab[TAB_N];
    static uintptr_t keys[64];
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER, hmu = PTHREAD_MUTEX_INITIALIZER;
    nvn_pl_sync sync = {0, 1u};
    nvn_plive_memo4 memo = {0};
    nvn_hostseg_summary hsum = {0};
    init_plive_table(tab, TAB_N, keys, &hsum);

    uint64_t leg_acc = 0, fast_acc = 0;
    double t0 = now_ms();
    for (int i = 0; i < ITERS; i++) leg_acc += leg_plive_probe(tab, TAB_N, &mu, &hmu, keys[i & 63]);
    double ms_leg = now_ms() - t0;

    double t1 = now_ms();
    for (int i = 0; i < ITERS; i++) {
        uintptr_t k = keys[i & 63];
        unsigned fl = 0; int sp = 0; uint64_t g = 0;
        if (!nvn_plive_fast_get(&memo, &sync, k, &fl, &sp, &g)) {
            uint32_t w0 = 0; uint64_t e0 = 0;
            nvn_pl_snap(&sync, &w0, &e0);
            pthread_mutex_lock(&mu);
            uint32_t idx = (uint32_t)(((k * 0x9E3779B97F4A7C15ull) >> 32) & (TAB_N - 1));
            while (tab[idx].p && (uintptr_t)tab[idx].p != k) idx = (idx + 1u) & (TAB_N - 1);
            g = tab[idx].gen; fl = tab[idx].flags; sp = tab[idx].sparse;
            pthread_mutex_unlock(&mu);
            nvn_plive_fast_put(&memo, &sync, w0, e0, k, g, fl, sp);
        }
        fast_acc += g + (uint64_t)sp;
        if (nvn_hostseg_may_overlap(&hsum, 0x60000000ull + (uintptr_t)(i & 15) * 4096u,
                                    0x60001000ull + (uintptr_t)(i & 15) * 4096u)) {
            pthread_mutex_lock(&hmu);
            fast_acc += 1u;
            pthread_mutex_unlock(&hmu);
        }
    }
    double ms_fast = now_ms() - t1;
    g_w19_sink ^= leg_acc ^ fast_acc;
    printf("aoi_w19_plive_hostseg: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, ms_leg, ms_fast, ms_leg / (ms_fast > 0.001 ? ms_fast : 0.001),
           (unsigned long long)(leg_acc ^ fast_acc));
}

int main(void) {
    const char *f = getenv("T_W19_FAULT");
    g_w19_fault = f ? atoi(f) : 0;
    if (test_shp_equiv() != 0) return 1;
    if (test_plive_equiv() != 0) return 1;
    if (test_hostseg_equiv() != 0) return 1;
    if (!g_w19_fault) {
        bench_shp_bind();
        bench_plive_hostseg();
    }
    printf("t_hle_fast_w19: PASS (fault=%d)\n", g_w19_fault);
    return 0;
}
