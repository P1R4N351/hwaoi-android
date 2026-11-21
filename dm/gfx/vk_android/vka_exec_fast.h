// Fast replay dispatch, strided render-condition lookup, pre-draw guard & opcode bitmasks (DM_VK_EXEC_FAST=1, default on; =0 disables).
#ifndef VKA_EXEC_FAST_CORE_H
#define VKA_EXEC_FAST_CORE_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W35_TEST_HOOKS
extern _Atomic int g_vka_exec_fast_state;
extern _Atomic int g_vka_w35_fault;
static inline void vka_exec_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_exec_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w35_set_fault(int mode) {
    atomic_store_explicit(&g_vka_w35_fault, mode, memory_order_release);
}
static inline int vka_w35_fault(void) {
    return atomic_load_explicit(&g_vka_w35_fault, memory_order_acquire);
}
#else
static _Atomic int g_vka_exec_fast_state;
static inline int vka_w35_fault(void) { return 0; }
#endif

static inline int vka_exec_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_exec_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_EXEC_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_exec_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline int vka_rc_qmem_sync_on(uint64_t env_gen, int (*slow_fn)(void)) {
    static _Atomic uint64_t g_qm_gen = UINT64_MAX;
    static _Atomic int g_qm_val;
    if (atomic_load_explicit(&g_qm_gen, memory_order_acquire) == env_gen) {
        return atomic_load_explicit(&g_qm_val, memory_order_relaxed);
    }
    int v = slow_fn ? (slow_fn() != 0) : 0;
    atomic_store_explicit(&g_qm_val, v, memory_order_relaxed);
    atomic_store_explicit(&g_qm_gen, env_gen, memory_order_release);
    return v;
}

typedef struct { uint64_t addr, sub; uint32_t kind, pad; } vka_rc_ent_w35;
_Static_assert(sizeof(vka_rc_ent_w35) == 24u, "vka_rc_ent_w35 size");

typedef struct { uint64_t addr[64]; uint32_t idx_p1[64]; } vka_rcm_front64;

static inline uint64_t vka_rcm_hash64(uint64_t a) {
    a ^= a >> 30;
    a *= UINT64_C(0xbf58476d1ce4e5b9);
    a ^= a >> 27;
    a *= UINT64_C(0x94d049bb133111eb);
    return a ^ (a >> 31);
}

static inline vka_rc_ent_w35 *vka_rcm_find_fast(vka_rc_ent_w35 *tab, uint32_t cap,
                                                vka_rcm_front64 *fc, uint64_t a, int fault_mode) {
    if (!cap || !a || !tab) return NULL;
    uint64_t h = vka_rcm_hash64(a);
    uint32_t fs = (uint32_t)h & 63u;
    if (fc && fc->idx_p1[fs] && fc->addr[fs] == a) {
        uint32_t idx = fc->idx_p1[fs] - 1u;
        if (idx < cap && (fault_mode == 1 || tab[idx].addr == a)) return &tab[idx];
    }
    uint32_t mask = cap - 1u, i = (uint32_t)h & mask;
    for (uint32_t step = 0; step < cap; step++) {
        if (tab[i].addr == a) {
            if (fc) { fc->addr[fs] = a; fc->idx_p1[fs] = i + 1u; }
            return &tab[i];
        }
        if (!tab[i].addr) return NULL;
        i = (i + 1u) & mask;
    }
    return NULL;
}

static inline int vka_rcm_grow_fast(vka_rc_ent_w35 **tab, uint32_t *cap, uint32_t max_entries,
                                    vka_rcm_front64 *fc, int fault_mode) {
    uint32_t nc = *cap ? *cap * 2u : 4096u;
    if (nc > 2u * max_entries) return 0;
    vka_rc_ent_w35 *t = (vka_rc_ent_w35 *)calloc(nc, sizeof *t);
    if (!t) return 0;
    vka_rc_ent_w35 *old = *tab;
    uint32_t oc = *cap, mask = nc - 1u;
    for (uint32_t k = 0; k < oc; k++) {
        if (!old[k].addr) continue;
        uint32_t i = (uint32_t)vka_rcm_hash64(old[k].addr) & mask;
        for (uint32_t st = 0; st < nc && t[i].addr; st++) i = (i + 1u) & mask;
        t[i] = old[k];
    }
    free(old); *tab = t; *cap = nc;
    if (fc && fault_mode != 1) memset(fc, 0, sizeof *fc);
    return 1;
}

