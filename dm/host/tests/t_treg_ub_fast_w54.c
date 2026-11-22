// Host unit test and benchmark for Wave 54 (Opt 56: vka_treg_ub_fast.h).
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vka_treg_ub_fast.h"

_Atomic int g_vka_treg_ub_fast_override = -1;
_Atomic int g_aoi_w54_fault = -1;

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

static void test_treg_sidx_and_multi(void) {
    vka_treg_sidx_t sidx = {0};
    enum { NIDS = 1200 };
    uint64_t exp_ser[NIDS];
    memset(exp_ser, 0, sizeof exp_ser);
    for (int id = 1; id < NIDS; id++) {
        uint64_t ser = UINT64_C(0x10000) + (uint64_t)(id % 61);
        vka_treg_sidx_set(&sidx, id, ser);
        exp_ser[id] = ser;
    }
    /* Reassign and clear some slots to exercise unlink and swap-pop. */
    vka_treg_sidx_set(&sidx, 33, UINT64_C(0x99999));
    exp_ser[33] = UINT64_C(0x99999);
    vka_treg_sidx_set(&sidx, 95, 0u);
    exp_ser[95] = 0u;
    for (uint64_t q = 0u; q < 61u; q++) {
        uint64_t ser = UINT64_C(0x10000) + q;
        uint64_t sum_idx = 0u, sum_lin = 0u;
        for (int id = vka_treg_sidx_first(&sidx, ser); id > 0; id = vka_treg_sidx_next(&sidx, id, ser)) sum_idx += (uint64_t)id;
        for (int id = 1; id < NIDS; id++) if (exp_ser[id] == ser) sum_lin += (uint64_t)id;
        fail_eq("treg_ser_sum", sum_lin, sum_idx);
    }
    int f33 = vka_treg_sidx_first(&sidx, UINT64_C(0x99999));
    fail_eq("treg_reassigned", 33u, (uint64_t)f33);
    fail_eq("treg_cleared", 0u, (uint64_t)(sidx.act_pos[95] >= 0 ? 1 : 0));

    uintptr_t im_a = (uintptr_t)0x123400u, im_b = (uintptr_t)0x567800u;
    vka_treg_sidx_mark_multi(&sidx, im_a);
    fail_eq("multi_a_on", 1u, (uint64_t)vka_treg_sidx_is_multi(&sidx, im_a));
    fail_eq("multi_b_off", 0u, (uint64_t)vka_treg_sidx_is_multi(&sidx, im_b));
    vka_treg_sidx_unmark_multi(&sidx, im_a);
    fail_eq("multi_a_off", 0u, (uint64_t)vka_treg_sidx_is_multi(&sidx, im_a));
    vka_treg_sidx_free(&sidx);
}

static int legacy_defer_slot(const uint64_t (*rng)[2], int n, uint64_t dst, uint64_t sz) {
    int slot = -1;
    for (int i = n - 1; i >= 0; i--) {
        uint64_t lo = rng[i][0], hi = lo + rng[i][1];
        if (lo <= dst && dst + sz <= hi) { slot = i; break; }
        if (i == n - 1 && hi == dst) { slot = i; break; }
        if (lo < dst + sz && dst < hi) break;
    }
    return slot;
}

static int fast_defer_slot(const vka_ub_dir_t *dir, const uint64_t (*rng)[2], int n, uint64_t dst, uint64_t sz) {
    if (n > 0) {
        uint64_t lo = rng[n - 1][0], hi = lo + rng[n - 1][1];
        if ((lo <= dst && dst + sz <= hi) || hi == dst) return n - 1;
        if (lo < dst + sz && dst < hi) return -1;
    }
    uint64_t cand[4];
    if (!vka_ub_dir_cand(dir, dst, dst + sz, cand)) return -1;
    for (int w = 3; w >= 0; w--) {
        uint64_t m = cand[w];
        while (m) {
            int bit = 63 - __builtin_clzll(m);
            m &= ~(UINT64_C(1) << bit);
            int i = (w << 6) + bit;
            if (i >= n - 1) continue;
            uint64_t lo = rng[i][0], hi = lo + rng[i][1];
            if (lo < dst + sz && dst < hi) return (lo <= dst && dst + sz <= hi) ? i : -1;
        }
    }
    return -1;
}

