#ifndef VKA_GW_FAST_H
#define VKA_GW_FAST_H
/* Wave 16 (Opt 18): Wait-free g_gw seqlock summary + chunk bloom, word-wide GW_FILL & UB 4 KiB bloom + 16x4 shadow memo (DM_VK_GW_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W16_TEST_HOOKS
extern int g_w16_fault;
#define VKA_GW_FAULT(n) (g_w16_fault == (n))
#else
#define VKA_GW_FAULT(n) 0
#endif

static inline int vka_gw_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_GW_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint32_t vka_gw_bit_idx(uint64_t p, uint32_t shift) {
    uint64_t x = (p ^ (p >> 7) ^ (p >> 17)) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> shift) & 63u;
}

static inline uint64_t vka_gw_bloom_mask(uint64_t lo, uint64_t hi) {
    if (hi < lo) return ~UINT64_C(0);
    if (hi == lo) return 0u;
    uint64_t p0 = lo >> 16, p1 = (hi - 1u) >> 16;
    if (VKA_GW_FAULT(1)) p1 = p0;
    if (p0 == p1) return UINT64_C(1) << vka_gw_bit_idx(p0, 58u);
    if (p1 - p0 >= 64u) return ~UINT64_C(0);
    uint64_t m = 0;
    for (uint64_t k = 0, n = p1 - p0 + 1u; k < n && k < 64u; k++)
        m |= UINT64_C(1) << vka_gw_bit_idx(p0 + k, 58u);
    return m;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint32_t count;
    _Atomic uint32_t haz_count;
    _Atomic uint64_t min_lo;
    _Atomic uint64_t max_hi;
    _Atomic uint64_t bloom;
    _Atomic uint64_t haz_lo;
    _Atomic uint64_t haz_hi;
    _Atomic uint64_t haz_bloom;
} vka_gw_summary;

static inline void vka_gw_sum_add(vka_gw_summary *s, uint64_t lo, uint64_t hi, int is_hazard) {
    assert(s != NULL && hi > lo);
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    uint32_t cnt = atomic_load_explicit(&s->count, memory_order_relaxed);
    uint64_t mlo = cnt ? atomic_load_explicit(&s->min_lo, memory_order_relaxed) : lo;
    uint64_t mhi = cnt ? atomic_load_explicit(&s->max_hi, memory_order_relaxed) : hi;
    uint64_t blm = atomic_load_explicit(&s->bloom, memory_order_relaxed) | vka_gw_bloom_mask(lo, hi);
    atomic_store_explicit(&s->min_lo, lo < mlo ? lo : mlo, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, hi > mhi ? hi : mhi, memory_order_relaxed);
    atomic_store_explicit(&s->bloom, blm, memory_order_relaxed);
    atomic_store_explicit(&s->count, cnt + 1u, memory_order_relaxed);
    if (is_hazard) {
        uint32_t hcnt = atomic_load_explicit(&s->haz_count, memory_order_relaxed);
        uint64_t hlo = hcnt ? atomic_load_explicit(&s->haz_lo, memory_order_relaxed) : lo;
        uint64_t hhi = hcnt ? atomic_load_explicit(&s->haz_hi, memory_order_relaxed) : hi;
        uint64_t hblm = atomic_load_explicit(&s->haz_bloom, memory_order_relaxed) | vka_gw_bloom_mask(lo, hi);
        atomic_store_explicit(&s->haz_lo, lo < hlo ? lo : hlo, memory_order_relaxed);
        atomic_store_explicit(&s->haz_hi, hi > hhi ? hi : hhi, memory_order_relaxed);
        atomic_store_explicit(&s->haz_bloom, hblm, memory_order_relaxed);
        atomic_store_explicit(&s->haz_count, hcnt + 1u, memory_order_relaxed);
    }
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline void vka_gw_sum_publish(vka_gw_summary *s, uint32_t cnt, uint64_t mlo,
                                      uint64_t mhi, uint64_t blm, uint32_t hcnt,
                                      uint64_t hlo, uint64_t hhi, uint64_t hblm) {
    assert(s != NULL);
    uint64_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    atomic_store_explicit(&s->min_lo, mlo, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, mhi, memory_order_relaxed);
    atomic_store_explicit(&s->bloom, blm, memory_order_relaxed);
    atomic_store_explicit(&s->count, cnt, memory_order_relaxed);
    atomic_store_explicit(&s->haz_lo, hlo, memory_order_relaxed);
    atomic_store_explicit(&s->haz_hi, hhi, memory_order_relaxed);
    atomic_store_explicit(&s->haz_bloom, hblm, memory_order_relaxed);
    atomic_store_explicit(&s->haz_count, hcnt, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
}

static inline int vka_gw_sum_hit_locked(const vka_gw_summary *s, uint64_t lo, uint64_t hi) {
    assert(s != NULL);
    if (hi < lo) return 1;
    if (hi == lo) return 0;
    uint32_t cnt = atomic_load_explicit(&s->count, memory_order_relaxed);
    if (!cnt) return 0;
    uint64_t mlo = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
    uint64_t mhi = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
    if (hi <= mlo || lo >= mhi) return 0;
    uint64_t blm = atomic_load_explicit(&s->bloom, memory_order_relaxed);
    return (blm & vka_gw_bloom_mask(lo, hi)) != 0u;
}

static inline int vka_gw_may_overlap(const vka_gw_summary *s, uint64_t lo, uint64_t hi) {
    assert(s != NULL);
    if (hi < lo) return 1;
    if (hi == lo) return 0;
    uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s1 & 1u) return 1;
    uint32_t cnt = atomic_load_explicit(&s->count, memory_order_relaxed);
    uint64_t mlo = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
    uint64_t mhi = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
    uint64_t blm = atomic_load_explicit(&s->bloom, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_relaxed);
    if (s1 != s2) return 1;
    if (!cnt || hi <= mlo || lo >= mhi) return 0;
    return (blm & vka_gw_bloom_mask(lo, hi)) != 0u;
}

static inline int vka_gw_may_hazard(const vka_gw_summary *s, uint64_t lo, uint64_t hi) {
    assert(s != NULL);
    if (hi < lo) return 1;
    if (hi == lo) return 0;
    uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s1 & 1u) return 1;
    uint32_t cnt = atomic_load_explicit(&s->haz_count, memory_order_relaxed);
    uint64_t mlo = atomic_load_explicit(&s->haz_lo, memory_order_relaxed);
    uint64_t mhi = atomic_load_explicit(&s->haz_hi, memory_order_relaxed);
    uint64_t blm = atomic_load_explicit(&s->haz_bloom, memory_order_relaxed);
    atomic_thread_fence(memory_order_acquire);
    uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_relaxed);
    if (s1 != s2) return 1;
    if (!cnt || hi <= mlo || lo >= mhi) return 0;
    return (blm & vka_gw_bloom_mask(lo, hi)) != 0u;
}

static inline void vka_gw_fill_bytes(uint8_t *out, uint64_t lo, uint64_t hi,
                                     uint64_t w_lo, uint32_t value) {
    assert(out != NULL && hi >= lo);
    size_t len = (size_t)(hi - lo);
    if (!len) return;
    uint8_t vb[4];
    memcpy(vb, &value, 4u);
    unsigned ph = VKA_GW_FAULT(2) ? 0u : (unsigned)((lo - w_lo) & 3u);
    uint8_t pat[8] = {
        vb[ph], vb[(ph + 1u) & 3u], vb[(ph + 2u) & 3u], vb[(ph + 3u) & 3u],
        vb[ph], vb[(ph + 1u) & 3u], vb[(ph + 2u) & 3u], vb[(ph + 3u) & 3u],
    };
    uint64_t w64 = 0;
    memcpy(&w64, pat, 8u);
    size_t n8 = len >> 3;
    for (size_t i = 0; i < n8; i++) memcpy(out + (i << 3), &w64, 8u);
    for (size_t t = n8 << 3; t < len; t++) out[t] = pat[t & 3u];
}

static inline uint64_t vka_ub_bloom_mask(uint64_t lo, uint64_t hi) {
    if (hi < lo) return ~UINT64_C(0);
    if (hi == lo) return 0u;
    uint64_t p0 = lo >> 12, p1 = (hi - 1u) >> 12;
    if (p0 == p1) return UINT64_C(1) << vka_gw_bit_idx(p0, 58u);
    if (p1 - p0 >= 64u) return ~UINT64_C(0);
    uint64_t m = 0;
    for (uint64_t k = 0, n = p1 - p0 + 1u; k < n && k < 64u; k++)
        m |= UINT64_C(1) << vka_gw_bit_idx(p0 + k, 58u);
    return m;
}

enum { VKA_UBM_SETS = 16u, VKA_UBM_WAYS = 4u };

typedef struct {
    uint64_t addr, size, seq, buf_u, off;
} vka_ub_shadow_ent;

typedef struct {
    vka_ub_shadow_ent w[VKA_UBM_WAYS];
    uint8_t hand, pad[7];
} vka_ub_shadow_set;

typedef struct {
    uint64_t cur_seq;
    vka_ub_shadow_set s[VKA_UBM_SETS];
} vka_ub_shadow_memo4;

static inline uint32_t vka_ub_shadow_set_idx(uint64_t addr, uint64_t sz) {
    uint64_t x = ((addr >> 4) ^ (addr >> 15) ^ (sz >> 4)) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> 60) & (VKA_UBM_SETS - 1u);
}

static inline void vka_ub_shadow_reset(vka_ub_shadow_memo4 *m) {
    assert(m != NULL);
    m->cur_seq = 0;
}

static inline int vka_ub_shadow_lookup(const vka_ub_shadow_memo4 *m, uint64_t ub_seq,
                                       uint64_t addr, uint64_t sz,
                                       uint64_t *buf_out, uint64_t *off_out) {
    assert(m != NULL && buf_out != NULL && off_out != NULL && sz > 0);
    if (!VKA_GW_FAULT(3) && (!ub_seq || m->cur_seq != ub_seq)) return 0;
    const vka_ub_shadow_set *set = &m->s[vka_ub_shadow_set_idx(addr, sz)];
    for (uint32_t i = 0; i < VKA_UBM_WAYS; i++) {
        const vka_ub_shadow_ent *e = &set->w[i];
        if (!e->size || e->addr != addr || e->size != sz) continue;
        if (!VKA_GW_FAULT(3) && e->seq != ub_seq) continue;
        *buf_out = e->buf_u;
        *off_out = e->off;
        return 1;
    }
    return 0;
}

static inline void vka_ub_shadow_insert(vka_ub_shadow_memo4 *m, uint64_t ub_seq,
                                        uint64_t addr, uint64_t sz,
                                        uint64_t buf_u, uint64_t off) {
    assert(m != NULL && sz > 0 && ub_seq > 0);
    if (m->cur_seq != ub_seq) m->cur_seq = ub_seq;
    vka_ub_shadow_set *set = &m->s[vka_ub_shadow_set_idx(addr, sz)];
    for (uint32_t i = 0; i < VKA_UBM_WAYS; i++) {
        vka_ub_shadow_ent *e = &set->w[i];
        if (!e->size || e->seq != ub_seq || (e->addr == addr && e->size == sz)) {
            *e = (vka_ub_shadow_ent){addr, sz, ub_seq, buf_u, off};
            return;
        }
    }
    uint32_t v = set->hand & (VKA_UBM_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_UBM_WAYS - 1u));
    set->w[v] = (vka_ub_shadow_ent){addr, sz, ub_seq, buf_u, off};
}

#endif
