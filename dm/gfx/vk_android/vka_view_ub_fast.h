// Fast O(1) shared image-view lookup, format-bytes memo, deferred UBO/region hazard filter & inst/prepass maps (Opt 53).
#ifndef VKA_VIEW_UB_FAST_H
#define VKA_VIEW_UB_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W51_TEST_HOOKS
extern _Atomic int g_vka_view_ub_fast_override;
extern _Atomic int g_aoi_w51_fault;
static inline void vka_view_ub_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_view_ub_fast_override, v, memory_order_release);
}
static inline void aoi_w51_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w51_fault, f, memory_order_release);
}
static inline int aoi_w51_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w51_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W51_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w51_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w51_fault_mode(void) { return 0; }
#endif

static inline int vka_view_ub_fast_on(void) {
#ifdef T_W51_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_view_ub_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_VIEW_UB_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_vu_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline int vka_iview_key_pack(int32_t fmt, uint32_t type, uint32_t asp,
                                     uint32_t level, uint32_t nlevels,
                                     uint32_t layer, uint32_t nlayers,
                                     uint32_t sr, uint32_t sg, uint32_t sb, uint32_t sa,
                                     uint64_t *vk0_out, uint64_t *vk1_out) {
    if (type >= 256u || asp >= 65536u || level >= 256u || nlevels >= 256u ||
        layer >= 65536u || nlayers >= 65536u || sr >= 8u || sg >= 8u || sb >= 8u || sa >= 8u)
        return 0;
    *vk0_out = (uint64_t)(uint32_t)fmt | ((uint64_t)type << 32) | ((uint64_t)asp << 40);
    if (aoi_w51_fault_mode() == 1) {
        *vk1_out = (uint64_t)level | ((uint64_t)nlevels << 8) | ((uint64_t)layer << 16);
        return 1;
    }
    *vk1_out = (uint64_t)level | ((uint64_t)nlevels << 8) | ((uint64_t)layer << 16) |
               ((uint64_t)nlayers << 32) | ((uint64_t)sr << 48) | ((uint64_t)sg << 51) |
               ((uint64_t)sb << 54) | ((uint64_t)sa << 57);
    return 1;
}

enum { VKA_IVM_CAP = 8192u, VKA_IVM_MASK = VKA_IVM_CAP - 1u };

typedef struct {
    uint64_t view_ep, vk0, vk1, view_u;
    uint32_t tgen, pad_;
} vka_img_view_slot_t;

typedef struct {
    vka_img_view_slot_t slots[VKA_IVM_CAP];
    uint32_t tgen, inserted;
} vka_img_view_map_t;

static inline uint32_t vka_ivm_slot_hash(uint64_t view_ep, uint64_t vk0, uint64_t vk1) {
    uint64_t h = vka_vu_mix64(view_ep ^ vka_vu_mix64(vk0));
    h = vka_vu_mix64(h ^ vk1);
    return (uint32_t)h & VKA_IVM_MASK;
}

static inline int vka_img_view_map_find(const vka_img_view_map_t *m, uint64_t view_ep,
                                        uint64_t vk0, uint64_t vk1, uint64_t *view_out) {
    if (!m->tgen || !view_ep) return 0;
    uint32_t slot = vka_ivm_slot_hash(view_ep, vk0, vk1);
    int f2 = aoi_w51_fault_mode() == 2;
    for (uint32_t probe = 0u; probe < VKA_IVM_CAP; probe++) {
        const vka_img_view_slot_t *e = &m->slots[slot];
        if (e->tgen != m->tgen) return 0;
        if (e->view_ep == view_ep && e->vk0 == vk0 && (f2 || e->vk1 == vk1)) {
            *view_out = e->view_u;
            return 1;
        }
        slot = (slot + 1u) & VKA_IVM_MASK;
    }
    return 0;
}

