#ifndef NVN_HLE_FAST_H
#define NVN_HLE_FAST_H
/* Wave 19 (Opt 21): Lock-free NVN shader-pool skip, live-pool memo & hostseg summary (DM_NVN_HLE_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W19_TEST_HOOKS
extern int g_w19_fault;
#define NVN_HLE_FAULT(n) (g_w19_fault == (n))
#else
#define NVN_HLE_FAULT(n) 0
#endif

static inline int nvn_hle_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("DM_NVN_HLE_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum {
    NVN_SHP_CHUNK_SHIFT = 24u,
    NVN_SHP_BLOOM_BITS = 256u,
    NVN_SHP_NEG_SLOTS = 64u,
    NVN_PLIVE_M4_SETS = 32u,
    NVN_PLIVE_M4_WAYS = 4u,
    NVN_HSEG_CHUNK_SHIFT = 16u
};

typedef struct {
    _Atomic uint64_t min_lo;
    _Atomic uint64_t max_hi;
    _Atomic uint64_t bloom16m[4];
} nvn_shp_summary;

typedef struct {
    uint64_t lo;
    uint64_t hi;
} nvn_shp_neg_slot;

typedef struct {
    uint32_t seq;
    uint32_t pad_;
    uint64_t last_lo;
    uint64_t last_hi;
    nvn_shp_neg_slot slots[NVN_SHP_NEG_SLOTS];
} nvn_shp_neg_cache;

static inline void nvn_shp_mark_chunks(uint64_t w[4], uint64_t l, uint64_t h) {
    assert(w != NULL);
    if (h <= l) return;
    uint64_t c0 = l >> NVN_SHP_CHUNK_SHIFT;
    uint64_t c1 = (h - 1u) >> NVN_SHP_CHUNK_SHIFT;
    if (c1 - c0 >= (uint64_t)NVN_SHP_BLOOM_BITS) {
        w[0] = w[1] = w[2] = w[3] = UINT64_MAX;
        return;
    }
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t idx = (uint32_t)(c & (NVN_SHP_BLOOM_BITS - 1u));
        w[idx >> 6] |= 1ull << (idx & 63u);
    }
}

static inline void nvn_shp_summary_rebuild(nvn_shp_summary *sum, const _Atomic uint64_t *lo_arr,
                                           const _Atomic uint64_t *hi_arr, int n) {
    assert(sum != NULL && lo_arr != NULL && hi_arr != NULL);
    if (n < 0) n = 0;
    if (n > 1024) n = 1024;
    uint64_t min_lo = UINT64_MAX, max_hi = 0, w[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) {
        uint64_t l = atomic_load_explicit(&lo_arr[i], memory_order_relaxed);
        uint64_t h = atomic_load_explicit(&hi_arr[i], memory_order_relaxed);
        if (h <= l) continue;
        if (l < min_lo) min_lo = l;
        if (h > max_hi) max_hi = h;
        nvn_shp_mark_chunks(w, l, h);
    }
    atomic_store_explicit(&sum->min_lo, min_lo, memory_order_relaxed);
    atomic_store_explicit(&sum->max_hi, max_hi, memory_order_relaxed);
    for (int k = 0; k < 4; k++) atomic_store_explicit(&sum->bloom16m[k], w[k], memory_order_relaxed);
}

static inline uint32_t nvn_shp_slot_idx(uint64_t a) {
    return (uint32_t)(((a >> NVN_SHP_CHUNK_SHIFT) ^ (a >> (NVN_SHP_CHUNK_SHIFT + 6u))) &
                      (NVN_SHP_NEG_SLOTS - 1u));
}

static inline void nvn_shp_neg_put(nvn_shp_neg_cache *c, uint32_t even_seq,
                                   uint64_t gap_lo, uint64_t gap_hi, uint64_t a) {
    assert(c != NULL);
    if ((even_seq & 1u) || gap_lo >= gap_hi) return;
    if (c->seq != even_seq) {
        memset(c, 0, sizeof *c);
        c->seq = even_seq;
    }
    c->last_lo = gap_lo;
    c->last_hi = gap_hi;
    uint32_t idx = nvn_shp_slot_idx(a);
    c->slots[idx].lo = gap_lo;
    c->slots[idx].hi = gap_hi;
}

static inline int nvn_shp_neg_hit(const nvn_shp_neg_cache *neg, uint32_t seq, uint64_t a, uint64_t e) {
    if (!neg || neg->seq != seq) return 0;
    if (a >= neg->last_lo && e <= neg->last_hi) return 1;
    const nvn_shp_neg_slot *sl = &neg->slots[nvn_shp_slot_idx(a)];
    return (a >= sl->lo && e <= sl->hi);
}

static inline int nvn_shp_bloom_miss(const nvn_shp_summary *sum, uint64_t a, uint64_t e) {
    assert(sum != NULL && e > a);
    uint64_t c0 = a >> NVN_SHP_CHUNK_SHIFT;
    uint64_t c1 = (e - 1u) >> NVN_SHP_CHUNK_SHIFT;
    if (c1 - c0 >= 4u) return 0;
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t idx = (uint32_t)(c & (NVN_SHP_BLOOM_BITS - 1u));
        uint64_t word = atomic_load_explicit(&sum->bloom16m[idx >> 6], memory_order_relaxed);
        if ((word >> (idx & 63u)) & 1ull) return 0;
    }
    return 1;
}

static inline int nvn_shp_fast_out(const nvn_shp_summary *sum, const nvn_shp_neg_cache *neg,
                                    const _Atomic uint32_t *seq_p, uint64_t a, uint64_t n) {
    assert(sum != NULL && seq_p != NULL);
    if (NVN_HLE_FAULT(1)) return 1;
    uint64_t e = a + (n ? n : 1u);
    if (e < a) e = UINT64_MAX;
    uint32_t s = atomic_load_explicit(seq_p, memory_order_acquire);
    if (s & 1u) return 0;
    if (nvn_shp_neg_hit(neg, s, a, e)) return 1;
    uint64_t min_lo = atomic_load_explicit(&sum->min_lo, memory_order_relaxed);
    uint64_t max_hi = atomic_load_explicit(&sum->max_hi, memory_order_relaxed);
    int out = (e <= min_lo || a >= max_hi) ? 1 : nvn_shp_bloom_miss(sum, a, e);
    if (!out) return 0;
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(seq_p, memory_order_relaxed) == s;
}

typedef struct {
    _Atomic uint32_t writers;
    _Atomic uint64_t epoch;
} nvn_pl_sync;

static inline void nvn_pl_wbegin(nvn_pl_sync *s) {
    assert(s != NULL);
    atomic_fetch_add_explicit(&s->writers, 1u, memory_order_acq_rel);
}

static inline void nvn_pl_wend(nvn_pl_sync *s) {
    assert(s != NULL);
    atomic_fetch_add_explicit(&s->epoch, 1u, memory_order_acq_rel);
    atomic_fetch_sub_explicit(&s->writers, 1u, memory_order_release);
}

typedef struct {
    uintptr_t pool[NVN_PLIVE_M4_WAYS];
    uint64_t epoch[NVN_PLIVE_M4_WAYS];
    uint64_t gen[NVN_PLIVE_M4_WAYS];
    uint32_t flags[NVN_PLIVE_M4_WAYS];
    uint8_t sparse[NVN_PLIVE_M4_WAYS];
    uint8_t valid[NVN_PLIVE_M4_WAYS];
    uint8_t next_way;
    uint8_t pad_[7];
} nvn_plive_set;

typedef struct {
    nvn_plive_set sets[NVN_PLIVE_M4_SETS];
} nvn_plive_memo4;

static inline uint32_t nvn_plive_set_idx(uintptr_t p) {
    return (uint32_t)(((p >> 6) ^ (p >> 14)) & (NVN_PLIVE_M4_SETS - 1u));
}

static inline void nvn_pl_snap(const nvn_pl_sync *s, uint32_t *w_out, uint64_t *ep_out) {
    assert(s != NULL && w_out != NULL && ep_out != NULL);
    *w_out = atomic_load_explicit(&s->writers, memory_order_acquire);
    *ep_out = atomic_load_explicit(&s->epoch, memory_order_acquire);
}

static inline int nvn_plive_fast_get(const nvn_plive_memo4 *m, const nvn_pl_sync *s,
                                     uintptr_t pool_u, unsigned *out_flags,
                                     int *out_sparse, uint64_t *out_gen) {
    assert(m != NULL && s != NULL && out_gen != NULL);
    uint32_t w0 = 0;
    uint64_t e0 = 0;
    nvn_pl_snap(s, &w0, &e0);
    if (!NVN_HLE_FAULT(2) && w0 != 0u) return 0;
    const nvn_plive_set *st = &m->sets[nvn_plive_set_idx(pool_u)];
    uint64_t g = 0;
    unsigned fl = 0;
    int sp = 0, hit = 0;
    for (uint32_t w = 0; w < NVN_PLIVE_M4_WAYS; w++) {
        if (!st->valid[w] || st->pool[w] != pool_u) continue;
        if (!NVN_HLE_FAULT(2) && st->epoch[w] != e0) continue;
        g = st->gen[w];
        fl = (unsigned)st->flags[w];
        sp = (int)st->sparse[w];
        hit = 1;
        break;
    }
    if (!hit) return 0;
    if (!NVN_HLE_FAULT(2)) {
        uint32_t w1 = atomic_load_explicit(&s->writers, memory_order_acquire);
        uint64_t e1 = atomic_load_explicit(&s->epoch, memory_order_acquire);
        if (w1 != 0u || e1 != e0) return 0;
    }
    if (out_flags) *out_flags = fl;
    if (out_sparse) *out_sparse = sp;
    *out_gen = g;
    return 1;
}

static inline void nvn_plive_fast_put(nvn_plive_memo4 *m, const nvn_pl_sync *s,
                                      uint32_t w0, uint64_t e0, uintptr_t pool_u,
                                      uint64_t gen, unsigned flags, int sparse) {
    assert(m != NULL && s != NULL);
    if (!gen) return;
    if (!NVN_HLE_FAULT(2)) {
        if (w0 != 0u) return;
        uint32_t w1 = atomic_load_explicit(&s->writers, memory_order_acquire);
        uint64_t e1 = atomic_load_explicit(&s->epoch, memory_order_acquire);
        if (w1 != 0u || e1 != e0) return;
    }
    nvn_plive_set *st = &m->sets[nvn_plive_set_idx(pool_u)];
    uint32_t slot = st->next_way & (NVN_PLIVE_M4_WAYS - 1u);
    for (uint32_t w = 0; w < NVN_PLIVE_M4_WAYS; w++) {
        if (!st->valid[w] || st->pool[w] == pool_u) { slot = w; break; }
    }
    st->pool[slot] = pool_u;
    st->epoch[slot] = e0;
    st->gen[slot] = gen;
    st->flags[slot] = (uint32_t)flags;
    st->sparse[slot] = sparse ? 1u : 0u;
    st->valid[slot] = 1u;
    st->next_way = (uint8_t)((slot + 1u) & (NVN_PLIVE_M4_WAYS - 1u));
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t busy_n;
    _Atomic uintptr_t min_lo;
    _Atomic uintptr_t max_hi;
    _Atomic uint64_t bloom64k[4];
} nvn_hostseg_summary;

static inline void nvn_hostseg_mark_chunks(uint64_t w[4], uintptr_t lo, uintptr_t hi) {
    assert(w != NULL);
    if (hi <= lo) return;
    uintptr_t c0 = lo >> NVN_HSEG_CHUNK_SHIFT;
    uintptr_t c1 = (hi - 1u) >> NVN_HSEG_CHUNK_SHIFT;
    if (c1 - c0 >= 256u) {
        w[0] = w[1] = w[2] = w[3] = UINT64_MAX;
        return;
    }
    for (uintptr_t c = c0; c <= c1; c++) {
        uint32_t idx = (uint32_t)(c & 255u);
        w[idx >> 6] |= 1ull << (idx & 63u);
    }
}

static inline void nvn_hostseg_summary_publish(nvn_hostseg_summary *sum, uint32_t busy_n,
                                               uintptr_t min_lo, uintptr_t max_hi,
                                               const uint64_t w[4]) {
    assert(sum != NULL && w != NULL);
    uint32_t s = atomic_load_explicit(&sum->seq, memory_order_relaxed);
    atomic_store_explicit(&sum->seq, s + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&sum->busy_n, busy_n, memory_order_relaxed);
    atomic_store_explicit(&sum->min_lo, busy_n ? min_lo : UINTPTR_MAX, memory_order_relaxed);
    atomic_store_explicit(&sum->max_hi, busy_n ? max_hi : 0u, memory_order_relaxed);
    for (int k = 0; k < 4; k++) atomic_store_explicit(&sum->bloom64k[k], w[k], memory_order_relaxed);
    atomic_store_explicit(&sum->seq, s + 2u, memory_order_release);
}

static inline int nvn_hostseg_may_overlap(const nvn_hostseg_summary *sum, uintptr_t lo, uintptr_t hi) {
    assert(sum != NULL);
    if (hi <= lo || NVN_HLE_FAULT(3)) return 0;
    uint32_t s = atomic_load_explicit(&sum->seq, memory_order_acquire);
    if (s & 1u) return 1;
    uint32_t busy_n = atomic_load_explicit(&sum->busy_n, memory_order_relaxed);
    uintptr_t min_lo = atomic_load_explicit(&sum->min_lo, memory_order_relaxed);
    uintptr_t max_hi = atomic_load_explicit(&sum->max_hi, memory_order_relaxed);
    int disjoint = (busy_n == 0u || hi <= min_lo || lo >= max_hi);
    if (!disjoint) {
        uintptr_t c0 = lo >> NVN_HSEG_CHUNK_SHIFT;
        uintptr_t c1 = (hi - 1u) >> NVN_HSEG_CHUNK_SHIFT;
        if (c1 - c0 < 8u) {
            int hit = 0;
            for (uintptr_t c = c0; c <= c1; c++) {
                uint32_t idx = (uint32_t)(c & 255u);
                uint64_t word = atomic_load_explicit(&sum->bloom64k[idx >> 6], memory_order_relaxed);
                if ((word >> (idx & 63u)) & 1ull) { hit = 1; break; }
            }
            if (!hit) disjoint = 1;
        }
    }
    if (!disjoint) return 1;
    atomic_thread_fence(memory_order_acquire);
    return atomic_load_explicit(&sum->seq, memory_order_relaxed) != s;
}

#endif
