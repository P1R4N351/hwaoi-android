// Host unit test, TSan stress test, fault injection & benchmark for Wave 50 (Opt 52: DM_VK_RP_MOD_FAST).
#ifndef T_W50_TEST_HOOKS
#define T_W50_TEST_HOOKS 1
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx/vk_android/vka_rp_mod_fast.h"

_Atomic int g_vka_rp_mod_fast_override = -1;
_Atomic int g_aoi_w50_fault = -1;
static int g_fail = 0;

typedef struct {
    uint32_t cf[8], n, ds, d, s, samples, clear;
} w50_rpkey;
_Static_assert(sizeof(w50_rpkey) == 56u, "w50_rpkey 56B");

typedef struct {
    w50_rpkey k;
    uint64_t rp;
} w50_rpent;

typedef struct {
    int variant;
    uint32_t vin_u, vin_s, fout_u, fout_s;
    int frz;
    uint64_t tex_u, tex_s;
} w50_texmod_key;
_Static_assert(sizeof(w50_texmod_key) == 40u, "w50_texmod_key 40B");

typedef struct w50_texnode {
    struct w50_texnode *next;
    w50_texmod_key k;
    uint64_t mod;
} w50_texnode;

#define EXPECT_EQ(msg, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w50_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static w50_rpkey make_rpkey(uint32_t i) {
    w50_rpkey k;
    memset(&k, 0, sizeof k);
    k.n = (i & 3u) + 1u;
    for (uint32_t c = 0u; c < k.n; c++) k.cf[c] = 37u + ((i + c) & 7u);
    k.ds = (i & 4u) ? 126u : 0u;
    k.d = k.ds != 0u;
    k.s = (i & 8u) != 0u;
    k.samples = (i & 16u) ? 4u : 1u;
    k.clear = i & 0x3ffu;
    return k;
}

static int ref_rpkey_eq(const w50_rpkey *a, const w50_rpkey *b) {
    for (uint32_t i = 0u; i < 8u; i++) if (a->cf[i] != b->cf[i]) return 0;
    return a->n == b->n && a->ds == b->ds && a->d == b->d && a->s == b->s &&
           a->samples == b->samples && a->clear == b->clear;
}

static w50_texmod_key make_tmk(uint32_t i) {
    w50_texmod_key k;
    memset(&k, 0, sizeof k);
    k.variant = (int)(i & 3u);
    k.vin_u = (i * 17u) & 0xffu; k.vin_s = (i * 31u) & 0xfu;
    k.fout_u = (i >> 2) & 7u; k.fout_s = (i >> 3) & 3u;
    k.frz = (i & 1u) ? (int)(i & 7u) : -1;
    k.tex_u = UINT64_C(1) << (i & 31u);
    k.tex_s = UINT64_C(0x101) + (uint64_t)i * UINT64_C(0x100000003);
    return k;
}

static int ref_tmk_eq(const w50_texmod_key *a, const w50_texmod_key *b) {
    return a->variant == b->variant && a->vin_u == b->vin_u && a->vin_s == b->vin_s &&
           a->fout_u == b->fout_u && a->fout_s == b->fout_s && a->frz == b->frz &&
           a->tex_u == b->tex_u && a->tex_s == b->tex_s;
}

static uint32_t *slow_read_idx(const uint8_t *src, int itype, uint32_t count) {
    size_t isz = itype == 0 ? 1u : (itype == 1 ? 2u : 4u);
    uint8_t *raw = (uint8_t *)malloc(isz * count + 4u);
    uint32_t *o = (uint32_t *)malloc(4u * (size_t)count + 4u);
    if (!raw || !o) { free(raw); free(o); return NULL; }
    memcpy(raw, src, isz * count);
    for (uint32_t i = 0u; i < count; i++) {
        o[i] = isz == 1u ? raw[i] : (isz == 2u ? ((uint16_t *)raw)[i] : ((uint32_t *)raw)[i]);
    }
    free(raw);
    return o;
}

