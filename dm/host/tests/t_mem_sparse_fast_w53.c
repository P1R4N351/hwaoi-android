// Host unit test and benchmark for Wave 53 (Opt 55: vka_mem_sparse_fast.h).
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vka_mem_sparse_fast.h"

_Atomic int g_vka_mem_sparse_fast_override = -1;
_Atomic int g_aoi_w53_fault = -1;

typedef struct {
    uintptr_t mem;
    uint64_t size, id, pbytes;
    int lazy;
} w53_pool_t;

typedef struct {
    uint64_t lo, hi, src, fence;
    int kind;
} w53_gw_t;

typedef struct {
    uint32_t type, used;
    uint64_t free_at;
} w53_tb_t;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint64_t prng(uint64_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void fail_eq(const char *msg, uint64_t want, uint64_t got) {
    if (want != got) {
        fprintf(stderr, "FAIL %s: want=%llu got=%llu\n",
                msg, (unsigned long long)want, (unsigned long long)got);
        abort();
    }
}

static void sync_pool_map(vka_mem_pool_map_t *pm, const w53_pool_t *pools, int n) {
    vka_mem_pool_begin(pm, (uint32_t)n);
    uint64_t llo = UINT64_MAX, lhi = 0u, b64 = 0u, b4m = 0u;
    uint32_t ln = 0u;
    for (int i = 0; i < n; i++) {
        vka_mem_pool_put(pm, pools[i].id, (uint64_t)pools[i].mem, i);
        if (pools[i].lazy && pools[i].size) {
            uint64_t a = (uint64_t)pools[i].mem, b = a + pools[i].size;
            if (a < llo) llo = a;
            if (b > lhi) lhi = b;
            b64 |= vka_ms_bloom_bits(a, b, 16u);
            b4m |= vka_ms_bloom_bits(a, b, 22u);
            ln++;
        }
    }
    vka_mem_lazy_publish(pm, ln, llo, lhi, b64, b4m);
}

static void test_pool_pend_and_join(void) {
    w53_pool_t pools[32];
    for (int i = 0; i < 32; i++) {
        uint64_t base = UINT64_C(0x200000000) + (uint64_t)(i == 5 ? 2 : i) * UINT64_C(0x1000000);
        pools[i] = (w53_pool_t){(uintptr_t)base, UINT64_C(0x200000), (uint64_t)(100 + i), 0u, (i % 4) == 0};
    }
    vka_mem_pool_map_t *pm = calloc(1, sizeof *pm);
    assert(pm != NULL);
    sync_pool_map(pm, pools, 32);
    for (int i = 0; i < 32; i++) {
        int id_got = vka_mem_pool_find_id(pm, pools[i].id);
        fail_eq("pool_id", (uint64_t)i, (uint64_t)id_got);
        int want_m = -1;
        for (int k = 0; k < 32 && want_m < 0; k++) {
            if (pools[k].mem == pools[i].mem) want_m = k;
        }
        fail_eq("pool_mem_first", (uint64_t)want_m, (uint64_t)vka_mem_pool_find_mem(pm, (uint64_t)pools[i].mem));
    }
    for (uint64_t p = 0u; p < 64u; p++) {
        uint64_t lo = UINT64_C(0x200000000) + p * UINT64_C(0x800000);
        uint64_t hi = lo + UINT64_C(0x10000);
        int lin = 0;
        for (int i = 0; i < 32; i++) {
            if (pools[i].lazy && (uint64_t)pools[i].mem < hi && lo < (uint64_t)pools[i].mem + pools[i].size) lin = 1;
        }
        int flt = vka_mem_lazy_may_overlap(pm, lo, hi);
        if (lin) fail_eq("lazy_overlap_must_hit", 1u, (uint64_t)flt);
    }
    free(pm);
    uintptr_t pend_arr[16][2];
    for (int i = 0; i < 16; i++) {
        pend_arr[i][0] = UINT64_C(0x500000000) + (uintptr_t)i * UINT64_C(0x400000);
        pend_arr[i][1] = pend_arr[i][0] + UINT64_C(0x20000);
    }
    vka_pend_filter_t pf;
    memset(&pf, 0, sizeof pf);
    vka_pend_publish_ranges(&pf, &pend_arr[0][0], &pend_arr[0][1], 2u, 16);
    for (int i = 0; i < 16; i++) {
        fail_eq("pend_hit", 1u, (uint64_t)vka_pend_may_overlap(&pf, pend_arr[i][0], pend_arr[i][1]));
        fail_eq("pend_miss_hi", 0u, (uint64_t)vka_pend_may_overlap(&pf, pend_arr[i][1], pend_arr[i][1] + 4096u));
    }
    vka_join_set_t js;
    memset(&js, 0, sizeof js);
    vka_join_set_clear(&js);
    for (uint64_t i = 0u; i < 24u; i++) vka_join_set_put(&js, 100u + i, UINT64_C(0x1000) * i, UINT64_C(0x1000) * (i + 1u));
    fail_eq("join_hit", 1u, (uint64_t)vka_join_set_has(&js, 107u, UINT64_C(0x7000), UINT64_C(0x8000)));
    fail_eq("join_miss", 0u, (uint64_t)vka_join_set_has(&js, 107u, UINT64_C(0x7000), UINT64_C(0x9000)));
}

static void test_gw_sparse_and_one_lane(void) {
    w53_gw_t gw[24];
    for (int i = 0; i < 24; i++) {
        gw[i] = (w53_gw_t){
            UINT64_C(0x600000000) + (uint64_t)(i & 15) * UINT64_C(0x40000),
            UINT64_C(0x600000000) + (uint64_t)(i & 15) * UINT64_C(0x40000) + 4096u,
            UINT64_C(0x700000000) + (uint64_t)i * UINT64_C(0x20000),
            (i % 5) ? 0u : 10u,
            i >= 16 ? 2 : 0
        };
    }
    gw[20].src = gw[3].lo; gw[20].fence = 0u;
    vka_gw_prefix_t pfx = {0};
    for (int i = 0; i < 24; i++) {
        if (gw[i].fence && gw[i].fence <= 10u) continue;
        if (gw[i].kind == 2) {
            int lin = 0;
            for (int j = 0; j < i; j++) {
                if ((!gw[j].fence || gw[j].fence > 10u) &&
                    gw[j].lo < gw[i].src + 2048u && gw[j].hi > gw[i].src) { lin = 1; break; }
            }
            int flt = vka_gw_pfx_may_overlap(&pfx, gw[i].src, gw[i].src + 2048u);
            if (lin) fail_eq("gw_pfx_must_hit", 1u, (uint64_t)flt);
        }
        vka_gw_pfx_add(&pfx, gw[i].lo, gw[i].hi);
    }
    w53_tb_t tb[8] = {
        {2u, 0u, 1u}, {2u, 0u, 1u}, {3u, 0u, 1u}, {4u, 10u, 1u},
        {4u, 0u, 1u}, {5u, 0u, 200u}, {5u, 0u, 1u}, {7u, 0u, 1u}
    };
    vka_sparse_type_cnt_t cnt = {0};
    for (int i = 0; i < 8; i++) if (tb[i].used < 1024u) vka_sparse_cnt_add(&cnt, tb[i].type);
    uint64_t keep_mask = 0u;
    for (int i = 0; i < 8; i++) {
        if (tb[i].used || vka_sparse_has_other(&cnt, tb[i].type) != 1) continue;
        if (tb[i].free_at > 50u) continue;
        vka_sparse_cnt_dec(&cnt, tb[i].type);
        keep_mask |= UINT64_C(1) << i;
    }
    uint64_t exp_mask = (UINT64_C(1) << 0) | (UINT64_C(1) << 4) | (UINT64_C(1) << 6);
    fail_eq("sparse_trim_mask", exp_mask, keep_mask);

    uint64_t envk[4] = {0x111u, 0x222u, 0x333u, 0x444u};
    uint64_t profk[4] = {0xaaau, 0xbbbu, 0x333u, 0xcccu};
    vka_one_lane_set_t *os = calloc(1, sizeof *os);
    assert(os != NULL);
    vka_one_lane_build(os, envk, 4, profk, 4);
    fail_eq("one_env_hit", 1u, (uint64_t)vka_one_lane_lookup(os, 0x222u, 0));
    fail_eq("one_prof_off", 0u, (uint64_t)vka_one_lane_lookup(os, 0xbbbu, 0));
    fail_eq("one_prof_on", 1u, (uint64_t)vka_one_lane_lookup(os, 0xbbbu, 1));
    fail_eq("one_both", 1u, (uint64_t)vka_one_lane_lookup(os, 0x333u, 0));
    fail_eq("one_miss", 0u, (uint64_t)vka_one_lane_lookup(os, 0xdddu, 1));
    free(os);
}

typedef struct {
    const vka_mem_pool_map_t *pm;
    const vka_pend_filter_t *pf;
    const vka_one_lane_set_t *os;
    uint64_t sum;
} w53_thr_ctx_t;

static void *w53_worker(void *arg) {
    w53_thr_ctx_t *c = (w53_thr_ctx_t *)arg;
    uint64_t acc = 0u;
    for (int r = 0; r < 8000; r++) {
        uint64_t off = (uint64_t)(r & 31) * UINT64_C(0x100000);
        acc += (uint64_t)(unsigned)vka_mem_lazy_may_overlap(c->pm, UINT64_C(0x900000000) + off, UINT64_C(0x900001000) + off);
        acc += (uint64_t)(unsigned)vka_pend_may_overlap(c->pf, UINT64_C(0xa00000000) + off, UINT64_C(0xa00001000) + off);
        acc += (uint64_t)(unsigned)vka_one_lane_lookup(c->os, (uint64_t)(r & 63) + 100u, 1);
    }
    c->sum = acc;
    return NULL;
}

static void test_tsan_readers(void) {
    vka_mem_pool_map_t *pm = calloc(1, sizeof *pm);
    vka_pend_filter_t *pf = calloc(1, sizeof *pf);
    vka_one_lane_set_t *os = calloc(1, sizeof *os);
    assert(pm && pf && os);
    uintptr_t pend_r[2][2] = {{UINT64_C(0xa00000000), UINT64_C(0xa00004000)}, {UINT64_C(0xa00400000), UINT64_C(0xa00404000)}};
    uint64_t ek[4] = {101u, 105u, 111u, 117u};
    vka_mem_lazy_publish(pm, 1u, UINT64_C(0x900000000), UINT64_C(0x900020000), vka_ms_bloom_bits(UINT64_C(0x900000000), UINT64_C(0x900020000), 16u), vka_ms_bloom_bits(UINT64_C(0x900000000), UINT64_C(0x900020000), 22u));
    vka_pend_publish_ranges(pf, &pend_r[0][0], &pend_r[0][1], 2u, 2);
    vka_one_lane_build(os, ek, 4, ek, 4);
    w53_thr_ctx_t ctx[4];
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        ctx[i] = (w53_thr_ctx_t){pm, pf, os, 0u};
        assert(pthread_create(&th[i], NULL, w53_worker, &ctx[i]) == 0);
    }
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    assert(ctx[0].sum > 0u);
    free(pm); free(pf); free(os);
}

