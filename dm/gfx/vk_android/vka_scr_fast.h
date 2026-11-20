#ifndef VKA_SCR_FAST_H
#define VKA_SCR_FAST_H
/* Wave 17 (Opt 19): Lock-free scratch TLAB, pow2 alignment & 256x4 indirect-draw look-ahead index (DM_VK_SCR_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W17_TEST_HOOKS
extern int g_w17_fault;
#define VKA_SCR_FAULT(n) (g_w17_fault == (n))
#else
#define VKA_SCR_FAULT(n) 0
#endif

static inline int vka_scr_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_SCR_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint64_t vka_scr_align_up(uint64_t off, uint64_t align) {
    assert(align > 0u);
    if (VKA_SCR_FAULT(1)) return off;
    if ((align & (align - 1u)) == 0u) return (off + align - 1u) & ~(align - 1u);
    return (off + align - 1u) / align * align;
}

enum {
    VKA_SCR_TLAB_MAX_REQ = 16384u,
    VKA_SCR_TLAB_CHUNK = 131072u,
    VKA_IND_SETS = 64u,
    VKA_IND_WAYS = 4u
};

typedef struct {
    uint64_t ep, buf_u, cur, end;
    uint8_t *base;
    void *blk_tag;
} vka_scr_tlab;

static inline int vka_scr_tlab_try(vka_scr_tlab *t, uint64_t cur_ep, uint64_t n,
                                     uint64_t align, uint64_t *buf_out,
                                     uint8_t **ptr_out, uint64_t *off_out) {
    assert(t != NULL && buf_out != NULL && ptr_out != NULL && off_out != NULL && align > 0u);
    if ((!VKA_SCR_FAULT(2) && t->ep != cur_ep) || !t->base) return 0;
    uint64_t o = vka_scr_align_up(t->cur, align);
    if (o > t->end || n > t->end - o) return 0;
    t->cur = o + n;
    *buf_out = t->buf_u;
    *ptr_out = t->base + o;
    *off_out = o;
    return 1;
}

static inline void vka_scr_tlab_fill(vka_scr_tlab *t, uint64_t cur_ep, uint64_t buf_u,
                                     uint8_t *base, void *blk_tag, uint64_t cur, uint64_t end) {
    assert(t != NULL && base != NULL && cur <= end);
    *t = (vka_scr_tlab){cur_ep, buf_u, cur, end, base, blk_tag};
}

typedef struct {
    uint64_t ind[VKA_IND_WAYS];
    int32_t slot[VKA_IND_WAYS];
    uint32_t gen;
    uint8_t count, overflow, pad[2];
} vka_ind_set;

typedef struct {
    uint32_t cur_gen;
    vka_ind_set s[VKA_IND_SETS];
} vka_ind_idx4;

static inline uint32_t vka_ind_set_idx(uint64_t ind) {
    uint64_t x = ((ind >> 4) ^ (ind >> 16) ^ (ind >> 28)) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> 58) & (VKA_IND_SETS - 1u);
}

static inline void vka_ind_idx4_clear(vka_ind_idx4 *m) {
    assert(m != NULL);
    m->cur_gen++;
    if (!m->cur_gen) {
        memset(m, 0, sizeof *m);
        m->cur_gen = 1u;
    }
}

static inline void vka_ind_idx4_put(vka_ind_idx4 *m, uint64_t ind, int32_t slot) {
    assert(m != NULL);
    if (!m->cur_gen) m->cur_gen = 1u;
    vka_ind_set *set = &m->s[vka_ind_set_idx(ind)];
    if (set->gen != m->cur_gen) {
        set->gen = m->cur_gen;
        set->count = 0u;
        set->overflow = 0u;
    }
    for (uint8_t i = 0; i < set->count && i < VKA_IND_WAYS; i++) {
        if (set->ind[i] == ind) { set->slot[i] = slot; return; }
    }
    if (set->count < VKA_IND_WAYS) {
        set->ind[set->count] = ind;
        set->slot[set->count] = slot;
        set->count++;
    } else {
        set->overflow = 1u;
    }
}

static inline int32_t vka_ind_idx4_find(const vka_ind_idx4 *m, uint64_t ind, int *need_linear) {
    assert(m != NULL && need_linear != NULL);
    *need_linear = 0;
    if (!m->cur_gen) return -1;
    const vka_ind_set *set = &m->s[vka_ind_set_idx(ind)];
    if (set->gen != m->cur_gen) return -1;
    for (uint8_t i = 0; i < set->count && i < VKA_IND_WAYS; i++) {
        int ok = VKA_SCR_FAULT(3) ? ((set->ind[i] & 0xffffu) == (ind & 0xffffu)) : (set->ind[i] == ind);
        if (ok) return set->slot[i];
    }
    *need_linear = (set->overflow != 0u);
    return -1;
}

#endif
