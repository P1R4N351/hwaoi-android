#ifndef VKA_STATE_FAST_H
#define VKA_STATE_FAST_H
/* Wave 11 (Opt 13): Byte-exact NVN state/binding deduplication in vka_state_op (DM_VK_STATE_FAST=1). */
#include "../nvn/nvn_obj.h"
#include "vka_bind.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W11_TEST_HOOKS
extern int g_w11_fault;
#define VKA_STATE_FAULT(n) (g_w11_fault == (n))
#else
#define VKA_STATE_FAULT(n) 0
#endif

static inline int vka_state_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_STATE_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline int vka_blend_eq(const blend_st *a, const blend_st *b) {
    return a->target == b->target && a->src_rgb == b->src_rgb && a->dst_rgb == b->dst_rgb &&
           a->src_a == b->src_a && a->dst_a == b->dst_a && a->eq_rgb == b->eq_rgb && a->eq_a == b->eq_a &&
           a->adv == b->adv && a->adv_npm == b->adv_npm && a->adv_ovl == b->adv_ovl;
}

static inline int vka_color_eq(const color_st *a, const color_st *b) {
    return a->blend_en == b->blend_en && a->logic_op == b->logic_op && a->alpha_func == b->alpha_func;
}

static inline int vka_poly_eq(const poly_st *a, const poly_st *b) {
    return a->cull == b->cull && a->front == b->front && a->mode == b->mode && a->offset_en == b->offset_en;
}

static inline int vka_ds_eq(const ds_st *a, const ds_st *b) {
    return a->dt == b->dt && a->dw == b->dw && a->df == b->df && a->st == b->st &&
           a->sf_f == b->sf_f && a->sf_b == b->sf_b && a->fail_f == b->fail_f &&
           a->zfail_f == b->zfail_f && a->zpass_f == b->zpass_f &&
           a->fail_b == b->fail_b && a->zfail_b == b->zfail_b && a->zpass_b == b->zpass_b;
}

static inline int vka_state_apply_bind_st(int fast, const p_bind_state *b, blend_st blend[8],
                                          color_st *color, chmask_st *chmask, ms_st *ms, poly_st *poly, ds_st *ds) {
    assert(b != NULL && blend != NULL && color != NULL && chmask != NULL && ms != NULL && poly != NULL && ds != NULL);
    int ch = 1;
    switch (b->kind) {
    case 0: { blend_st bs; memcpy(&bs, b->bytes, sizeof bs);
        if (fast && vka_blend_eq(&blend[bs.target], &bs)) ch = 0; else blend[bs.target] = bs; break; }
    case 1: { color_st cs; memcpy(&cs, b->bytes, sizeof cs);
        if (fast && vka_color_eq(color, &cs)) ch = 0; else *color = cs; break; }
    case 2: { chmask_st cm; memcpy(&cm, b->bytes, sizeof cm);
        if (fast && chmask->mask == cm.mask) ch = 0; else *chmask = cm; break; }
    case 3: { ms_st m; memcpy(&m, b->bytes, sizeof m);
        if (fast && !memcmp(ms, &m, sizeof m)) ch = 0; else *ms = m; break; }
    case 4: { poly_st p; memcpy(&p, b->bytes, sizeof p);
        if (fast && vka_poly_eq(poly, &p)) ch = 0; else *poly = p; break; }
    case 5: { ds_st d; memcpy(&d, b->bytes, sizeof d);
        if (fast && vka_ds_eq(ds, &d)) ch = 0; else *ds = d; break; }
    default: break;
    }
    if (VKA_STATE_FAULT(3)) ch = 0;
    return ch;
}

static inline int vka_state_apply_vattr(int fast, int vattr_clear, va_st attr[16], int *nattr, const p_bind_vattr *b) {
    assert(attr != NULL && nattr != NULL && b != NULL);
    if (b->n < 0 || b->first < 0 || b->first + b->n > 16) return 1;
    if (vattr_clear) {
        uint32_t zero_va = 0u;
        int end = b->first + b->n, same = fast && *nattr == end;
        if (same && memcmp(&attr[b->first], b->st, 4u * (size_t)b->n) != 0) same = 0;
        for (int i = 0; same && i < b->first; i++) if (memcmp(&attr[i], &zero_va, 4u) != 0) same = 0;
        for (int i = end; same && i < 16; i++) if (memcmp(&attr[i], &zero_va, 4u) != 0) same = 0;
        if (same) return 0;
        memset(attr, 0, sizeof(va_st) * 16u);
        memcpy(&attr[b->first], b->st, 4u * (size_t)b->n);
        *nattr = end;
        return VKA_STATE_FAULT(1) ? 0 : 1;
    }
    int end = b->first + b->n, same = fast && end <= *nattr && !memcmp(&attr[b->first], b->st, 4u * (size_t)b->n);
    if (same) return 0;
    memcpy(&attr[b->first], b->st, 4u * (size_t)b->n);
    if (end > *nattr) *nattr = end;
    return VKA_STATE_FAULT(1) ? 0 : 1;
}

static inline int vka_state_apply_vstream(int fast, vs_st stream[16], int *nstream, const p_bind_vstream *b) {
    assert(stream != NULL && nstream != NULL && b != NULL);
    if (b->n < 0 || b->first < 0 || b->first + b->n > 16) return 1;
    int end = b->first + b->n;
    if (fast && end <= *nstream && !memcmp(&stream[b->first], b->st, 8u * (size_t)b->n)) return 0;
    memcpy(&stream[b->first], b->st, 8u * (size_t)b->n);
    if (end > *nstream) *nstream = end;
    return VKA_STATE_FAULT(1) ? 0 : 1;
}

