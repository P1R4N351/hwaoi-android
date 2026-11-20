#define _POSIX_C_SOURCE 200809L
#define W11_TEST_HOOKS 1
#include "../nvn/nvn_hmap.h"
#include "../vk_android/vka_state_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w11_fault = 0;
static volatile uint64_t g_w11_sink;

typedef struct {
    vka_bstate b;
    const prog_t *bst[VKA_NSTAGE], *cprog;
    blend_st blend[8]; color_st color; chmask_st chmask; ms_st ms; poly_st poly; ds_st ds;
    va_st attr[16]; int nattr; vs_st stream[16]; int nstream; NVNbufferRange vbuf[16];
    p_poly_offset po; float vp[16][4]; float dr[2]; int has_vp, depth_clamp;
    float blend_color[4], alpha_ref; p_prim_restart restart; int rast_discard, tf_on;
    uint32_t bind_gen, ubo_dirty[VKA_NSTAGE];
    uint64_t s_vfmt, s_vbuf, s_ubo5, s_ssbo[VKA_NSTAGE], s_tex[VKA_NSTAGE], s_push;
} w11_state;

static uint64_t g_w11_stamp = 100u;
static uint64_t w11_next_stamp(void) { return ++g_w11_stamp; }

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static uint64_t ref_hash(const void *k, uint32_t len) {
    const uint8_t *p = k; uint64_t h = 0x9e3779b97f4a7c15ull ^ len;
    for (; len >= 8; p += 8, len -= 8) { uint64_t w; memcpy(&w, p, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; }
    if (len) { uint64_t w = 0; memcpy(&w, p, len); h = (h ^ w) * 0xff51afd7ed558ccdull; }
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

static int apply_burst(w11_state *s, int fast, const p_bind_program *pr, const p_bind_vattr *va, const p_bind_vstream *vs,
                       const p_bind_vbuf *vb, const p_bind_buf *ub, const p_bind_buf *sb,
                       const p_bind_handles *th, const p_bind_state *st, const p_viewports *vp) {
    int ch = 0;
    if (vka_state_apply_program(fast, s->bst, &s->cprog, pr)) { s->bind_gen++; ch = 1; }
    if (vka_state_apply_vattr(fast, 1, s->attr, &s->nattr, va)) { s->s_vfmt = w11_next_stamp(); s->bind_gen++; ch = 1; }
    if (vka_state_apply_vstream(fast, s->stream, &s->nstream, vs)) { s->s_vfmt = w11_next_stamp(); s->bind_gen++; ch = 1; }
    if (vka_state_apply_vbuf(fast, s->vbuf, vb)) { s->s_vbuf = w11_next_stamp(); s->bind_gen++; ch = 1; }
    if (vka_state_apply_ubo(fast, &s->b, s->ubo_dirty, ub) && ub->stage == 5) { s->s_ubo5 = w11_next_stamp(); }
    if (vka_state_apply_ssbo(fast, &s->b, sb)) { s->s_ssbo[sb->stage] = w11_next_stamp(); s->bind_gen++; ch = 1; }
    if (vka_state_apply_tex(fast, &s->b, th->stage, th->first, th->n, th->h)) { s->s_tex[th->stage] = w11_next_stamp(); s->bind_gen++; ch = 1; }
    if (vka_state_apply_bind_st(fast, st, s->blend, &s->color, &s->chmask, &s->ms, &s->poly, &s->ds)) { s->bind_gen++; ch = 1; }
    if (vka_state_apply_viewports(fast, s->vp, &s->has_vp, vp)) { s->bind_gen++; ch = 1; }
    return ch;
}

static int test_state_dedup(void) {
    w11_state *s = calloc(1, sizeof *s); assert(s != NULL);
    prog_t pr_obj = {.serial = 1u}; p_bind_program pr = {.prog = &pr_obj, .stages = 0x3};
    p_bind_vattr va = {.first = 0, .n = 2}; va_st a0 = {0x1au, 0u, 0u}, a1 = {0x1au, 12u, 0u};
    memcpy(&va.st[0], &a0, 4); memcpy(&va.st[1], &a1, 4);
    p_bind_vstream vs = {.first = 0, .n = 1, .st = {{24u, 0u}}};
    p_bind_vbuf vb = {.first = 0, .n = 1, .r = {{0x40000000ull, 4096u}}};
    p_bind_buf ub = {.stage = 5, .index = 2, .addr = 0x50000000ull, .size = 256u};
    p_bind_buf sb = {.stage = 1, .index = 0, .addr = 0x60000000ull, .size = 1024u};
    p_bind_handles th = {.stage = 1, .first = 0, .n = 2, .h = {0x101ull, 0x102ull}};
    p_bind_state st = {.kind = 0}; blend_st bs = {0u, 2u, 3u, 2u, 3u, 0u, 0u, 0u, 0u, 0u}; memcpy(st.bytes, &bs, sizeof bs);
    p_viewports vp = {.first = 0, .n = 1, .v = {{0.f, 0.f, 1280.f, 720.f, 0.f, 1.f}}};

    apply_burst(s, 1, &pr, &va, &vs, &vb, &ub, &sb, &th, &st, &vp);
    uint32_t g0 = s->bind_gen; uint64_t sv = s->s_vfmt, sb0 = s->s_vbuf, su = s->s_ubo5, ss = s->s_ssbo[1], stex = s->s_tex[1];
    memset(s->ubo_dirty, 0, sizeof s->ubo_dirty);
    int ch = apply_burst(s, 1, &pr, &va, &vs, &vb, &ub, &sb, &th, &st, &vp);
    if (ch || s->bind_gen != g0 || s->s_vfmt != sv || s->s_vbuf != sb0 || s->s_ubo5 != su ||
        s->s_ssbo[1] != ss || s->s_tex[1] != stex || s->ubo_dirty[5] != 0u) { free(s); return 1; }

    /* Mutate Fault 1 (vattr), Fault 2 (tex + ssbo), Fault 3 (ubo_dirty + bind_state). */
    a1.offset = 16u; memcpy(&va.st[1], &a1, 4);
    apply_burst(s, 1, &pr, &va, &vs, &vb, &ub, &sb, &th, &st, &vp);
    if (s->s_vfmt == sv) { free(s); return 2; }
    th.h[1] = 0x103ull; sb.addr = 0x60001000ull;
    apply_burst(s, 1, &pr, &va, &vs, &vb, &ub, &sb, &th, &st, &vp);
    if (s->s_tex[1] == stex || s->s_ssbo[1] == ss) { free(s); return 3; }
    uint32_t g1 = s->bind_gen; ub.addr = 0x50000100ull; bs.src_rgb = 5u; memcpy(st.bytes, &bs, sizeof bs);
    apply_burst(s, 1, &pr, &va, &vs, &vb, &ub, &sb, &th, &st, &vp);
    if (s->ubo_dirty[5] == 0u || s->bind_gen == g1) { free(s); return 4; }
    free(s);
    return 0;
}

static int test_hmap16(void) {
    nvn_hmap m; nvn_hm_init(&m, 16u);
    uint64_t ref_k[512][2]; void *ref_v[512];
    for (uint32_t i = 0; i < 512u; i++) {
        ref_k[i][0] = 0x1000000000000000ull ^ (uint64_t)i * 0x9e3779b97f4a7c15ull;
        ref_k[i][1] = 0x2000000000000000ull ^ (uint64_t)(i & 15u);
        if (ref_hash(ref_k[i], 16u) != nvn_hm_hash(ref_k[i], 16u)) { free(m.keys); free(m.vals); return 1; }
        ref_v[i] = (void *)(uintptr_t)(i + 1u);
        if (nvn_hm_put(&m, ref_k[i], ref_v[i]) != NULL) { free(m.keys); free(m.vals); return 2; }
    }
    for (uint32_t i = 0; i < 512u; i++) {
        if (nvn_hm_get(&m, ref_k[i]) != ref_v[i]) { free(m.keys); free(m.vals); return 3; }
        uint64_t miss[2] = {ref_k[i][0], ref_k[i][1] ^ 1ull};
        if (nvn_hm_get(&m, miss) != NULL) { free(m.keys); free(m.vals); return 4; }
    }
    for (uint32_t i = 0; i < 512u; i += 2u) {
        if (nvn_hm_del(&m, ref_k[i]) != ref_v[i]) { free(m.keys); free(m.vals); return 5; }
    }
    for (uint32_t i = 0; i < 512u; i++) {
        void *got = nvn_hm_get(&m, ref_k[i]);
        if (got != ((i & 1u) ? ref_v[i] : NULL)) { free(m.keys); free(m.vals); return 6; }
    }
    free(m.keys); free(m.vals);
    return 0;
}

static double bench_state_fast(int fast, uint64_t *skipped_o) {
    w11_state *s = calloc(1, sizeof *s); assert(s != NULL);
    prog_t pr_obj = {.serial = 1u}; p_bind_program pr = {.prog = &pr_obj, .stages = 0x3};
    p_bind_vattr va = {.first = 0, .n = 4};
    p_bind_vstream vs = {.first = 0, .n = 2, .st = {{32u, 0u}, {16u, 1u}}};
    p_bind_vbuf vb = {.first = 0, .n = 2, .r = {{0x40000000ull, 4096u}, {0x41000000ull, 2048u}}};
    p_bind_buf ub = {.stage = 5, .index = 2, .addr = 0x50000000ull, .size = 256u};
    p_bind_buf sb = {.stage = 1, .index = 0, .addr = 0x60000000ull, .size = 1024u};
    p_bind_handles th = {.stage = 1, .first = 0, .n = 4, .h = {1u, 2u, 3u, 4u}};
    p_bind_state st = {.kind = 0}; blend_st bs = {0u, 2u, 3u, 2u, 3u, 0u, 0u, 0u, 0u, 0u}; memcpy(st.bytes, &bs, sizeof bs);
    p_viewports vp = {.first = 0, .n = 1, .v = {{0.f, 0.f, 1280.f, 720.f, 0.f, 1.f}}};
    uint64_t skipped = 0; double t0 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        if (!apply_burst(s, fast, &pr, &va, &vs, &vb, &ub, &sb, &th, &st, &vp)) skipped++;
        g_w11_sink ^= s->bind_gen ^ s->s_tex[1];
    }
    double dt = now_ms() - t0; *skipped_o = skipped; free(s); return dt;
}

static void *hm_get_generic(const nvn_hmap *m, const void *k) {
    if (!m->n) return NULL;
    uint32_t mask = m->cap - 1u;
    for (uint32_t i = (uint32_t)ref_hash(k, m->klen) & mask, s = 0; s < m->cap; s++, i = (i + 1u) & mask) {
        if (!m->vals[i]) return NULL;
        if (!memcmp(m->keys + (size_t)i * m->klen, k, m->klen)) return m->vals[i];
    }
    return NULL;
}

static double bench_hmap16(int fast) {
    nvn_hmap m; nvn_hm_init(&m, 16u); uint64_t ks[64][2];
    for (uint32_t i = 0; i < 64u; i++) {
        ks[i][0] = 0xabcdef0100000000ull + i * 17u; ks[i][1] = 0x1234567800000000ull + (i ^ 5u);
        nvn_hm_put(&m, ks[i], (void *)(uintptr_t)(i + 1u));
    }
    uint64_t sum = 0; double t0 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        void *v = fast ? nvn_hm_get(&m, ks[i & 63u]) : hm_get_generic(&m, ks[i & 63u]);
        sum += (uintptr_t)v;
    }
    double dt = now_ms() - t0; g_w11_sink ^= sum; free(m.keys); free(m.vals); return dt;
}