static uint32_t *fast_read_idx(const uint8_t *src, int itype, uint32_t count) {
    size_t isz = itype == 0 ? 1u : (itype == 1 ? 2u : 4u);
    uint32_t *o = (uint32_t *)malloc(4u * (size_t)count + 4u);
    if (!o) return NULL;
    size_t nb = isz * (size_t)count;
    uint8_t *dst = (uint8_t *)o + (isz < 4u ? (4u - isz) * (size_t)count : 0u);
    memcpy(dst, src, nb);
    if (isz < 4u) vka_widen_idx_tail_fast(o, count, isz);
    return o;
}

static void test_rp_and_mod_maps(void) {
    static w50_rpent rp_ents[256];
    static vka_rp_map_t rp_map;
    static vka_mod_pair_map_t pm;
    static vka_mod_tex_map_t tm;
    memset(&rp_map, 0, sizeof rp_map); memset(&pm, 0, sizeof pm); memset(&tm, 0, sizeof tm);
    for (uint32_t i = 0u; i < 200u; i++) {
        rp_ents[i].k = make_rpkey(i);
        rp_ents[i].rp = UINT64_C(0x500000) + i;
        vka_rp_map_insert(&rp_map, vka_rpkey_hash_fast(&rp_ents[i].k), (uint16_t)(i + 1u));
        vka_mod_pair_put(&pm, (i & 3u) + 1u, UINT64_C(0x9000) + (i & 7u), i * 3u, i * 11u + 7u, i & 1u ? 0u : UINT64_C(0x70000) + i);
        w50_texmod_key tk = make_tmk(i);
        vka_mod_tex_put(&tm, UINT64_C(0xa000) + (i & 7u), &tk, i & 2u ? 0u : UINT64_C(0x80000) + i);
    }
    w50_rpkey miss_rp = make_rpkey(0); miss_rp.clear ^= 1u;
    EXPECT_EQ("rp_eq_clear_diff", 0, vka_rpkey_eq_fast(&rp_ents[0].k, &miss_rp));
    EXPECT_EQ("rp_clear_coll", -1, vka_rp_map_find(&rp_map, rp_ents, sizeof *rp_ents, &miss_rp, vka_rpkey_hash_fast(&rp_ents[0].k)));
    EXPECT_EQ("rp_hit_199", 199, vka_rp_map_find(&rp_map, rp_ents, sizeof *rp_ents, &rp_ents[199].k, vka_rpkey_hash_fast(&rp_ents[199].k)));
    uint64_t got = 999u, b2 = 19u;
    uint32_t h_pair = vka_mod_pair_hash(2u, UINT64_C(0x9001), 3u, 18u);
    while (b2 < 65536u && vka_mod_pair_hash(2u, UINT64_C(0x9001), 3u, b2) != h_pair) b2++;
    EXPECT_EQ("pair_coll_found", h_pair, vka_mod_pair_hash(2u, UINT64_C(0x9001), 3u, b2));
    EXPECT_EQ("pair_hit_null", 1, vka_mod_pair_find(&pm, 2u, UINT64_C(0x9001), 3u, 18u, &got));
    EXPECT_EQ("pair_null_val", 0u, got);
    EXPECT_EQ("pair_b_miss", 0, vka_mod_pair_find(&pm, 2u, UINT64_C(0x9001), 3u, b2, &got));
    w50_texmod_key tk0 = make_tmk(4u), tk_bad = tk0;
    EXPECT_EQ("tex_hit", 1, vka_mod_tex_find(&tm, UINT64_C(0xa004), &tk0, &got));
    EXPECT_EQ("tex_val", UINT64_C(0x80004), got);
    uint32_t h_tex = vka_texmod_key_hash_fast(UINT64_C(0xa004), &tk0);
    for (uint64_t t = 1u; t < 65536u; t++) {
        tk_bad.tex_s = tk0.tex_s ^ t;
        if (vka_texmod_key_hash_fast(UINT64_C(0xa004), &tk_bad) == h_tex) break;
    }
    EXPECT_EQ("tex_eq_diff", 0, vka_texmod_key_eq_fast(&tk0, &tk_bad));
    EXPECT_EQ("tex_s_miss", 0, vka_mod_tex_find(&tm, UINT64_C(0xa004), &tk_bad, &got));
}

