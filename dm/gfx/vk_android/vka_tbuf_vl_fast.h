// Fast O(1) buffer-texture view/image maps, lock-free write filter, learned-format index & PFB twin map (Opt 54).
#ifndef VKA_TBUF_VL_FAST_H
#define VKA_TBUF_VL_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W52_TEST_HOOKS
extern _Atomic int g_vka_tbuf_vl_fast_override;
extern _Atomic int g_aoi_w52_fault;
static inline void vka_tbuf_vl_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_tbuf_vl_fast_override, v, memory_order_release);
}
static inline void aoi_w52_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w52_fault, f, memory_order_release);
}
static inline int aoi_w52_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w52_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W52_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w52_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w52_fault_mode(void) { return 0; }
#endif

static inline int vka_tbuf_vl_fast_on(void) {
#ifdef T_W52_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_tbuf_vl_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_TBUF_VL_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_tv_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

enum {
    VKA_TBVM_CAP = 512u,
    VKA_TBVM_MASK = VKA_TBVM_CAP - 1u,
    VKA_TBIM_CAP = 512u,
    VKA_TBIM_MASK = VKA_TBIM_CAP - 1u,
    VKA_TIMG_MAX_CHUNKS = 64u,
    VKA_VLI_CAP = 256u,
    VKA_VLI_MASK = VKA_VLI_CAP - 1u,
    VKA_PFBM_CAP = 1024u,
    VKA_PFBM_MASK = VKA_PFBM_CAP - 1u
};

_Static_assert((VKA_TBVM_CAP & VKA_TBVM_MASK) == 0u, "tbvm pow2");
_Static_assert((VKA_TBIM_CAP & VKA_TBIM_MASK) == 0u, "tbim pow2");
_Static_assert((VKA_VLI_CAP & VKA_VLI_MASK) == 0u, "vli pow2");
_Static_assert((VKA_PFBM_CAP & VKA_PFBM_MASK) == 0u, "pfbm pow2");

typedef struct {
    uint64_t buf_u, off, range;
    int32_t fmt;
    uint16_t slot;
    uint16_t pad_;
    uint32_t gen;
    uint32_t pad2_;
} vka_tbuf_vslot_t;

typedef struct {
    vka_tbuf_vslot_t slots[VKA_TBVM_CAP];
    uint32_t cur_gen, count;
} vka_tbuf_vmap_t;

static inline uint32_t vka_tbvm_hash(uint64_t buf_u, uint64_t off, uint64_t range, int32_t fmt) {
    uint64_t h = vka_tv_mix64(buf_u ^ vka_tv_mix64(off));
    h = vka_tv_mix64(h ^ range ^ ((uint64_t)(uint32_t)fmt << 17));
    return (uint32_t)h & VKA_TBVM_MASK;
}

static inline void vka_tbuf_vmap_clear(vka_tbuf_vmap_t *m) {
    m->cur_gen++;
    if (!m->cur_gen) {
        memset(m->slots, 0, sizeof m->slots);
        m->cur_gen = 1u;
    }
    m->count = 0u;
}

static inline int vka_tbuf_vmap_find(const vka_tbuf_vmap_t *m, uint64_t buf_u, uint64_t off,
                                     uint64_t range, int32_t fmt, uint16_t *slot_out) {
    if (!m->cur_gen || !m->count) return 0;
    uint32_t idx = vka_tbvm_hash(buf_u, off, range, fmt);
    for (uint32_t probe = 0u; probe < VKA_TBVM_CAP; probe++) {
        const vka_tbuf_vslot_t *e = &m->slots[idx];
        if (e->gen != m->cur_gen) return 0;
        if (e->buf_u == buf_u && e->off == off && e->range == range && e->fmt == fmt) {
            uint16_t s = e->slot;
            if (aoi_w52_fault_mode() == 1) s = (uint16_t)((s ^ 1u) & 255u);
            *slot_out = s;
            return 1;
        }
        idx = (idx + 1u) & VKA_TBVM_MASK;
    }
    return 0;
}

static inline void vka_tbuf_vmap_put(vka_tbuf_vmap_t *m, uint64_t buf_u, uint64_t off,
                                     uint64_t range, int32_t fmt, uint16_t slot) {
    if (!m->cur_gen) m->cur_gen = 1u;
    if (m->count >= VKA_TBVM_CAP - 16u) return;
    uint32_t idx = vka_tbvm_hash(buf_u, off, range, fmt);
    for (uint32_t probe = 0u; probe < VKA_TBVM_CAP; probe++) {
        vka_tbuf_vslot_t *e = &m->slots[idx];
        if (e->gen != m->cur_gen) {
            e->buf_u = buf_u; e->off = off; e->range = range;
            e->fmt = fmt; e->slot = slot; e->gen = m->cur_gen;
            m->count++;
            return;
        }
        if (e->buf_u == buf_u && e->off == off && e->range == range && e->fmt == fmt) {
            e->slot = slot;
            return;
        }
        idx = (idx + 1u) & VKA_TBVM_MASK;
    }
}

typedef struct {
    uint64_t addr;
    uint32_t elems;
    int32_t format;
    uint8_t is2d, pad_[3];
    uint16_t slot, pad2_;
    uint32_t gen;
} vka_tbuf_islot_t;

typedef struct {
    vka_tbuf_islot_t slots[VKA_TBIM_CAP];
    uint32_t cur_gen, count;
} vka_tbuf_imap_t;

static inline uint32_t vka_tbim_hash(uint64_t addr, uint32_t elems, int32_t format, uint8_t is2d) {
    uint64_t h = vka_tv_mix64(addr ^ ((uint64_t)elems << 32) ^ ((uint64_t)(uint32_t)format << 1) ^ is2d);
    return (uint32_t)h & VKA_TBIM_MASK;
}

static inline void vka_tbuf_imap_clear(vka_tbuf_imap_t *m) {
    m->cur_gen++;
    if (!m->cur_gen) {
        memset(m->slots, 0, sizeof m->slots);
        m->cur_gen = 1u;
    }
    m->count = 0u;
}

static inline int vka_tbuf_imap_find(const vka_tbuf_imap_t *m, uint64_t addr, uint32_t elems,
                                     int32_t format, uint8_t is2d, uint16_t *slot_out) {
    if (!m->cur_gen || !m->count) return 0;
    uint32_t idx = vka_tbim_hash(addr, elems, format, is2d);
    for (uint32_t probe = 0u; probe < VKA_TBIM_CAP; probe++) {
        const vka_tbuf_islot_t *e = &m->slots[idx];
        if (e->gen != m->cur_gen) return 0;
        if (e->addr == addr && e->elems == elems && e->format == format && e->is2d == is2d) {
            uint16_t s = e->slot;
            if (aoi_w52_fault_mode() == 3) s = (uint16_t)((s ^ 1u) & 255u);
            *slot_out = s;
            return 1;
        }
        idx = (idx + 1u) & VKA_TBIM_MASK;
    }
    return 0;
}

static inline void vka_tbuf_imap_put(vka_tbuf_imap_t *m, uint64_t addr, uint32_t elems,
                                     int32_t format, uint8_t is2d, uint16_t slot) {
    if (!m->cur_gen) m->cur_gen = 1u;
    if (m->count >= VKA_TBIM_CAP - 16u) return;
    uint32_t idx = vka_tbim_hash(addr, elems, format, is2d);
    for (uint32_t probe = 0u; probe < VKA_TBIM_CAP; probe++) {
        vka_tbuf_islot_t *e = &m->slots[idx];
        if (e->gen != m->cur_gen) {
            e->addr = addr; e->elems = elems; e->format = format;
            e->is2d = is2d; e->slot = slot; e->gen = m->cur_gen;
            m->count++;
            return;
        }
        if (e->addr == addr && e->elems == elems && e->format == format && e->is2d == is2d) {
            e->slot = slot;
            return;
        }
        idx = (idx + 1u) & VKA_TBIM_MASK;
    }
}

typedef struct {
    _Atomic uint32_t seq, active, huge, init;
    _Atomic uint64_t lo, hi, bloom[2];
} vka_timg_wfilter_t;

static inline uint32_t vka_timg_chunk_bit(uint64_t c) {
    return (uint32_t)((c * UINT64_C(0x9e3779b97f4a7c15)) >> 57) & 127u;
}

static inline void vka_timg_wf_clear(vka_timg_wfilter_t *wf) {
    atomic_fetch_add_explicit(&wf->seq, 1u, memory_order_release);
    atomic_store_explicit(&wf->active, 0u, memory_order_relaxed);
    atomic_store_explicit(&wf->huge, 0u, memory_order_relaxed);
    atomic_store_explicit(&wf->lo, UINT64_MAX, memory_order_relaxed);
    atomic_store_explicit(&wf->hi, 0u, memory_order_relaxed);
    atomic_store_explicit(&wf->bloom[0], 0u, memory_order_relaxed);
    atomic_store_explicit(&wf->bloom[1], 0u, memory_order_relaxed);
    atomic_store_explicit(&wf->init, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&wf->seq, 1u, memory_order_release);
}

static inline void vka_timg_wf_add(vka_timg_wfilter_t *wf, uint64_t addr, uint64_t bytes) {
    if (!bytes) return;
    uint64_t hi = addr > UINT64_MAX - bytes ? UINT64_MAX : addr + bytes;
    atomic_fetch_add_explicit(&wf->seq, 1u, memory_order_release);
    if (!atomic_load_explicit(&wf->init, memory_order_relaxed) ||
        !atomic_load_explicit(&wf->active, memory_order_relaxed)) {
        atomic_store_explicit(&wf->lo, addr, memory_order_relaxed);
        atomic_store_explicit(&wf->hi, hi, memory_order_relaxed);
        atomic_store_explicit(&wf->init, 1u, memory_order_relaxed);
    } else {
        uint64_t lo0 = atomic_load_explicit(&wf->lo, memory_order_relaxed);
        uint64_t hi0 = atomic_load_explicit(&wf->hi, memory_order_relaxed);
        if (addr < lo0) atomic_store_explicit(&wf->lo, addr, memory_order_relaxed);
        if (hi > hi0) atomic_store_explicit(&wf->hi, hi, memory_order_relaxed);
    }
    uint64_t c0 = addr >> 16, c1 = (hi - 1u) >> 16;
    if (c1 - c0 >= VKA_TIMG_MAX_CHUNKS) {
        atomic_store_explicit(&wf->huge, 1u, memory_order_relaxed);
    } else {
        for (uint64_t c = c0; c <= c1; c++) {
            uint32_t b = vka_timg_chunk_bit(c);
            atomic_fetch_or_explicit(&wf->bloom[b >> 6], UINT64_C(1) << (b & 63u), memory_order_relaxed);
        }
    }
    atomic_fetch_add_explicit(&wf->active, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&wf->seq, 1u, memory_order_release);
}

static inline int vka_timg_wf_may_overlap(const vka_timg_wfilter_t *wf, uint64_t addr, uint64_t n) {
    if (aoi_w52_fault_mode() == 2) return 0;
    if (!n) return 0;
    uint32_t s0 = atomic_load_explicit(&wf->seq, memory_order_acquire);
    if (s0 & 1u) return 1;
    if (!atomic_load_explicit(&wf->active, memory_order_relaxed)) return 0;
    uint64_t qhi = addr > UINT64_MAX - n ? UINT64_MAX : addr + n;
    uint64_t lo = atomic_load_explicit(&wf->lo, memory_order_relaxed);
    uint64_t hi = atomic_load_explicit(&wf->hi, memory_order_relaxed);
    int ov = 0;
    if (addr < hi && qhi > lo) {
        if (atomic_load_explicit(&wf->huge, memory_order_relaxed)) {
            ov = 1;
        } else {
            uint64_t c0 = addr >> 16, c1 = (qhi - 1u) >> 16;
            if (c1 - c0 >= VKA_TIMG_MAX_CHUNKS) {
                ov = 1;
            } else {
                for (uint64_t c = c0; c <= c1; c++) {
                    uint32_t b = vka_timg_chunk_bit(c);
                    uint64_t w = atomic_load_explicit(&wf->bloom[b >> 6], memory_order_relaxed);
                    if (w & (UINT64_C(1) << (b & 63u))) { ov = 1; break; }
                }
            }
        }
    }
    uint32_t s1 = atomic_load_explicit(&wf->seq, memory_order_acquire);
    if (s0 != s1) return 1;
    return ov;
}

typedef struct {
    int32_t a, b[4];
    uint32_t w, h;
    uint8_t pos[4], count, used, pad_[2];
} vka_vl_bucket_t;

typedef struct {
    vka_vl_bucket_t bkt[VKA_VLI_CAP];
    uint32_t count;
} vka_vl_index_t;

static inline uint32_t vka_vli_hash(int32_t a, uint32_t w, uint32_t h) {
    uint64_t x = vka_tv_mix64(((uint64_t)(uint32_t)a << 32) ^ ((uint64_t)w << 16) ^ h);
    return (uint32_t)x & VKA_VLI_MASK;
}

static inline const vka_vl_bucket_t *vka_vl_idx_bucket_find(const vka_vl_index_t *idx,
                                                             int32_t a, uint32_t w, uint32_t h) {
    if (!idx->count) return NULL;
    uint32_t slot = vka_vli_hash(a, w, h);
    for (uint32_t probe = 0u; probe < VKA_VLI_CAP; probe++) {
        const vka_vl_bucket_t *e = &idx->bkt[slot];
        if (!e->used) return NULL;
        if (e->a == a && e->w == w && e->h == h) return e;
        slot = (slot + 1u) & VKA_VLI_MASK;
    }
    return NULL;
}

static inline int vka_vl_idx_has_or_count(const vka_vl_index_t *idx, int32_t a, int32_t b,
                                          uint32_t w, uint32_t h, int *same_out) {
    const vka_vl_bucket_t *e = vka_vl_idx_bucket_find(idx, a, w, h);
    if (!e) { *same_out = 0; return 0; }
    *same_out = (int)e->count;
    for (uint8_t i = 0u; i < e->count && i < 4u; i++) {
        if (e->b[i] == b) return 1;
    }
    return 0;
}

static inline void vka_vl_idx_add(vka_vl_index_t *idx, int32_t a, int32_t b,
                                  uint32_t w, uint32_t h, uint8_t pos) {
    if (idx->count >= VKA_VLI_CAP - 16u) return;
    uint32_t slot = vka_vli_hash(a, w, h);
    for (uint32_t probe = 0u; probe < VKA_VLI_CAP; probe++) {
        vka_vl_bucket_t *e = &idx->bkt[slot];
        if (!e->used) {
            e->a = a; e->w = w; e->h = h; e->used = 1u;
            e->b[0] = b; e->pos[0] = pos; e->count = 1u;
            idx->count++;
            return;
        }
        if (e->a == a && e->w == w && e->h == h) {
            if (e->count < 4u) { e->b[e->count] = b; e->pos[e->count] = pos; e->count++; }
            return;
        }
        slot = (slot + 1u) & VKA_VLI_MASK;
    }
}

static inline void vka_vl_emit_dedup(int32_t fmt, int32_t *out_b, int *n, int max_out) {
    if (*n >= max_out) return;
    for (int k = 0; k < *n; k++) {
        if (out_b[k] == fmt) return;
    }
    out_b[(*n)++] = fmt;
}

static inline int vka_vl_idx_formats(const vka_vl_index_t *idx, int32_t a, uint32_t w,
                                     uint32_t h, int img, int32_t *out_b, int max_out) {
    if (aoi_w52_fault_mode() == 4 || max_out <= 0) return 0;
    const vka_vl_bucket_t *e0 = vka_vl_idx_bucket_find(idx, a, w, h);
    const vka_vl_bucket_t *e1 = (img && (w || h)) ? vka_vl_idx_bucket_find(idx, a, 0u, 0u) : NULL;
    if (!e0 && !e1) return 0;
    int n = 0;
    uint8_t i0 = 0u, i1 = 0u, n0 = e0 ? e0->count : 0u, n1 = e1 ? e1->count : 0u;
    for (uint8_t step = 0u; step < 8u && n < max_out && (i0 < n0 || i1 < n1); step++) {
        int take0 = (i1 >= n1) || (i0 < n0 && e0->pos[i0] <= e1->pos[i1]);
        int32_t fmt = take0 ? e0->b[i0++] : e1->b[i1++];
        vka_vl_emit_dedup(fmt, out_b, &n, max_out);
    }
    return n;
}

typedef struct {
    uint64_t early_u, late_u;
    uint32_t idx;
    int32_t kind;
    uint8_t used, pad_[7];
} vka_pfb_slot_t;

typedef struct {
    vka_pfb_slot_t slots[VKA_PFBM_CAP];
    uint32_t count;
} vka_pfb_map_t;

static inline uint32_t vka_pfb_slot_hash(uint64_t early_u) {
    return (uint32_t)vka_tv_mix64(early_u) & VKA_PFBM_MASK;
}

static inline int vka_pfb_map_find(const vka_pfb_map_t *m, uint64_t early_u,
                                   uint64_t *late_out, int *kind_out) {
    if (!m->count) return 0;
    uint32_t idx = vka_pfb_slot_hash(early_u);
    for (uint32_t probe = 0u; probe < VKA_PFBM_CAP; probe++) {
        const vka_pfb_slot_t *e = &m->slots[idx];
        if (!e->used) return 0;
        if (e->early_u == early_u) {
            int f5 = aoi_w52_fault_mode() == 5;
            *late_out = f5 ? 0u : e->late_u;
            *kind_out = f5 ? 0 : (int)e->kind;
            return 1;
        }
        idx = (idx + 1u) & VKA_PFBM_MASK;
    }
    return 0;
}

static inline void vka_pfb_map_put(vka_pfb_map_t *m, uint64_t early_u,
                                   uint64_t late_u, int kind, uint32_t arr_idx) {
    if (m->count >= VKA_PFBM_CAP - 32u) return;
    uint32_t idx = vka_pfb_slot_hash(early_u);
    for (uint32_t probe = 0u; probe < VKA_PFBM_CAP; probe++) {
        vka_pfb_slot_t *e = &m->slots[idx];
        if (!e->used) {
            e->early_u = early_u; e->late_u = late_u; e->idx = arr_idx;
            e->kind = (int32_t)kind; e->used = 1u;
            m->count++;
            return;
        }
        if (e->early_u == early_u) {
            e->late_u = late_u; e->idx = arr_idx; e->kind = (int32_t)kind;
            return;
        }
        idx = (idx + 1u) & VKA_PFBM_MASK;
    }
}

static inline void vka_pfb_map_set_idx(vka_pfb_map_t *m, uint64_t early_u, uint32_t arr_idx) {
    if (!m->count) return;
    uint32_t idx = vka_pfb_slot_hash(early_u);
    for (uint32_t probe = 0u; probe < VKA_PFBM_CAP; probe++) {
        vka_pfb_slot_t *e = &m->slots[idx];
        if (!e->used) return;
        if (e->early_u == early_u) { e->idx = arr_idx; return; }
        idx = (idx + 1u) & VKA_PFBM_MASK;
    }
}

static inline int vka_pfb_map_del(vka_pfb_map_t *m, uint64_t early_u,
                                  uint64_t *late_out, uint32_t *arr_idx_out) {
    if (!m->count) return 0;
    uint32_t cur = vka_pfb_slot_hash(early_u);
    int found = 0;
    for (uint32_t probe = 0u; probe < VKA_PFBM_CAP; probe++) {
        if (!m->slots[cur].used) return 0;
        if (m->slots[cur].early_u == early_u) {
            *late_out = m->slots[cur].late_u;
            *arr_idx_out = m->slots[cur].idx;
            found = 1;
            break;
        }
        cur = (cur + 1u) & VKA_PFBM_MASK;
    }
    if (!found) return 0;
    m->count--;
    uint32_t gap = cur;
    for (uint32_t step = 0u; step < VKA_PFBM_CAP; step++) {
        cur = (cur + 1u) & VKA_PFBM_MASK;
        if (!m->slots[cur].used) break;
        uint32_t home = vka_pfb_slot_hash(m->slots[cur].early_u);
        if (((cur - home) & VKA_PFBM_MASK) >= ((cur - gap) & VKA_PFBM_MASK)) {
            m->slots[gap] = m->slots[cur];
            gap = cur;
        }
    }
    m->slots[gap] = (vka_pfb_slot_t){0};
    return 1;
}
#endif