static void test_ub_dir_and_pp_cut(void) {
    enum { N = 160 };
    uint64_t rng[N][2];
    vka_ub_dir_t dir = {0};
    for (int i = 0; i < N; i++) {
        uint64_t base = UINT64_C(0x40000000) + (uint64_t)(i % 97) * UINT64_C(0x1000) + (uint64_t)((i & 3) * 64);
        uint64_t sz = (i == 95) ? 128u : 64u;
        rng[i][0] = base; rng[i][1] = sz;
        vka_ub_dir_add(&dir, i, base, base + sz);
    }
    /* Probe across slots including slot >= 64 (specifically slot 95 in bins) to validate Fault 3. */
    uint64_t q95 = rng[95][0], q95_end = q95 + rng[95][1];
    int s_leg = legacy_defer_slot(rng, N, q95 + 16u, 32u);
    int s_fst = fast_defer_slot(&dir, rng, N, q95 + 16u, 32u);
    fail_eq("ub_slot95_eq", (uint64_t)(unsigned)s_leg, (uint64_t)(unsigned)s_fst);
    fail_eq("ub_slot95_val", 95u, (uint64_t)(unsigned)s_fst);
    /* Verify ascending cand order overlap equivalence. */
    uint64_t c[4], got_mask = 0u, exp_mask = 0u;
    int hit = vka_ub_dir_cand(&dir, q95 + 16u, q95_end, c);
    fail_eq("ub_cand_hit", 1u, (uint64_t)hit);
    for (int w = 0; w < 4; w++) for (uint64_t m = c[w]; m; m &= m - 1u) {
        int i = (w << 6) + __builtin_ctzll(m);
        if (i < N && rng[i][0] < q95_end && q95 + 16u < rng[i][0] + rng[i][1]) got_mask ^= UINT64_C(1) << (i & 63);
    }
    for (int i = 0; i < N; i++) if (rng[i][0] < q95_end && q95 + 16u < rng[i][0] + rng[i][1]) exp_mask ^= UINT64_C(1) << (i & 63);
    fail_eq("ub_ovl_parity", exp_mask, got_mask);
    vka_ub_dir_clear(&dir);
    fail_eq("ub_dir_cleared", 0u, dir.active_bins);

    /* Duplicate key in later row must preserve first row index (Fault 4) and exact pair match (Fault 5). */
    const uint64_t cut[4][4] = {
        {0x1111u, 0x2222u, 0xaaaa1u, 1u},
        {0x3333u, 0x1111u, 0xaaaa2u, 1u},
        {0x5555u, 0x6666u, 0xaaaa3u, 0u},
        {0x5555u, 0x7777u, 0xaaaa4u, 0u},
    };
    vka_pp_cut_map_t cm;
    vka_pp_cut_build(&cm, cut, 4);
    int role = -1, row = vka_pp_cut_first_key(&cm, 0x1111u, &role);
    fail_eq("pp_dup_row", 0u, (uint64_t)(unsigned)row);
    fail_eq("pp_dup_role", 0u, (uint64_t)(unsigned)role);
    fail_eq("pp_pair_hit", 1u, (uint64_t)vka_pp_cut_has_pair(&cm, 0x1111u, 0x2222u));
    fail_eq("pp_pair_miss_col", 0u, (uint64_t)vka_pp_cut_has_pair(&cm, 0x1111u, 0x9999u));
}

typedef struct {
    const vka_ub_dir_t *dir;
    const vka_pp_cut_map_t *cm;
    const uint64_t (*rng)[2];
    uint64_t sum;
} w54_thr_ctx_t;