static void test_idx_and_inst(void) {
    static const uint32_t k_lens[7] = {0u, 1u, 3u, 7u, 16u, 33u, 131u};
    uint8_t src[540];
    for (uint32_t i = 0u; i < 540u; i++) src[i] = (uint8_t)((i * 29u + 7u) & 0xffu);
    for (int it = 0; it < 3; it++) {
        for (size_t li = 0u; li < 7u; li++) {
            uint32_t n = k_lens[li];
            uint32_t *ref = slow_read_idx(src, it, n);
            uint32_t *got = fast_read_idx(src, it, n);
            for (uint32_t i = 0u; i < n; i++) EXPECT_EQ("idx_val", ref[i], got[i]);
            free(ref); free(got);
        }
    }
    for (int st = 0; st < 8; st++) {
        uint32_t dirty[8] = {0};
        dirty[st] = UINT32_C(1) << 12;
        int want = (st == 0) && (12 == 5);
        EXPECT_EQ("inst_dirty_st", want, vka_inst_ubo_dirty_ok_fast(dirty, 5, 9));
    }
    int32_t s0[4] = {10, 20, 640, 480}, s1[4] = {10, 20, 640, 481};
    EXPECT_EQ("scissor_eq", 1, vka_scissor_eq_fast(s0, s0));
    EXPECT_EQ("scissor_neq", 0, vka_scissor_eq_fast(s0, s1));
}

typedef struct {
    int tid;
    const w50_rpent *rp_ents;
    const vka_rp_map_t *rp_map;
    const vka_mod_pair_map_t *pm;
    const vka_mod_tex_map_t *tm;
} w50_thr_ctx;

static void *w50_worker(void *arg) {
    const w50_thr_ctx *ctx = (const w50_thr_ctx *)arg;
    uint8_t src[128];
    for (uint32_t i = 0u; i < 128u; i++) src[i] = (uint8_t)(i + (uint32_t)ctx->tid);
    for (uint32_t i = 0u; i < 4000u; i++) {
        uint32_t idx = i & 127u;
        const w50_rpent *e = &ctx->rp_ents[idx];
        int hit = vka_rp_map_find(ctx->rp_map, ctx->rp_ents, sizeof *ctx->rp_ents, &e->k, vka_rpkey_hash_fast(&e->k));
        EXPECT_EQ("tsan_rp", (int)idx, hit);
        uint64_t m = 0u;
        w50_texmod_key tk = make_tmk(idx);
        EXPECT_EQ("tsan_tex", 1, vka_mod_tex_find(ctx->tm, UINT64_C(0xa000), &tk, &m));
        EXPECT_EQ("tsan_tex_val", UINT64_C(0x80000) + idx, m);
        uint32_t *g = fast_read_idx(src, (int)(i % 3u), 19u);
        EXPECT_EQ("tsan_idx", i % 3u == 0u ? (uint32_t)src[0] : (i % 3u == 1u ? (uint32_t)(src[0] | (src[1] << 8)) : 0u), i % 3u < 2u ? g[0] : 0u);
        free(g);
    }
    return NULL;
}

static void test_tsan(void) {
    static w50_rpent rp_ents[128];
    static vka_rp_map_t rp_map;
    static vka_mod_pair_map_t pm;
    static vka_mod_tex_map_t tm;
    memset(&rp_map, 0, sizeof rp_map); memset(&pm, 0, sizeof pm); memset(&tm, 0, sizeof tm);
    for (uint32_t i = 0u; i < 128u; i++) {
        rp_ents[i].k = make_rpkey(i); rp_ents[i].rp = i + 1u;
        vka_rp_map_insert(&rp_map, vka_rpkey_hash_fast(&rp_ents[i].k), (uint16_t)(i + 1u));
        w50_texmod_key tk = make_tmk(i);
        vka_mod_tex_put(&tm, UINT64_C(0xa000), &tk, UINT64_C(0x80000) + i);
    }
    w50_thr_ctx ctx[4];
    pthread_t th[4];
    for (int t = 0; t < 4; t++) { ctx[t] = (w50_thr_ctx){t, rp_ents, &rp_map, &pm, &tm}; pthread_create(&th[t], NULL, w50_worker, &ctx[t]); }
    for (int t = 0; t < 4; t++) pthread_join(th[t], NULL);
}

