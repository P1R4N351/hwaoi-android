#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W10_TEST_HOOKS 1
int g_w10_fault = 0;
#include "vka_walk_fast.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int dummy_rd(void *ctx, uint64_t addr, void *out, size_t n) {
    (void)ctx; (void)addr;
    memset(out, 0, n);
    return 1;
}

static void init_shader_fixture(dmsh_vk_entry *e, dmsh_vk_bind b[16]) {
    memset(e, 0, sizeof *e);
    memset(b, 0, sizeof(dmsh_vk_bind) * 16u);
    e->key = 0xfeedfacecafebeefull;
    e->bind_count = 16;
    for (uint16_t i = 0; i < 4; i++) {
        b[i] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_CBUF, .desc = 6, .binding = i, .count = 1, .reg = (int16_t)i};
    }
    b[4] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_DRVCB, .desc = 6, .binding = 4, .count = 1, .reg = 0};
    b[5] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_SUPPORT, .desc = 6, .binding = 5, .count = 1, .reg = -1};
    b[6] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_SBUF, .desc = 7, .binding = 6, .flags = DMSH_VK_BF_RW, .count = 1, .reg = 0};
    for (uint16_t i = 7; i < 14; i++) {
        b[i] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_TEX, .desc = 1, .binding = i, .count = 1, .reg = (int16_t)(i - 7),
                              .skind = DMSH_VK_KIND_SAMP_TEX, .dim = DMSH_VK_DIM_UINT, .sreg = (int16_t)(i - 7), .soff = 0xFFFF};
    }
    b[14] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_IMG, .desc = 3, .binding = 14, .count = 1, .reg = 0, .dim = 0};
    b[15] = (dmsh_vk_bind){.kind = DMSH_VK_KIND_TBUF, .desc = 4, .binding = 15, .count = 1, .reg = 8};
}

static void init_bstate(vka_bstate *bs, uint32_t mat) {
    memset(bs, 0, sizeof *bs);
    for (uint32_t r = 0; r < 4; r++) {
        bs->ubo[1][r] = (vka_range){0x10000000ull + (uint64_t)mat * 0x1000ull + r * 256ull, 512u + mat * 16u};
    }
    bs->ssbo[1][0] = (vka_range){0x20000000ull, 4096u};
    for (uint32_t t = 0; t < 9; t++) {
        bs->tex[1][t] = (uint64_t)(100u + mat * 16u + t) | ((uint64_t)(10u + t) << 20);
    }
}

static int res_eq(const vka_res *a, const vka_res *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i].what != b[i].what || a[i].desc != b[i].desc || a[i].flags != b[i].flags ||
            a[i].dim != b[i].dim || a[i].binding != b[i].binding || a[i].elem != b[i].elem ||
            a[i].bank != b[i].bank || a[i].tid != b[i].tid || a[i].sid != b[i].sid ||
            a[i].addr != b[i].addr || a[i].size != b[i].size) return 0;
    }
    return 1;
}