static inline uint32_t vka_img_view_map_put(vka_img_view_map_t *m, uint64_t view_ep,
                                            uint64_t vk0, uint64_t vk1, uint64_t view_u) {
    if (!view_ep || !view_u) return 0u;
    if (!m->tgen) m->tgen = 1u;
    if (m->inserted >= VKA_IVM_CAP - 1024u) {
        m->tgen++;
        m->inserted = 0u;
        if (!m->tgen) { memset(m->slots, 0, sizeof m->slots); m->tgen = 1u; }
    }
    uint32_t slot = vka_ivm_slot_hash(view_ep, vk0, vk1);
    for (uint32_t probe = 0u; probe < VKA_IVM_CAP; probe++) {
        vka_img_view_slot_t *e = &m->slots[slot];
        if (e->tgen != m->tgen) {
            e->view_ep = view_ep; e->vk0 = vk0; e->vk1 = vk1;
            e->view_u = view_u; e->tgen = m->tgen;
            m->inserted++;
            return m->tgen;
        }
        if (e->view_ep == view_ep && e->vk0 == vk0 && e->vk1 == vk1) {
            e->view_u = view_u;
            return m->tgen;
        }
        slot = (slot + 1u) & VKA_IVM_MASK;
    }
    return 0u;
}

enum { VKA_VL_BPB_CAP = 64u, VKA_VL_BPB_MASK = VKA_VL_BPB_CAP - 1u };

typedef struct {
    int32_t fmt, bpb;
    uint8_t used, pad[3];
} vka_vl_bpb_slot_t;

typedef struct {
    vka_vl_bpb_slot_t slots[VKA_VL_BPB_CAP];
    uint32_t inserted;
} vka_vl_bpb_memo_t;

static inline uint32_t vka_vl_bpb_hash(int32_t fmt) {
    uint32_t x = (uint32_t)fmt * UINT32_C(0x9e3779b1);
    return (x ^ (x >> 16)) & VKA_VL_BPB_MASK;
}

static inline int vka_vl_bpb_get(const vka_vl_bpb_memo_t *m, int32_t fmt, int *bpb_out) {
    uint32_t slot = vka_vl_bpb_hash(fmt);
    for (uint32_t probe = 0u; probe < VKA_VL_BPB_CAP; probe++) {
        const vka_vl_bpb_slot_t *e = &m->slots[slot];
        if (!e->used) return 0;
        if (e->fmt == fmt) {
            *bpb_out = aoi_w51_fault_mode() == 3 ? (e->bpb ^ 4) : e->bpb;
            return 1;
        }
        slot = (slot + 1u) & VKA_VL_BPB_MASK;
    }
    return 0;
}

static inline void vka_vl_bpb_put(vka_vl_bpb_memo_t *m, int32_t fmt, int bpb) {
    if (m->inserted >= VKA_VL_BPB_CAP - 8u) return;
    uint32_t slot = vka_vl_bpb_hash(fmt);
    for (uint32_t probe = 0u; probe < VKA_VL_BPB_CAP; probe++) {
        vka_vl_bpb_slot_t *e = &m->slots[slot];
        if (!e->used) { e->fmt = fmt; e->bpb = bpb; e->used = 1u; m->inserted++; return; }
        if (e->fmt == fmt) { e->bpb = bpb; return; }
        slot = (slot + 1u) & VKA_VL_BPB_MASK;
    }
}

typedef struct {
    uint64_t min_lo, max_hi, bloom64;
    uint32_t count, pad_;
} vka_range_haz_t;

static inline uint64_t vka_range_haz_mask(uint64_t lo, uint64_t hi) {
    if (hi <= lo) return UINT64_C(0);
    uint64_t b0 = lo >> 8, b1 = (hi - UINT64_C(1)) >> 8;
    if (b1 - b0 >= UINT64_C(8)) return ~UINT64_C(0);
    uint64_t m = UINT64_C(0);
    for (uint64_t b = b0; b <= b1; b++) {
        uint64_t idx = ((b * UINT64_C(0x9e3779b97f4a7c15)) >> 58) & UINT64_C(63);
        m |= UINT64_C(1) << (unsigned)idx;
    }
    return m;
}

static inline int vka_range_haz_may_overlap(const vka_range_haz_t *h, uint64_t lo,
                                            uint64_t hi, uint64_t mask) {
    if (!h->count || hi <= h->min_lo || lo >= h->max_hi) return 0;
    if (aoi_w51_fault_mode() == 4) return 0;
    return (h->bloom64 & mask) != UINT64_C(0);
}