static void *w54_worker(void *arg) {
    w54_thr_ctx_t *c = (w54_thr_ctx_t *)arg;
    uint64_t acc = 0u;
    for (int r = 0; r < 6000; r++) {
        uint64_t q = c->rng[r & 63][0];
        acc += (uint64_t)(unsigned)(fast_defer_slot(c->dir, c->rng, 160, q, 16u) + 1);
        int role = 0;
        acc += (uint64_t)(unsigned)(vka_pp_cut_first_key(c->cm, (r & 1) ? 0x1111u : 0x5555u, &role) + 2);
        acc += (uint64_t)vka_pp_cut_has_pair(c->cm, 0x5555u, (r & 1) ? 0x6666u : 0x9999u);
    }
    c->sum = acc;
    return NULL;
}

static void test_tsan_readers(void) {
    enum { N = 160 };
    uint64_t (*rng)[2] = calloc(N, sizeof *rng);
    vka_ub_dir_t *dir = calloc(1, sizeof *dir);
    vka_pp_cut_map_t *cm = calloc(1, sizeof *cm);
    assert(rng && dir && cm);
    for (int i = 0; i < N; i++) {
        rng[i][0] = UINT64_C(0x41000000) + (uint64_t)i * UINT64_C(0x1000);
        rng[i][1] = 128u;
        vka_ub_dir_add(dir, i, rng[i][0], rng[i][0] + rng[i][1]);
    }
    const uint64_t cut[3][4] = {
        {0x1111u, 0x2222u, 0xau, 1u},
        {0x5555u, 0x6666u, 0xbu, 1u},
        {0x7777u, 0x8888u, 0xcu, 0u},
    };
    vka_pp_cut_build(cm, cut, 3);
    w54_thr_ctx_t ctx[4];
    pthread_t th[4];
    for (int i = 0; i < 4; i++) {
        ctx[i] = (w54_thr_ctx_t){dir, cm, rng, 0u};
        assert(pthread_create(&th[i], NULL, w54_worker, &ctx[i]) == 0);
    }
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    assert(ctx[0].sum > 0u);
    free(rng); free(dir); free(cm);
}

static uint64_t bench_w54_slow(const uint64_t *tser, const uint64_t (*rng)[2],
                               const uint64_t (*cut)[4], int ntreg, int nub, int ncut) {
    uint64_t rng0 = 0x54545454u, a0 = 0u;
    for (int q = 0; q < 12000; q++) {
        uint64_t k = prng(&rng0);
        if ((q & 63) == 0) {
            uint64_t ws = UINT64_C(0x200000) + (k & 511u);
            for (int id = 1; id < ntreg; id++) if (tser[id] == ws) a0 += (uint64_t)id;
        }
        uint64_t qa = (q & 3) == 0 ? rng[k % (uint64_t)nub][0] : (UINT64_C(0x90000000) + (k & 255u) * UINT64_C(0x1000));
        for (int i = 0; i < nub; i++) if (rng[i][0] < qa + 256u && qa < rng[i][0] + rng[i][1]) { a0++; break; }
        uint64_t pk = cut[q % ncut][0];
        for (int i = 0; i < ncut; i++) if (cut[i][0] == pk || cut[i][1] == pk) { a0 += (uint64_t)(i + 1); break; }
    }
    return a0;
}