static inline int vka_state_apply_vbuf(int fast, NVNbufferRange vbuf[16], const p_bind_vbuf *b) {
    assert(vbuf != NULL && b != NULL);
    if (b->n < 0 || b->first < 0 || b->first + b->n > 16) return 1;
    if (fast && !memcmp(&vbuf[b->first], b->r, sizeof vbuf[0] * (size_t)b->n)) return 0;
    memcpy(&vbuf[b->first], b->r, sizeof vbuf[0] * (size_t)b->n);
    return 1;
}

static inline int vka_state_apply_ubo(int fast, vka_bstate *bstate, uint32_t ubo_dirty[VKA_NSTAGE], const p_bind_buf *b) {
    assert(bstate != NULL && ubo_dirty != NULL && b != NULL);
    if (b->stage < 0 || b->stage >= VKA_NSTAGE || b->index < 0 || b->index >= 16) return 0;
    vka_range *dst = &bstate->ubo[b->stage][b->index];
    if (fast && dst->addr == b->addr && dst->size == b->size) return 0;
    *dst = (vka_range){b->addr, b->size};
    if (!VKA_STATE_FAULT(3)) ubo_dirty[b->stage] |= 1u << b->index;
    return 1;
}

static inline int vka_state_apply_ssbo(int fast, vka_bstate *bstate, const p_bind_buf *b) {
    assert(bstate != NULL && b != NULL);
    if (b->stage < 0 || b->stage >= VKA_NSTAGE || b->index < 0 || b->index >= 16) return 0;
    vka_range *dst = &bstate->ssbo[b->stage][b->index];
    if (fast && dst->addr == b->addr && dst->size == b->size) return 0;
    *dst = (vka_range){b->addr, b->size};
    return VKA_STATE_FAULT(2) ? 0 : 1;
}

static inline int vka_state_apply_tex(int fast, vka_bstate *bstate, int st, int first, int n, const uint64_t *h) {
    assert(bstate != NULL && h != NULL);
    if (st < 0 || st >= VKA_NSTAGE || first < 0 || n < 0 || first + n > VKA_NTEX) return 0;
    if (fast && !memcmp(&bstate->tex[st][first], h, 8u * (size_t)n)) return 0;
    memcpy(&bstate->tex[st][first], h, 8u * (size_t)n);
    return VKA_STATE_FAULT(2) ? 0 : 1;
}

static inline int vka_state_apply_samp(int fast, vka_bstate *bstate, const p_bind_handles *b) {
    assert(bstate != NULL && b != NULL);
    if (b->stage < 0 || b->stage >= VKA_NSTAGE || b->first < 0 || b->n < 0 || b->first + b->n > 128) return 0;
    if (fast && !memcmp(&bstate->samp[b->stage][b->first], b->h, 8u * (size_t)b->n)) return 0;
    memcpy(&bstate->samp[b->stage][b->first], b->h, 8u * (size_t)b->n);
    return VKA_STATE_FAULT(2) ? 0 : 1;
}

static inline int vka_state_apply_img(int fast, vka_bstate *bstate, const p_bind_handles *b) {
    assert(bstate != NULL && b != NULL);
    if (b->stage < 0 || b->stage >= VKA_NSTAGE || b->first < 0 || b->n < 0 || b->first + b->n > 32) return 0;
    if (fast && !memcmp(&bstate->img[b->stage][b->first], b->h, 8u * (size_t)b->n)) return 0;
    memcpy(&bstate->img[b->stage][b->first], b->h, 8u * (size_t)b->n);
    return VKA_STATE_FAULT(2) ? 0 : 1;
}

static inline int vka_state_apply_program(int fast, const prog_t *bst[VKA_NSTAGE], const prog_t **cprog, const p_bind_program *b) {
    assert(bst != NULL && cprog != NULL && b != NULL);
    int ch = !fast;
    if ((b->stages & 0x20) && *cprog != b->prog) { *cprog = b->prog; ch = 1; }
    for (int st = 0; st < VKA_NSTAGE; st++) {
        if (st != 5 && ((b->stages >> st) & 1) && bst[st] != b->prog) { bst[st] = b->prog; ch = 1; }
    }
    return ch;
}

static inline int vka_state_apply_viewports(int fast, float vp[16][4], int *has_vp, const p_viewports *b) {
    assert(vp != NULL && has_vp != NULL && b != NULL);
    int ch = !fast || !*has_vp;
    for (int i = 0; i < b->n && b->first + i < 16; i++) {
        if (!ch && !memcmp(vp[b->first + i], b->v[i], 16u)) continue;
        memcpy(vp[b->first + i], b->v[i], 16u); ch = 1;
    }
    *has_vp = 1;
    return ch;
}

static inline int vka_state_apply_push(int fast, vka_bstate *bstate, const p_push_const *b) {
    assert(bstate != NULL && b != NULL);
    if ((uint32_t)b->offset + b->size > DMSH_PUSH_SIZE) return 0;
    uint8_t *dst = bstate->push[b->compute ? 1 : 0] + b->offset;
    if (fast && !memcmp(dst, b->data, b->size)) return 0;
    memcpy(dst, b->data, b->size);
    return 1;
}

#endif
