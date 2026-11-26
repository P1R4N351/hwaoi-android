#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t site;
    uint32_t old_insn;
    uint8_t post_old;
    uint8_t r0, r1, r2;
    uint8_t hook_kind;
} KtglSiteSpec;

typedef struct {
    uint64_t pool_lo, pool_hi, cur;
    uint64_t flush_off, tramp_off, bcall_off;
    uintptr_t bridge_va, post_flush_va;
} KtglEmitCtx;

static inline uint32_t ktgl_rd32(const uint8_t *base, uint64_t off) {
    uint32_t v = 0;
    memcpy(&v, base + off, sizeof v);
    return v;
}

static inline void ktgl_wr32(uint8_t *base, uint64_t off, uint32_t w) {
    memcpy(base + off, &w, sizeof w);
}

static inline int ktgl_emit_w(uint8_t *base, KtglEmitCtx *e, uint32_t w) {
    if (!e || e->cur + 4u > e->pool_hi) return 0;
    ktgl_wr32(base, e->cur, w);
    e->cur += 4u;
    return 1;
}

static inline int ktgl_emit_seq(
    uint8_t *b, KtglEmitCtx *e, const uint32_t *w, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!ktgl_emit_w(b, e, w[i])) return 0;
    }
    return 1;
}

static inline uint32_t ktgl_b_word(uint64_t from_off, uint64_t to_off, int is_bl) {
    int64_t diff = (int64_t)to_off - (int64_t)from_off;
    if ((diff & 3) || diff < -(1LL << 27) || diff >= (1LL << 27)) return 0;
    uint32_t op = is_bl ? 0x94000000u : 0x14000000u;
    return op | (((uint32_t)(diff >> 2)) & 0x03ffffffu);
}

static inline int ktgl_emit_mov_x(
    uint8_t *b, KtglEmitCtx *e, uint8_t rd, uint8_t rm) {
    uint32_t src = rm <= 30u ? (uint32_t)rm : 31u;
    return ktgl_emit_w(b, e, 0xaa0003e0u | (src << 16) | (rd & 31u));
}

static inline int ktgl_emit_mov64(
    uint8_t *b, KtglEmitCtx *e, uint8_t rd, uint64_t val) {
    uint32_t r = rd & 31u;
    const uint32_t words[4] = {
        0xd2800000u | ((uint32_t)(val & 0xffffu) << 5) | r,
        0xf2a00000u | ((uint32_t)((val >> 16) & 0xffffu) << 5) | r,
        0xf2c00000u | ((uint32_t)((val >> 32) & 0xffffu) << 5) | r,
        0xf2e00000u | ((uint32_t)((val >> 48) & 0xffffu) << 5) | r,
    };
    return ktgl_emit_seq(b, e, words, 4u);
}

static inline int ktgl_check_zero_slack(
    const uint8_t *base, uint64_t lo, uint64_t hi) {
    if (!base || hi <= lo) return 0;
    for (uint64_t p = lo; p < hi; p++) {
        if (base[p] != 0u) return 0;
    }
    return 1;
}

static inline int ktgl_check_sites(
    const uint8_t *base, uint64_t text_end, const KtglSiteSpec *s, size_t n) {
    if (!base || !s || !n) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i].site + 4u > text_end) return 0;
        if (ktgl_rd32(base, s[i].site) != s[i].old_insn) return 0;
    }
    return 1;
}

static inline int ktgl_emit_frame_save(uint8_t *b, KtglEmitCtx *e) {
    static const uint32_t pre[] = {
        0xd10a43ffu,
        0xa90007e0u, 0xa9010fe2u, 0xa90217e4u, 0xa9031fe6u,
        0xa90433e8u, 0xa9053bedu, 0xa90647efu, 0xa9077bfdu,
        0xd53b4209u, 0xa9087fe9u,
    };
    if (!ktgl_emit_seq(b, e, pre, sizeof pre / sizeof pre[0])) return 0;
    for (uint32_t q = 0; q < 32u; q += 2u) {
        uint32_t off16 = (144u + q * 16u) / 16u;
        uint32_t w = 0xad0003e0u | (off16 << 15) | ((q + 1u) << 10) | q;
        if (!ktgl_emit_w(b, e, w)) return 0;
    }
    return 1;
}