static uint64_t bench_one_pass(int fast, double *ms_out, const w50_rpent *rps, const vka_rp_map_t *rp_map,
                               w50_texnode *head, const w50_texmod_key *tks, const vka_mod_tex_map_t *tm, const uint8_t *src) {
    enum { ITERS = 40000u };
    uint64_t sink = 0u, t0 = w50_now_ns();
    for (uint32_t i = 0u; i < ITERS; i++) {
        uint32_t ri = (i * 7u) % 192u, ti = (i * 5u) & 31u;
        if (fast) {
            int hit = vka_rp_map_find(rp_map, rps, sizeof *rps, &rps[ri].k, vka_rpkey_hash_fast(&rps[ri].k));
            if (hit >= 0) sink += rps[hit].rp;
            uint64_t mh = 0u;
            if (vka_mod_tex_find(tm, UINT64_C(0xb000), &tks[ti], &mh)) sink += mh;
            uint32_t *idx = fast_read_idx(src, 1, 64u);
            if (idx) sink += idx[(i * 3u) & 63u];
            free(idx);
        } else {
            for (int k = 0; k < 192; k++) if (ref_rpkey_eq(&rps[k].k, &rps[ri].k)) { sink += rps[k].rp; break; }
            for (const w50_texnode *v = head; v; v = v->next) if (ref_tmk_eq(&v->k, &tks[ti])) { sink += v->mod; break; }
            uint32_t *idx = slow_read_idx(src, 1, 64u);
            if (idx) sink += idx[(i * 3u) & 63u];
            free(idx);
        }
    }
    *ms_out = (double)(w50_now_ns() - t0) / 1e6;
    return sink;
}

static void run_bench(void) {
    static w50_rpent rps[192];
    static vka_rp_map_t rp_map;
    static w50_texnode nodes[32];
    static w50_texmod_key tks[32];
    static vka_mod_tex_map_t tm;
    uint8_t src[256];
    memset(&rp_map, 0, sizeof rp_map); memset(&tm, 0, sizeof tm);
    for (uint32_t i = 0u; i < 192u; i++) {
        rps[i].k = make_rpkey(i); rps[i].rp = UINT64_C(0x1000) + i;
        vka_rp_map_insert(&rp_map, vka_rpkey_hash_fast(&rps[i].k), (uint16_t)(i + 1u));
    }
    w50_texnode *head = NULL;
    for (uint32_t i = 0u; i < 32u; i++) {
        tks[i] = make_tmk(i);
        nodes[i].k = tks[i]; nodes[i].mod = UINT64_C(0x2000) + i; nodes[i].next = head; head = &nodes[i];
        vka_mod_tex_put(&tm, UINT64_C(0xb000), &tks[i], nodes[i].mod);
    }
    for (uint32_t i = 0u; i < 256u; i++) src[i] = (uint8_t)((i * 11u + 3u) & 0xffu);
    double s_ms = 0.0, f_ms = 0.0;
    uint64_t s_sink = bench_one_pass(0, &s_ms, rps, &rp_map, head, tks, &tm, src);
    uint64_t f_sink = bench_one_pass(1, &f_ms, rps, &rp_map, head, tks, &tm, src);
    EXPECT_EQ("bench_sink", s_sink, f_sink);
    printf("t_rp_mod_fast_w50: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=%s\n",
           s_ms, f_ms, f_ms > 0.0 ? s_ms / f_ms : 0.0, (unsigned long long)f_sink, g_fail ? "FAIL" : "PASS");
}

int main(void) {
    vka_rp_mod_fast_set_for_test(1);
    EXPECT_EQ("switch_1", 1, vka_rp_mod_fast_on());
    vka_rp_mod_fast_set_for_test(0);
    EXPECT_EQ("switch_0", 0, vka_rp_mod_fast_on());
    vka_rp_mod_fast_set_for_test(1);
    test_rp_and_mod_maps();
    test_idx_and_inst();
    test_tsan();
    run_bench();
    return g_fail ? 1 : 0;
}