static inline vka_rc_ent_w35 *vka_rcm_note_fast(vka_rc_ent_w35 **tab, uint32_t *cap, uint32_t *cnt,
                                                  uint32_t max_entries, vka_rcm_front64 *fc,
                                                  uint64_t a, uint64_t sub, uint32_t kind,
                                                  int fault_mode, int *oom) {
    *oom = 0;
    if (!a) return NULL;
    vka_rc_ent_w35 *e = vka_rcm_find_fast(*tab, *cap, fc, a, fault_mode);
    if (!e) {
        if ((*cnt + 1u) * 2u > *cap && !vka_rcm_grow_fast(tab, cap, max_entries, fc, fault_mode)) {
            *oom = 1;
            return NULL;
        }
        if (!*tab || !*cap) return NULL;
        uint64_t h = vka_rcm_hash64(a);
        uint32_t mask = *cap - 1u, i = (uint32_t)h & mask;
        for (uint32_t st = 0; st < *cap && (*tab)[i].addr; st++) i = (i + 1u) & mask;
        e = &(*tab)[i];
        e->addr = a;
        (*cnt)++;
        if (fc) { uint32_t fs = (uint32_t)h & 63u; fc->addr[fs] = a; fc->idx_p1[fs] = i + 1u; }
    }
    e->sub = sub; e->kind = kind;
    return e;
}

/* 0: run full pre-draw guards; 1: skip ind_before_draw(p, NULL) when pass open; 2: skip both. */
static inline int vka_exec_idx_skip_pre(int rp_on, int memu_on, int fault_mode) {
    if (fault_mode == 5) return 2;
    if (!rp_on) return 0;
    return memu_on ? 1 : 2;
}
#endif

#if defined(VKA_EXEC_HAVE_OPS) && !defined(VKA_EXEC_FAST_OPS_H)
#define VKA_EXEC_FAST_OPS_H
_Static_assert(OP_COUNT_ <= 64, "opcode masks fit in uint64_t");

static inline int vka_op_bit_test(uint64_t mask, uint32_t op) {
    return op < 64u && ((mask >> op) & UINT64_C(1)) != 0u;
}

static inline int vka_op_memu_allowed_fast(uint32_t op, int fault_mode) {
    const uint64_t m =
        (UINT64_C(1) << OP_END) | (UINT64_C(1) << OP_JUMP) | (UINT64_C(1) << OP_BEGIN) |
        (UINT64_C(1) << OP_CALL) | (UINT64_C(1) << OP_DRAW) | (UINT64_C(1) << OP_DRAW_IDX) |
        (UINT64_C(1) << OP_SCISSOR) | (UINT64_C(1) << OP_COPY_STRIDE) | (UINT64_C(1) << OP_CLEAR_COLOR) |
        (UINT64_C(1) << OP_CLEAR_DS) | (UINT64_C(1) << OP_REPORT_COUNTER) | (UINT64_C(1) << OP_RESET_COUNTER) |
        (UINT64_C(1) << OP_FENCE_SYNC) | (UINT64_C(1) << OP_VIEWPORTS) | (UINT64_C(1) << OP_DEPTH_RANGE) |
        (UINT64_C(1) << OP_BIND_PROGRAM) | (UINT64_C(1) << OP_BIND_STATE) | (UINT64_C(1) << OP_BIND_VATTR) |
        (UINT64_C(1) << OP_BIND_VSTREAM) | (UINT64_C(1) << OP_BIND_VBUF) | (UINT64_C(1) << OP_BIND_VBUF1) |
        (UINT64_C(1) << OP_BIND_UBO) | (UINT64_C(1) << OP_BIND_UBO_SMALL) | (UINT64_C(1) << OP_BIND_SSBO) |
        (UINT64_C(1) << OP_BIND_TEX) | (UINT64_C(1) << OP_BIND_TEX1) | (UINT64_C(1) << OP_BIND_SAMP) |
        (UINT64_C(1) << OP_BIND_IMG) | (UINT64_C(1) << OP_STENCIL) | (UINT64_C(1) << OP_POLY_OFFSET) |
        (UINT64_C(1) << OP_SET_TEX_POOL) | (UINT64_C(1) << OP_SET_SAMP_POOL) | (UINT64_C(1) << OP_BLEND_COLOR) |
        (UINT64_C(1) << OP_DEPTH_CLAMP) | (UINT64_C(1) << OP_TF_BIND) | (UINT64_C(1) << OP_TF_STATE) |
        (UINT64_C(1) << OP_RAST_DISCARD) | (UINT64_C(1) << OP_VP_SWIZZLE) | (UINT64_C(1) << OP_PRIM_RESTART) |
        (UINT64_C(1) << OP_ALPHA_REF) | (UINT64_C(1) << OP_PUSH_CONST);
    uint64_t eff = (fault_mode == 3) ? (m | (UINT64_C(1) << OP_DISPATCH)) : m;
    return vka_op_bit_test(eff, op);
}