static int test_multi_mat_and_invalidation(vka_walk_sh_memo4 *sm, vka_walk_meta4 *m4, vka_walk_slot *slots) {
    dmsh_vk_entry e; dmsh_vk_bind binds[16]; init_shader_fixture(&e, binds);
    uint16_t um = 0;
    if (!vka_walk_shader_traits(sm, &e, binds, &um) || um != 0x000Fu) return 1;
    vka_bstate bs; vka_res ref[512], got[512];
    int ref_n = 0, ref_p = 0, got_n = 0, got_p = 0; uint8_t got_fl = 0; char why[64];
    uint64_t s_ssbo = 10u;
    for (uint32_t pass = 0; pass < 3u; pass++) {
        for (uint32_t mat = 0; mat < 16u; mat++) {
            uint64_t s_tex = 20u + mat;
            init_bstate(&bs, mat);
            ref_p = 0;
            ref_n = vka_walk(&bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, ref, 512, &ref_p, why, sizeof why);
            assert(ref_n == 16);
            int hit = vka_walk_memo4_find(m4, slots, (uintptr_t)&e, (uintptr_t)binds, e.key, 1u,
                                          s_ssbo, s_tex, bs.ubo[1], um, 16u, 16u, 1u, got, &got_n, &got_p, &got_fl);
            if (!hit) {
                got_p = 0;
                got_n = vka_walk(&bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, got, 512, &got_p, why, sizeof why);
                got_fl = vka_walk_summarize(got, got_n);
                vka_walk_memo4_put(m4, slots, (uintptr_t)&e, (uintptr_t)binds, e.key, 1u,
                                   s_ssbo, s_tex, bs.ubo[1], um, 16u, 16u, 1u, got, got_n, got_p, got_fl);
            }
            if (pass > 0 && !hit) return 2;
            if (got_n != ref_n || got_p != ref_p || !res_eq(got, ref, ref_n)) return 3;
        }
    }
    /* Same s_ssbo & s_tex, changed UBO -> must miss & match new UBO (catches T_W10_FAULT=1). */
    init_bstate(&bs, 0u);
    bs.ubo[1][0].addr += 0x400u;
    ref_p = 0; ref_n = vka_walk(&bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, ref, 512, &ref_p, why, sizeof why);
    if (!vka_walk_memo4_find(m4, slots, (uintptr_t)&e, (uintptr_t)binds, e.key, 1u,
            s_ssbo, 20u, bs.ubo[1], um, 16u, 16u, 1u, got, &got_n, &got_p, &got_fl)) {
        got_p = 0; got_n = vka_walk(&bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, got, 512, &got_p, why, sizeof why);
    }
    if (got_n != ref_n || !res_eq(got, ref, ref_n)) return 4;
    /* Same UBO & s_ssbo, changed tex & s_tex -> must miss & match new tex (catches T_W10_FAULT=2). */
    init_bstate(&bs, 0u);
    bs.tex[1][0] += 77u;
    ref_p = 0; ref_n = vka_walk(&bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, ref, 512, &ref_p, why, sizeof why);
    if (!vka_walk_memo4_find(m4, slots, (uintptr_t)&e, (uintptr_t)binds, e.key, 1u,
            s_ssbo, 21u, bs.ubo[1], um, 16u, 16u, 1u, got, &got_n, &got_p, &got_fl)) {
        got_p = 0; got_n = vka_walk(&bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, got, 512, &got_p, why, sizeof why);
    }
    if (got_n != ref_n || !res_eq(got, ref, ref_n)) return 5;
    return 0;
}

static int test_summary_and_uncacheable(vka_walk_sh_memo4 *sm) {
    vka_res r[3] = {
        {.what = VKA_R_TEX, .desc = 1, .dim = 0, .tid = 42},
        {.what = VKA_R_BUF, .desc = 7, .flags = DMSH_VK_BF_RW, .addr = 0x1000},
        {.what = VKA_R_GMEM_MAP, .desc = 7}
    };
    uint8_t fl = vka_walk_summarize(r, 3);
    if ((fl & (VKA_WF_TEX_IMG | VKA_WF_FLOAT_TEX | VKA_WF_WRITES_MEM | VKA_WF_BUF | VKA_WF_GMEM_MAP)) !=
        (VKA_WF_TEX_IMG | VKA_WF_FLOAT_TEX | VKA_WF_WRITES_MEM | VKA_WF_BUF | VKA_WF_GMEM_MAP)) return 10;
    dmsh_vk_entry e; dmsh_vk_bind b[2]; memset(&e, 0, sizeof e); memset(b, 0, sizeof b);
    e.key = 0x99u; e.bind_count = 2;
    b[0].kind = DMSH_VK_KIND_CBUF; b[0].reg = 0; b[0].count = 1;
    b[1].kind = DMSH_VK_KIND_TEX_UBO; b[1].reg = 1; b[1].count = 1;
    uint16_t um = 0xFFFFu;
    if (vka_walk_shader_traits(sm, &e, b, &um) != 0) return 11;
    return 0;
}