static inline void vka_range_haz_add(vka_range_haz_t *h, uint64_t lo,
                                     uint64_t hi, uint64_t mask) {
    if (hi <= lo) return;
    if (!h->count) {
        h->min_lo = lo; h->max_hi = hi; h->bloom64 = mask; h->count = 1u;
        return;
    }
    if (lo < h->min_lo) h->min_lo = lo;
    if (hi > h->max_hi) h->max_hi = hi;
    h->bloom64 |= mask;
    h->count++;
}

static inline uint64_t vka_regw_bloom_bit(uint64_t im_u, uint32_t lv, uint32_t ly) {
    uint64_t h = vka_vu_mix64(im_u ^ ((uint64_t)lv << 32) ^ (uint64_t)ly);
    return UINT64_C(1) << (unsigned)(h & UINT64_C(63));
}

enum { VKA_IVAR_CAP = 256u, VKA_IVAR_MASK = VKA_IVAR_CAP - 1u };

typedef struct {
    uint64_t key;
    uint32_t binding;
    uint16_t idx1, pad_;
} vka_inst_var_slot_t;

typedef struct {
    vka_inst_var_slot_t slots[VKA_IVAR_CAP];
    uint16_t inserted, pad_;
} vka_inst_var_map_t;

static inline uint32_t vka_ivar_hash(uint64_t key, uint32_t binding) {
    uint64_t h = vka_vu_mix64(key ^ ((uint64_t)binding * UINT64_C(0x9e3779b97f4a7c15)));
    return (uint32_t)h & VKA_IVAR_MASK;
}

static inline int vka_inst_var_map_find(const vka_inst_var_map_t *m, uint64_t key, uint32_t binding) {
    uint32_t slot = vka_ivar_hash(key, binding);
    int f5 = aoi_w51_fault_mode() == 5;
    for (uint32_t probe = 0u; probe < VKA_IVAR_CAP; probe++) {
        const vka_inst_var_slot_t *e = &m->slots[slot];
        if (!e->idx1) return -1;
        if (e->key == key && (f5 || e->binding == binding)) return (int)(e->idx1 - 1u);
        slot = (slot + 1u) & VKA_IVAR_MASK;
    }
    return -1;
}

static inline void vka_inst_var_map_put(vka_inst_var_map_t *m, uint64_t key,
                                        uint32_t binding, uint16_t idx1) {
    if (!idx1 || m->inserted >= VKA_IVAR_CAP - 32u) return;
    uint32_t slot = vka_ivar_hash(key, binding);
    for (uint32_t probe = 0u; probe < VKA_IVAR_CAP; probe++) {
        vka_inst_var_slot_t *e = &m->slots[slot];
        if (!e->idx1) {
            e->key = key; e->binding = binding; e->idx1 = idx1; m->inserted++;
            return;
        }
        if (e->key == key && e->binding == binding) { e->idx1 = idx1; return; }
        slot = (slot + 1u) & VKA_IVAR_MASK;
    }
}

typedef struct {
    uint32_t pair_mask[64];
    uint8_t slot_valid[32];
    uint8_t slot_pair[32];
} vka_pps_mask_t;

static inline void vka_pps_note_slot(vka_pps_mask_t *pm, unsigned slot, int pair) {
    if (slot >= 32u) return;
    if (pm->slot_valid[slot]) {
        uint8_t oldp = pm->slot_pair[slot];
        if (oldp < 64u) pm->pair_mask[oldp] &= ~(UINT32_C(1) << slot);
    }
    if (pair >= 0 && pair < 64) {
        pm->pair_mask[pair] |= UINT32_C(1) << slot;
        pm->slot_valid[slot] = 1u;
        pm->slot_pair[slot] = (uint8_t)pair;
    } else {
        pm->slot_valid[slot] = 0u;
    }
}

static inline uint32_t vka_pps_cand_mask(const vka_pps_mask_t *pm, int pair) {
    if (pair < 0 || pair >= 64) return ~UINT32_C(0);
    return pm->pair_mask[pair];
}
#endif