int main(void) {
    const char *e = getenv("T_W11_FAULT"); g_w11_fault = e ? atoi(e) : 0;
    (void)vka_state_fast_on();
    int r1 = test_state_dedup(), r2 = test_hmap16();
    if (r1 || r2) { fprintf(stderr, "FAIL r1=%d r2=%d fault=%d\n", r1, r2, g_w11_fault); return 1; }
    uint64_t sk0 = 0, sk1 = 0;
    double st0 = bench_state_fast(0, &sk0), st1 = bench_state_fast(1, &sk1);
    double hm0 = bench_hmap16(0), hm1 = bench_hmap16(1);
    printf("aoi_w11_state_fast: ops=400000 base=%.2fms (skips=%llu) fast=%.2fms (skips=%llu) speedup=%.2fx\n",
           st0, (unsigned long long)sk0, st1, (unsigned long long)sk1, st1 > 0 ? st0 / st1 : 0.0);
    printf("aoi_w11_hmap16: ops=400000 generic_ms=%.2f fast_ms=%.2f speedup=%.2fx\n",
           hm0, hm1, hm1 > 0 ? hm0 / hm1 : 0.0);
    if (g_w11_fault == 0 && (sk0 != 0u || sk1 != 399999u)) return 1;
    printf("t_state_fast_w11: fault=%d status=PASS sink=%llu\n", g_w11_fault, (unsigned long long)g_w11_sink);
    return 0;
}
