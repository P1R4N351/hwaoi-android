#ifndef VKA_TBUF_FAST_H
#define VKA_TBUF_FAST_H
/* Wave 15 (Opt 17): Lock-free texel-buffer VkBufferView, sampled buffer-image & VkFormat front-memos (DM_VK_TBUF_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W15_TEST_HOOKS
extern int g_w15_fault;
#define VKA_TBUF_FAULT(n) (g_w15_fault == (n))
#else
#define VKA_TBUF_FAULT(n) 0
#endif

static inline int vka_tbuf_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_TBUF_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint64_t vka_tbuf_epoch_mix(uint64_t mem_gen, uint32_t seg_gen,
                                          uint64_t pool_fin, uint32_t env_gen,
                                          uint32_t evict_gen) {
    uint64_t x = mem_gen ^ ((uint64_t)seg_gen * UINT64_C(0x9e3779b97f4a7c15));
    x ^= (pool_fin + UINT64_C(0x517cc1b727220a95)) * UINT64_C(0xbf58476d1ce4e5b9);
    x ^= ((uint64_t)env_gen << 32) | (uint64_t)evict_gen;
    x ^= x >> 30;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x ? x : 1ull;
}

enum { VKA_TBUF_SETS = 64u, VKA_TBUF_WAYS = 4u, VKA_TBUF_FMT_SETS = 16u };

typedef struct {
    uint32_t key, tver;
    uint64_t epoch, bv_u, addr, range;
    uint16_t slot, pad;
} vka_tbuf_view_ent;

typedef struct {
    vka_tbuf_view_ent w[VKA_TBUF_WAYS];
    uint8_t hand, pad[7];
} vka_tbuf_view_set;

typedef struct {
    vka_tbuf_view_set s[VKA_TBUF_SETS];
} vka_tbuf_view_memo4;

static inline uint32_t vka_tbuf_set_idx(uint32_t key) {
    uint32_t x = key ^ (key >> 7) ^ (key >> 14);
    return x & (VKA_TBUF_SETS - 1u);
}

static inline int vka_tbuf_view_memo4_find(const vka_tbuf_view_memo4 *m, uint32_t id,
                                           int writes, uint32_t tver, uint64_t epoch,
                                           uint64_t *bv_out, uint64_t *addr_out,
                                           uint64_t *range_out, uint16_t *slot_out) {
    assert(m != NULL && bv_out != NULL && addr_out != NULL && range_out != NULL && slot_out != NULL);
    uint32_t key = (id << 1) | (uint32_t)(writes != 0);
    const vka_tbuf_view_set *set = &m->s[vka_tbuf_set_idx(key)];
    for (uint32_t i = 0; i < VKA_TBUF_WAYS; i++) {
        const vka_tbuf_view_ent *e = &set->w[i];
        if (!e->bv_u || e->key != key) continue;
        if (!VKA_TBUF_FAULT(1) && e->epoch != epoch) continue;
        if (!VKA_TBUF_FAULT(2) && e->tver != tver) continue;
        *bv_out = e->bv_u;
        *addr_out = e->addr;
        *range_out = e->range;
        *slot_out = e->slot;
        return 1;
    }
    return 0;
}

static inline void vka_tbuf_view_memo4_put(vka_tbuf_view_memo4 *m, uint32_t id, int writes,
                                           uint32_t tver, uint64_t epoch, uint64_t bv_u,
                                           uint64_t addr, uint64_t range, uint16_t slot) {
    assert(m != NULL);
    if (!bv_u || !tver) return;
    uint32_t key = (id << 1) | (uint32_t)(writes != 0);
    vka_tbuf_view_set *set = &m->s[vka_tbuf_set_idx(key)];
    for (uint32_t i = 0; i < VKA_TBUF_WAYS; i++) {
        if (set->w[i].key == key || !set->w[i].bv_u) {
            set->w[i] = (vka_tbuf_view_ent){key, tver, epoch, bv_u, addr, range, slot, 0u};
            return;
        }
    }
    uint32_t v = set->hand & (VKA_TBUF_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_TBUF_WAYS - 1u));
    set->w[v] = (vka_tbuf_view_ent){key, tver, epoch, bv_u, addr, range, slot, 0u};
}

typedef struct {
    uint32_t key, tver;
    uint64_t frame, write_gen, epoch, iv_u;
    uint16_t slot, pad;
} vka_tbuf_img_ent;

typedef struct {
    vka_tbuf_img_ent w[VKA_TBUF_WAYS];
    uint8_t hand, pad[7];
} vka_tbuf_img_set;

typedef struct {
    vka_tbuf_img_set s[VKA_TBUF_SETS];
} vka_tbuf_img_memo4;

static inline int vka_tbuf_img_memo4_find(const vka_tbuf_img_memo4 *m, uint32_t id, int is2d,
                                          uint32_t tver, uint64_t frame, uint64_t write_gen,
                                          uint64_t epoch, uint64_t *iv_out, uint16_t *slot_out) {
    assert(m != NULL && iv_out != NULL && slot_out != NULL);
    uint32_t key = (id << 1) | (uint32_t)(is2d != 0);
    const vka_tbuf_img_set *set = &m->s[vka_tbuf_set_idx(key)];
    for (uint32_t i = 0; i < VKA_TBUF_WAYS; i++) {
        const vka_tbuf_img_ent *e = &set->w[i];
        if (!e->iv_u || e->key != key || e->tver != tver || e->epoch != epoch) continue;
        if (!VKA_TBUF_FAULT(3) && (e->frame != frame || e->write_gen != write_gen)) continue;
        *iv_out = e->iv_u;
        *slot_out = e->slot;
        return 1;
    }
    return 0;
}

static inline void vka_tbuf_img_memo4_put(vka_tbuf_img_memo4 *m, uint32_t id, int is2d,
                                          uint32_t tver, uint64_t frame, uint64_t write_gen,
                                          uint64_t epoch, uint64_t iv_u, uint16_t slot) {
    assert(m != NULL);
    if (!iv_u || !tver) return;
    uint32_t key = (id << 1) | (uint32_t)(is2d != 0);
    vka_tbuf_img_set *set = &m->s[vka_tbuf_set_idx(key)];
    for (uint32_t i = 0; i < VKA_TBUF_WAYS; i++) {
        if (set->w[i].key == key || !set->w[i].iv_u) {
            set->w[i] = (vka_tbuf_img_ent){key, tver, frame, write_gen, epoch, iv_u, slot, 0u};
            return;
        }
    }
    uint32_t v = set->hand & (VKA_TBUF_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_TBUF_WAYS - 1u));
    set->w[v] = (vka_tbuf_img_ent){key, tver, frame, write_gen, epoch, iv_u, slot, 0u};
}

typedef struct {
    uint64_t pd_u;
    uint32_t fmt, features;
    uint8_t valid, pad[3];
} vka_tbuf_fmt_ent;

typedef struct {
    vka_tbuf_fmt_ent w[VKA_TBUF_WAYS];
    uint8_t hand;
} vka_tbuf_fmt_set;

typedef struct {
    vka_tbuf_fmt_set s[VKA_TBUF_FMT_SETS];
} vka_tbuf_fmt_memo;

static inline int vka_tbuf_fmt_memo_get(const vka_tbuf_fmt_memo *m, uint64_t pd_u,
                                        uint32_t fmt, uint32_t *features_out) {
    assert(m != NULL && features_out != NULL);
    if (!fmt) return 0;
    uint32_t idx = (fmt ^ (fmt >> 4)) & (VKA_TBUF_FMT_SETS - 1u);
    const vka_tbuf_fmt_set *set = &m->s[idx];
    for (uint32_t i = 0; i < VKA_TBUF_WAYS; i++) {
        const vka_tbuf_fmt_ent *e = &set->w[i];
        if (e->valid && e->fmt == fmt && e->pd_u == pd_u) {
            *features_out = e->features;
            return 1;
        }
    }
    return 0;
}

static inline void vka_tbuf_fmt_memo_put(vka_tbuf_fmt_memo *m, uint64_t pd_u,
                                         uint32_t fmt, uint32_t features) {
    assert(m != NULL);
    if (!fmt) return;
    uint32_t idx = (fmt ^ (fmt >> 4)) & (VKA_TBUF_FMT_SETS - 1u);
    vka_tbuf_fmt_set *set = &m->s[idx];
    for (uint32_t i = 0; i < VKA_TBUF_WAYS; i++) {
        if (!set->w[i].valid || (set->w[i].fmt == fmt && set->w[i].pd_u == pd_u)) {
            set->w[i] = (vka_tbuf_fmt_ent){pd_u, fmt, features, 1u, {0, 0, 0}};
            return;
        }
    }
    uint32_t v = set->hand & (VKA_TBUF_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_TBUF_WAYS - 1u));
    set->w[v] = (vka_tbuf_fmt_ent){pd_u, fmt, features, 1u, {0, 0, 0}};
}

#endif