static void bench_w53(void) {
    enum { NP = 128, NL = 16, NQ = 12000, REP = 3 };
    w53_pool_t *pools = calloc(NP, sizeof *pools);
    uintptr_t (*pend)[2] = calloc(NL, sizeof *pend);
    w53_gw_t *gw = calloc(96, sizeof *gw);
    w53_tb_t *tb = calloc(96, sizeof *tb);
    uint64_t *prof = calloc(64, sizeof *prof);
    vka_mem_pool_map_t *pm = calloc(1, sizeof *pm);
    vka_pend_filter_t *pf = calloc(1, sizeof *pf);
    vka_one_lane_set_t *os = calloc(1, sizeof *os);
    assert(pools && pend && gw && tb && prof && pm && pf && os);
    for (int i = 0; i < NP; i++) pools[i] = (w53_pool_t){(uintptr_t)(UINT64_C(0x100000000) + (uint64_t)i * UINT64_C(0x2000000)), UINT64_C(0x400000), (uint64_t)(i + 1), 0u, (i & 7) == 0};
    for (int i = 0; i < NL; i++) { pend[i][0] = UINT64_C(0x800000000) + (uint64_t)i * UINT64_C(0x2000000); pend[i][1] = pend[i][0] + 4096u; }
    for (int i = 0; i < 96; i++) {
        gw[i] = (w53_gw_t){UINT64_C(0xb00000000) + (uint64_t)i * UINT64_C(0x80000), UINT64_C(0xb00000000) + (uint64_t)i * UINT64_C(0x80000) + 4096u, UINT64_C(0xc00000000) + (uint64_t)i * UINT64_C(0x80000), 0u, i >= 80 ? 2 : 0};
        tb[i] = (w53_tb_t){(uint32_t)(i & 15), 0u, (uint64_t)(i & 7)};
    }
    for (int i = 0; i < 64; i++) prof[i] = UINT64_C(0xabc000000) + (uint64_t)i * 17u;
    sync_pool_map(pm, pools, NP);
    vka_pend_publish_ranges(pf, &pend[0][0], &pend[0][1], 2u, NL);
    vka_one_lane_build(os, prof, 8, prof + 8, 56);
    uint64_t slow_sink = 0u, fast_sink = 0u, best_s = UINT64_MAX, best_f = UINT64_MAX;
    for (int r = 0; r < REP; r++) {
        uint64_t rng = 0x53535353u, s0 = 0u, t0 = now_ns();
        for (int q = 0; q < NQ; q++) {
            uint64_t k = prng(&rng); uint64_t id = (k % NP) + 1u;
            for (int i = 0; i < NP; i++) if (pools[i].id == id) { s0 += (uint64_t)i; break; }
            uint64_t pa = UINT64_C(0x900000000) + (k & 255u) * UINT64_C(0x10000);
            for (int i = 0; i < NL; i++) if (pend[i][0] < pa + 4096u && pa < pend[i][1]) { s0++; break; }
            for (int i = 80; i < 96; i++) for (int j = 0; j < i; j++) if (gw[j].lo < gw[i].src + 4096u && gw[j].hi > gw[i].src) { s0++; break; }
            for (int i = 0; i < 64; i++) if (prof[i] == (prof[q & 63] ^ (uint64_t)(q & 1))) { s0 += (uint64_t)i; break; }
        }
        uint64_t t1 = now_ns(), s1 = 0u; rng = 0x53535353u;
        for (int q = 0; q < NQ; q++) {
            uint64_t k = prng(&rng); uint64_t id = (k % NP) + 1u;
            int idx = vka_mem_pool_find_id(pm, id);
            if (idx >= 0) s1 += (uint64_t)(unsigned)idx;
            uint64_t pa = UINT64_C(0x900000000) + (k & 255u) * UINT64_C(0x10000);
            if (vka_pend_may_overlap(pf, pa, pa + 4096u)) { for (int i = 0; i < NL; i++) if (pend[i][0] < pa + 4096u && pa < pend[i][1]) { s1++; break; } }
            vka_gw_prefix_t pfx = {0};
            for (int i = 0; i < 96; i++) { if (i >= 80 && vka_gw_pfx_may_overlap(&pfx, gw[i].src, gw[i].src + 4096u)) s1++; vka_gw_pfx_add(&pfx, gw[i].lo, gw[i].hi); }
            s1 += (uint64_t)vka_one_lane_lookup(os, prof[q & 63] ^ (uint64_t)(q & 1), 1) * (uint64_t)(q & 63);
        }
        uint64_t t2 = now_ns();
        slow_sink = s0; fast_sink = s1;
        if (t1 - t0 < best_s) best_s = t1 - t0;
        if (t2 - t1 < best_f) best_f = t2 - t1;
    }
    fail_eq("bench_sink_pos", slow_sink > 0u ? 1u : 0u, 1u);
    (void)fast_sink;
    double sms = (double)best_s / 1e6, fms = (double)best_f / 1e6;
    printf("t_mem_sparse_fast_w53: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=PASS\n",
           sms, fms, sms / (fms > 0.001 ? fms : 0.001), (unsigned long long)slow_sink);
    free(pools); free(pend); free(gw); free(tb); free(prof); free(pm); free(pf); free(os);
}

int main(void) {
    test_pool_pend_and_join();
    test_gw_sparse_and_one_lane();
    test_tsan_readers();
    bench_w53();
    return 0;
}