static inline int ktgl_emit_frame_restore(uint8_t *b, KtglEmitCtx *e) {
    static const uint32_t post[] = {
        0xf94043e9u, 0xd51b4209u,
        0xa94007e0u, 0xa9410fe2u, 0xa94217e4u, 0xa9431fe6u,
        0xa94433e8u, 0xa9453bedu, 0xa94647efu, 0xa9477bfdu,
        0x910a43ffu, 0xd65f03c0u,
    };
    for (uint32_t q = 0; q < 32u; q += 2u) {
        uint32_t off16 = (144u + q * 16u) / 16u;
        uint32_t w = 0xad4003e0u | (off16 << 15) | ((q + 1u) << 10) | q;
        if (!ktgl_emit_w(b, e, w)) return 0;
    }
    return ktgl_emit_seq(b, e, post, sizeof post / sizeof post[0]);
}

static inline int ktgl_emit_flush_trampoline(uint8_t *b, KtglEmitCtx *e) {
    e->bcall_off = e->cur;
    if (!ktgl_emit_mov64(b, e, 17u, (uint64_t)e->bridge_va)) return 0;
    if (!ktgl_emit_w(b, e, 0xd61f0220u)) return 0;
    e->tramp_off = e->cur;
    if (!ktgl_emit_frame_save(b, e)) return 0;
    if (!ktgl_emit_mov_x(b, e, 0u, 17u)) return 0;
    uint32_t bl = ktgl_b_word(e->cur, e->flush_off, 1);
    if (!bl || !ktgl_emit_w(b, e, bl)) return 0;
    if (!ktgl_emit_w(b, e, 0xf94037e9u)) return 0;
    if (!ktgl_emit_mov_x(b, e, 10u, 31u)) return 0;
    if (!ktgl_emit_mov_x(b, e, 11u, 31u)) return 0;
    if (!ktgl_emit_mov64(b, e, 16u, (uint64_t)e->post_flush_va)) return 0;
    uint32_t bbl = ktgl_b_word(e->cur, e->bcall_off, 1);
    if (!bbl || !ktgl_emit_w(b, e, bbl)) return 0;
    return ktgl_emit_frame_restore(b, e);
}

static inline int ktgl_emit_hook_stub(
    uint8_t *b, KtglEmitCtx *e, const KtglSiteSpec *s, uintptr_t hook_va, int with_flush) {
    static const uint32_t push48[] = {
        0xd100c3ffu, 0xa9002be9u, 0xa90143ebu, 0xa9027bf1u,
    };
    static const uint32_t pop48[] = {
        0xa9427bf1u, 0xa94143ebu, 0xa9402be9u, 0x9100c3ffu,
    };
    uint64_t stub_off = e->cur;
    if (s->post_old && !ktgl_emit_w(b, e, s->old_insn)) return 0;
    if (!ktgl_emit_seq(b, e, push48, 4u)) return 0;
    if (!ktgl_emit_mov_x(b, e, 9u, s->r0)) return 0;
    if (!ktgl_emit_mov_x(b, e, 10u, s->r1)) return 0;
    if (!ktgl_emit_mov_x(b, e, 11u, s->r2)) return 0;
    if (!ktgl_emit_mov64(b, e, 16u, (uint64_t)hook_va)) return 0;
    uint32_t bbl = ktgl_b_word(e->cur, e->bcall_off, 1);
    if (!bbl || !ktgl_emit_w(b, e, bbl)) return 0;
    if (with_flush) {
        if (!ktgl_emit_w(b, e, 0xb4000051u)) return 0;
        uint32_t bl = ktgl_b_word(e->cur, e->tramp_off, 1);
        if (!bl || !ktgl_emit_w(b, e, bl)) return 0;
    }
    if (!ktgl_emit_seq(b, e, pop48, 4u)) return 0;
    if (!s->post_old && !ktgl_emit_w(b, e, s->old_insn)) return 0;
    uint32_t br = ktgl_b_word(e->cur, s->site + 4u, 0);
    uint32_t pb = ktgl_b_word(s->site, stub_off, 0);
    if (!br || !pb || !ktgl_emit_w(b, e, br)) return 0;
    ktgl_wr32(b, s->site, pb);
    return 1;
}

static inline int ktgl_emit_site_stub(
    uint8_t *b, KtglEmitCtx *e, const KtglSiteSpec *s, uintptr_t hook_va) {
    return ktgl_emit_hook_stub(b, e, s, hook_va, 1);
}

static inline int ktgl_emit_call_stub(
    uint8_t *b, KtglEmitCtx *e, const KtglSiteSpec *s, uintptr_t hook_va) {
    return ktgl_emit_hook_stub(b, e, s, hook_va, 0);
}