static inline int vka_op_need_settle_fast(uint32_t op, int fault_mode) {
    const uint64_t m =
        (UINT64_C(1) << OP_DRAW) | (UINT64_C(1) << OP_DRAW_IDX) | (UINT64_C(1) << OP_DRAW_TEX) |
        (UINT64_C(1) << OP_CLEAR_COLOR) | (UINT64_C(1) << OP_CLEAR_DS) | (UINT64_C(1) << OP_CLEAR_TEX) |
        (UINT64_C(1) << OP_CLEAR_BUF) | (UINT64_C(1) << OP_COPY_B2B) | (UINT64_C(1) << OP_COPY_B2T) |
        (UINT64_C(1) << OP_COPY_T2B) | (UINT64_C(1) << OP_COPY_T2T) | (UINT64_C(1) << OP_UPDATE_UB) |
        (UINT64_C(1) << OP_REPORT_COUNTER);
    uint64_t eff = (fault_mode == 2) ? (m & ~(UINT64_C(1) << OP_COPY_T2B)) : m;
    return vka_op_bit_test(eff, op);
}

static inline int vka_op_rp_nop_fast(uint32_t op, int fault_mode) {
    const uint64_t m =
        (UINT64_C(1) << OP_END) | (UINT64_C(1) << OP_BEGIN) | (UINT64_C(1) << OP_JUMP) |
        (UINT64_C(1) << OP_CALL) | (UINT64_C(1) << OP_SIGNAL_EVENT) | (UINT64_C(1) << OP_REPORT_COUNTER) |
        (UINT64_C(1) << OP_FENCE_SYNC) | (UINT64_C(1) << OP_RENDER_COND) | (UINT64_C(1) << OP_RESET_COUNTER) |
        (UINT64_C(1) << OP_WAIT_EVENT) | (UINT64_C(1) << OP_WAIT_SYNC) | (UINT64_C(1) << OP_SCISSOR) |
        (UINT64_C(1) << OP_COPY_STRIDE) | (UINT64_C(1) << OP_UPDATE_UB) | (UINT64_C(1) << OP_DRAW) |
        (UINT64_C(1) << OP_DRAW_IDX) | (UINT64_C(1) << OP_CLEAR_COLOR) | (UINT64_C(1) << OP_CLEAR_DS);
    uint64_t eff = (fault_mode == 4) ? (m | (UINT64_C(1) << OP_SET_RT)) : m;
    return vka_op_bit_test(eff, op);
}

static inline int vka_op_ind_write_fast(uint32_t op) {
    const uint64_t m =
        (UINT64_C(1) << OP_DISPATCH) | (UINT64_C(1) << OP_COPY_B2B) | (UINT64_C(1) << OP_CLEAR_BUF) |
        (UINT64_C(1) << OP_COPY_T2B) | (UINT64_C(1) << OP_UPDATE_UB);
    return vka_op_bit_test(m, op);
}
#endif