static uint64_t bench_w54_fast(const vka_treg_sidx_t *sidx, const vka_ub_dir_t *dir,
                               const uint64_t (*rng)[2], int nub,
                               const vka_pp_cut_map_t *cm, const uint64_t (*cut)[4], int ncut) {
    uint64_t rng0 = 0x54545454u, a1 = 0u;
    for (int q = 0; q < 12000; q++) {
        uint64_t k = prng(&rng0);
        if ((q & 63) == 0) {
            uint64_t ws = UINT64_C(0x200000) + (k & 511u);
            for (int id = vka_treg_sidx_first(sidx, ws); id > 0; id = vka_treg_sidx_next(sidx, id, ws)) a1 += (uint64_t)id;
        }
        uint64_t qa = (q & 3) == 0 ? rng[k % (uint64_t)nub][0] : (UINT64_C(0x90000000) + (k & 255u) * UINT64_C(0x1000));
        uint64_t c[4];
        if (vka_ub_dir_cand(dir, qa, qa + 256u, c)) {
            int hit = 0;
            for (int w = 0; w < 4 && !hit; w++) for (uint64_t m = c[w]; m; m &= m - 1u) {
                int i = (w << 6) + __builtin_ctzll(m);
                if (i < nub && rng[i][0] < qa + 256u && qa < rng[i][0] + rng[i][1]) { hit = 1; break; }
            }
            if (hit) a1++;
        }
        int role = 0;
        int row = vka_pp_cut_first_key(cm, cut[q % ncut][0], &role);
        if (row >= 0) a1 += (uint64_t)(row + 1);
    }
    return a1;
}

static void bench_w54(void) {
    enum { NTREG = 4096, NUB = 220, NCUT = 48, REP = 3 };
    uint64_t *tser = calloc(NTREG, sizeof *tser);
    uint64_t (*rng)[2] = calloc(NUB, sizeof *rng);
    uint64_t (*cut)[4] = calloc(NCUT, sizeof *cut);
    vka_treg_sidx_t sidx = {0};
    vka_ub_dir_t *dir = calloc(1, sizeof *dir);
    vka_pp_cut_map_t *cm = calloc(1, sizeof *cm);
    assert(tser && rng && cut && dir && cm);
    for (int id = 1; id < NTREG; id++) {
        uint64_t ser = UINT64_C(0x200000) + (uint64_t)(id & 511);
        tser[id] = ser;
        vka_treg_sidx_set(&sidx, id, ser);
    }
    for (int i = 0; i < NUB; i++) {
        rng[i][0] = UINT64_C(0x80000000) + (uint64_t)i * UINT64_C(0x4000);
        rng[i][1] = 256u;
        vka_ub_dir_add(dir, i, rng[i][0], rng[i][0] + 256u);
    }
    for (int i = 0; i < NCUT; i++) {
        cut[i][0] = UINT64_C(0x9000) + (uint64_t)i * 17u;
        cut[i][1] = UINT64_C(0xa000) + (uint64_t)i * 19u;
    }
    vka_pp_cut_build(cm, cut, NCUT);

    uint64_t s0 = 0u, s1 = 0u, best_s = UINT64_MAX, best_f = UINT64_MAX;
    for (int r = 0; r < REP; r++) {
        uint64_t t0 = now_ns();
        uint64_t a0 = bench_w54_slow(tser, rng, cut, NTREG, NUB, NCUT);
        uint64_t t1 = now_ns();
        uint64_t a1 = bench_w54_fast(&sidx, dir, rng, NUB, cm, cut, NCUT);
        uint64_t t2 = now_ns();
        s0 = a0; s1 = a1;
        if (t1 - t0 < best_s) best_s = t1 - t0;
        if (t2 - t1 < best_f) best_f = t2 - t1;
    }
    fail_eq("bench_sink", s0, s1);
    double sms = (double)best_s / 1e6, fms = (double)best_f / 1e6;
    printf("t_treg_ub_fast_w54: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=PASS\n",
           sms, fms, sms / (fms > 0.001 ? fms : 0.001), (unsigned long long)s1);
    vka_treg_sidx_free(&sidx);
    free(tser); free(rng); free(cut); free(dir); free(cm);
}

int main(void) {
    test_treg_sidx_and_multi();
    test_ub_dir_and_pp_cut();
    test_tsan_readers();
    bench_w54();
    return 0;
}
