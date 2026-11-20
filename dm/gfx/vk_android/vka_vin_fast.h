#ifndef VKA_VIN_FAST_H
#define VKA_VIN_FAST_H
/* Wave 9 (Opt 11): multi-material 16x4 vertex-input memo, stream-subset VBO tracker & 128x4 address-run memo (DM_VK_VIN_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef W9_TEST_HOOKS
extern int g_w9_fault;
#define VKA_VIN_FAST_FAULT(n) (g_w9_fault == (n))
#else
#define VKA_VIN_FAST_FAULT(n) 0
#endif

enum { VKA_VC_SETS = 16, VKA_VC_WAYS = 4, VKA_VC_SLOTS = 64, VKA_ADDRM_SETS = 128, VKA_ADDRM_WAYS = 4 };

typedef struct {
    uint64_t vs_u, s_vfmt;
    uint32_t ser, vin_u, vin_s;
    uint16_t used;
    uint8_t nb, na, zb, zero, valid, pad;
} vka_vinfast_vc_ent;

typedef struct {
    vka_vinfast_vc_ent e[VKA_VC_WAYS];
    uint32_t rr, pad;
} vka_vinfast_vc_set;

typedef struct {
    vka_vinfast_vc_set s[VKA_VC_SETS];
} vka_vinfast_vc_memo4;

typedef struct {
    uint64_t s_vbuf, mg;
    uint32_t bound_used;
    int32_t bound_zb;
    int valid, pad;
} vka_vinfast_vbc;

typedef struct {
    uint64_t mg, addr, req_len, buf_u, off, run;
} vka_addrm4_ent;

typedef struct {
    vka_addrm4_ent e[VKA_ADDRM_WAYS];
    uint32_t rr, pad;
} vka_addrm4_set;

typedef struct {
    vka_addrm4_set s[VKA_ADDRM_SETS];
} vka_addrm4;

static inline int vka_vin_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_VIN_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint32_t vka_vin_mix32(uint64_t x, uint32_t sets) {
    uint64_t h = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((h ^ (h >> 31)) & (uint64_t)(sets - 1u));
}

static inline const vka_vinfast_vc_ent *vka_vinfast_vc_find(const vka_vinfast_vc_memo4 *m, uint64_t vs_u, uint64_t s_vfmt,
                                                             uint32_t *slot_out) {
    assert(m != NULL && slot_out != NULL);
    if (!vs_u) return NULL;
    uint32_t idx = vka_vin_mix32(vs_u ^ (s_vfmt * 0x9e3779b97f4a7c15ull), VKA_VC_SETS);
    const vka_vinfast_vc_set *set = &m->s[idx];
    for (uint32_t w = 0; w < VKA_VC_WAYS; w++) {
        const vka_vinfast_vc_ent *e = &set->e[w];
        if (e->valid && e->vs_u == vs_u && (VKA_VIN_FAST_FAULT(1) || e->s_vfmt == s_vfmt)) {
            *slot_out = idx * VKA_VC_WAYS + w;
            return e;
        }
    }
    return NULL;
}

static inline uint32_t vka_vinfast_vc_alloc(vka_vinfast_vc_memo4 *m, const vka_vinfast_vc_ent *in) {
    assert(m != NULL && in != NULL && in->vs_u != 0);
    uint32_t idx = vka_vin_mix32(in->vs_u ^ (in->s_vfmt * 0x9e3779b97f4a7c15ull), VKA_VC_SETS);
    vka_vinfast_vc_set *set = &m->s[idx];
    uint32_t w = VKA_VC_WAYS;
    for (uint32_t i = 0; i < VKA_VC_WAYS; i++) {
        if (!set->e[i].valid || (set->e[i].vs_u == in->vs_u && set->e[i].s_vfmt == in->s_vfmt)) { w = i; break; }
    }
    if (w == VKA_VC_WAYS) w = set->rr++ & (VKA_VC_WAYS - 1u);
    set->e[w] = *in;
    set->e[w].valid = 1;
    return idx * VKA_VC_WAYS + w;
}

static inline void vka_vinfast_vbc_reset(vka_vinfast_vbc *v) {
    assert(v != NULL);
    v->s_vbuf = 0; v->mg = 0; v->bound_used = 0; v->bound_zb = -1; v->valid = 0; v->pad = 0;
}

static inline int vka_vinfast_need_streams(const vka_vinfast_vbc *v, int vb_valid, uint64_t s_vbuf, uint64_t mg,
                                            uint32_t vi_used, int vi_zero, uint32_t vi_zb,
                                            uint32_t *need_used, int *need_zero) {
    assert(v != NULL && need_used != NULL && need_zero != NULL);
    if (!vb_valid || !v->valid || v->s_vbuf != s_vbuf || (!VKA_VIN_FAST_FAULT(2) && v->mg != mg)) {
        *need_used = vi_used;
        *need_zero = vi_zero != 0;
    } else {
        *need_used = vi_used & ~v->bound_used;
        *need_zero = (vi_zero != 0) && (v->bound_zb != (int32_t)vi_zb);
    }
    return (*need_used != 0u) || (*need_zero != 0);
}

static inline void vka_vinfast_note_streams(vka_vinfast_vbc *v, int vb_valid, uint64_t s_vbuf, uint64_t mg,
                                            uint32_t bound_now, int zero_now, uint32_t vi_zb) {
    assert(v != NULL);
    if (!vb_valid || !v->valid || v->s_vbuf != s_vbuf || v->mg != mg) {
        v->valid = 1; v->s_vbuf = s_vbuf; v->mg = mg; v->bound_used = 0; v->bound_zb = -1;
    }
    if (v->bound_zb >= 0 && v->bound_zb < 16 && ((bound_now >> (uint32_t)v->bound_zb) & 1u)) v->bound_zb = -1;
    v->bound_used |= bound_now;
    if (zero_now) {
        if (vi_zb < 16u) v->bound_used &= ~(1u << vi_zb);
        v->bound_zb = (int32_t)vi_zb;
    }
}

static inline int vka_addrm4_find(const vka_addrm4 *m, uint64_t mg, uint64_t addr, uint64_t req_len,
                                  uint64_t *buf_out, uint64_t *off_out, uint64_t *run_out) {
    assert(m != NULL && buf_out != NULL && off_out != NULL && run_out != NULL);
    if (!mg || !addr) return 0;
    const vka_addrm4_set *set = &m->s[vka_vin_mix32((addr >> 4) ^ (req_len * 0x9e3779b97f4a7c15ull), VKA_ADDRM_SETS)];
    for (uint32_t w = 0; w < VKA_ADDRM_WAYS; w++) {
        const vka_addrm4_ent *e = &set->e[w];
        if (e->run && e->mg == mg && e->req_len == req_len && (VKA_VIN_FAST_FAULT(3) || e->addr == addr)) {
            *buf_out = e->buf_u; *off_out = e->off; *run_out = e->run;
            return 1;
        }
    }
    return 0;
}

static inline void vka_addrm4_put(vka_addrm4 *m, uint64_t mg, uint64_t addr, uint64_t req_len,
                                  uint64_t buf_u, uint64_t off, uint64_t run) {
    assert(m != NULL);
    if (!mg || !addr || !run) return;
    vka_addrm4_set *set = &m->s[vka_vin_mix32((addr >> 4) ^ (req_len * 0x9e3779b97f4a7c15ull), VKA_ADDRM_SETS)];
    uint32_t slot = VKA_ADDRM_WAYS;
    for (uint32_t w = 0; w < VKA_ADDRM_WAYS; w++) {
        const vka_addrm4_ent *e = &set->e[w];
        if (!e->run || e->mg != mg || (e->addr == addr && e->req_len == req_len)) { slot = w; break; }
    }
    if (slot == VKA_ADDRM_WAYS) slot = set->rr++ & (VKA_ADDRM_WAYS - 1u);
    set->e[slot] = (vka_addrm4_ent){mg, addr, req_len, buf_u, off, run};
}

#endif