static uint64_t bench_walk(int fast, vka_walk_sh_memo4 *sm, vka_walk_meta4 *m4, vka_walk_slot *slots,
                           uint64_t *misses_o, uint64_t *sink_o) {
    dmsh_vk_entry e; dmsh_vk_bind binds[16]; init_shader_fixture(&e, binds);
    vka_bstate mat_bs[16];
    for (uint32_t m = 0; m < 16u; m++) init_bstate(&mat_bs[m], m);
    vka_res res[512]; char why[64];
    uint64_t misses = 0, sink = 0, t0 = now_ns();
    for (uint32_t it = 0; it < 400000u; it++) {
        uint32_t mat = it & 15u;
        const vka_bstate *bs = &mat_bs[mat];
        uint16_t um = 0; int n = 0, pool = 0; uint8_t fl = VKA_WF_FALLBACK;
        int hit = 0;
        if (fast && vka_walk_shader_traits(sm, &e, binds, &um)) {
            hit = vka_walk_memo4_find(m4, slots, (uintptr_t)&e, (uintptr_t)binds, e.key, 1u,
                                      100u, 200u + mat, bs->ubo[1], um, 16u, 16u, 1u, res, &n, &pool, &fl);
        }
        if (!hit) {
            misses++;
            n = vka_walk(bs, 1, &e, binds, 16u, 16u, dummy_rd, NULL, res, 512, &pool, why, sizeof why);
            fl = fast ? vka_walk_summarize(res, n) : VKA_WF_FALLBACK;
            if (fast && n >= 0 && n <= VKA_WALK_MAX_RES)
                vka_walk_memo4_put(m4, slots, (uintptr_t)&e, (uintptr_t)binds, e.key, 1u,
                                   100u, 200u + mat, bs->ubo[1], um, 16u, 16u, 1u, res, n, pool, fl);
        }
        if (fl & VKA_WF_FLOAT_TEX) for (int i = 0; i < n; i++) sink ^= (uint64_t)res[i].tid;
        sink ^= (uint64_t)n + (n > 0 ? res[0].addr : 0u) + fl;
    }
    uint64_t dt = now_ns() - t0;
    *misses_o = misses; *sink_o = sink;
    return dt;
}

int main(void) {
    const char *fe = getenv("T_W10_FAULT");
    g_w10_fault = fe && *fe ? atoi(fe) : 0;
    vka_walk_sh_memo4 *sm = calloc(1, sizeof *sm);
    vka_walk_meta4 *m4 = calloc(1, sizeof *m4);
    vka_walk_slot *slots = calloc(VKA_WALK_SLOTS, sizeof *slots);
    assert(sm && m4 && slots);
    int r1 = test_multi_mat_and_invalidation(sm, m4, slots);
    int r2 = test_summary_and_uncacheable(sm);
    if (r1 || r2) {
        fprintf(stderr, "t_walk_fast_w10: FAIL r1=%d r2=%d fault=%d\n", r1, r2, g_w10_fault);
        free(sm); free(m4); free(slots); return 1;
    }
    memset(sm, 0, sizeof *sm); memset(m4, 0, sizeof *m4); memset(slots, 0, VKA_WALK_SLOTS * sizeof *slots);
    uint64_t m0 = 0, m1 = 0, s0 = 0, s1 = 0;
    uint64_t ns0 = bench_walk(0, sm, m4, slots, &m0, &s0);
    uint64_t ns1 = bench_walk(1, sm, m4, slots, &m1, &s1);
    (void)vka_walk_fast_on();
    printf("aoi_w10_walk_fast: ops=400000 baseline_ms=%.2f fast_ms=%.2f speedup=%.2fx misses=%llu->%llu\n",
           (double)ns0 / 1e6, (double)ns1 / 1e6, ns1 ? (double)ns0 / (double)ns1 : 0.0,
           (unsigned long long)m0, (unsigned long long)m1);
    printf("t_walk_fast_w10: fault=%d status=%s sink=%llu\n", g_w10_fault,
           s0 == s1 && m1 == 16u ? "PASS" : "MISMATCH", (unsigned long long)(s0 ^ s1));
    free(sm); free(m4); free(slots);
    return (s0 != s1 || m1 != 16u) ? 1 : 0;
}
