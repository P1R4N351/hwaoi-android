#ifndef NVN_REG_FAST_H
#define NVN_REG_FAST_H
/* Wave 20 (Opt 22): Registry unchanged-write elision & lock-free fl/ft interval summaries (DM_VK_REG_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W20_TEST_HOOKS
extern int g_w20_fault;
#define NVN_REG_FAULT(n) (g_w20_fault == (n))
#else
#define NVN_REG_FAULT(n) 0
#endif

static inline int nvn_reg_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("DM_VK_REG_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

static inline int nvn_reg_eq_bytes(const void *a, const void *b, size_t n) {
    assert(n <= 256u);
    if (!n) return 1;
    assert(a != NULL && b != NULL);
    return memcmp(a, b, n) == 0;
}

static inline int nvn_treg_same(
    uint64_t cur_ser, uint64_t new_ser, uint64_t cur_pgen, uint64_t new_pgen,
    uint8_t cur_img, uint8_t new_img, uint8_t cur_hv, uint8_t new_hv,
    const void *cur_t, const void *new_t, size_t t_sz,
    const void *cur_v, const void *new_v, size_t v_sz) {
    if (!new_ser || cur_img != new_img) return 0;
    if (NVN_REG_FAULT(1)) return 1;
    if (cur_ser != new_ser || cur_pgen != new_pgen || cur_hv != new_hv) return 0;
    if (!nvn_reg_eq_bytes(cur_t, new_t, t_sz)) return 0;
    return !new_hv || nvn_reg_eq_bytes(cur_v, new_v, v_sz);
}

static inline int nvn_sreg_same(uint8_t was_ok, const void *cur_p, const void *new_p, size_t p_sz) {
    if (!was_ok) return 0;
    if (NVN_REG_FAULT(2)) return 1;
    return nvn_reg_eq_bytes(cur_p, new_p, p_sz);
}

enum { NVN_FL_SETS = 16u, NVN_FL_WAYS = 4u, NVN_FL_MAX_CHUNKS = 64u };

typedef struct {
    uint64_t mru_dseq;
    uintptr_t mru_lo, mru_hi;
    uint64_t dseq[NVN_FL_SETS][NVN_FL_WAYS];
    uintptr_t lo[NVN_FL_SETS][NVN_FL_WAYS];
    uintptr_t hi[NVN_FL_SETS][NVN_FL_WAYS];
    uint8_t ref[NVN_FL_SETS];
    uint8_t hand[NVN_FL_SETS];
} nvn_fl_cov_memo4;

static inline uint32_t nvn_fl_set_idx(uintptr_t a) {
    return (uint32_t)(((a >> 16) ^ (a >> 28)) & (NVN_FL_SETS - 1u));
}

static inline int nvn_fl_dseq_match(uint64_t stored, uint64_t dseq) {
    if (!stored) return 0;
    return NVN_REG_FAULT(3) ? 1 : (stored == dseq);
}

static inline int nvn_fl_cov_contains(const nvn_fl_cov_memo4 *m, uint64_t dseq, uintptr_t a, uintptr_t b) {
    if (a >= b) return 0;
    if (nvn_fl_dseq_match(m->mru_dseq, dseq) && m->mru_lo <= a && b <= m->mru_hi) return 1;
    uint32_t s = nvn_fl_set_idx(a);
    for (uint32_t w = 0; w < NVN_FL_WAYS; w++) {
        if (nvn_fl_dseq_match(m->dseq[s][w], dseq) && m->lo[s][w] <= a && b <= m->hi[s][w]) return 1;
    }
    return 0;
}

static inline int nvn_fl_cov_overlaps(const nvn_fl_cov_memo4 *m, uint64_t dseq, uintptr_t a, uintptr_t b) {
    if (a >= b) return 0;
    if (nvn_fl_dseq_match(m->mru_dseq, dseq) && m->mru_lo < b && a < m->mru_hi) return 1;
    uint32_t s = nvn_fl_set_idx(a);
    for (uint32_t w = 0; w < NVN_FL_WAYS; w++) {
        if (nvn_fl_dseq_match(m->dseq[s][w], dseq) && m->lo[s][w] < b && a < m->hi[s][w]) return 1;
    }
    return 0;
}

static inline void nvn_fl_cov_put(nvn_fl_cov_memo4 *m, uint64_t dseq, uintptr_t key_a, uintptr_t lo, uintptr_t hi) {
    assert(lo < hi);
    m->mru_dseq = dseq;
    m->mru_lo = lo;
    m->mru_hi = hi;
    uint32_t s = nvn_fl_set_idx(key_a);
    for (uint32_t w = 0; w < NVN_FL_WAYS; w++) {
        if (m->lo[s][w] == lo && m->hi[s][w] == hi && m->dseq[s][w] == dseq) {
            m->ref[s] |= (uint8_t)(1u << w);
            return;
        }
    }
    uint32_t h = m->hand[s] & 3u;
    for (uint32_t step = 0; step < NVN_FL_WAYS * 2u; step++) {
        uint8_t bit = (uint8_t)(1u << h);
        if (!m->dseq[s][h] || !(m->ref[s] & bit)) break;
        m->ref[s] &= (uint8_t)~bit;
        h = (h + 1u) & 3u;
    }
    m->dseq[s][h] = dseq;
    m->lo[s][h] = lo;
    m->hi[s][h] = hi;
    m->ref[s] |= (uint8_t)(1u << h);
    m->hand[s] = (uint8_t)((h + 1u) & 3u);
}

static inline uint32_t nvn_reg_chunk_bit(uint64_t ch) {
    return (uint32_t)((ch * UINT64_C(0x9e3779b97f4a7c15)) >> 56);
}

static inline void nvn_reg_bloom_mark_range(uintptr_t lo, uintptr_t hi, uint64_t w[4], uint32_t *huge) {
    if (lo >= hi) return;
    uint64_t c0 = (uint64_t)lo >> 16, c1 = (uint64_t)(hi - 1u) >> 16;
    if (c1 - c0 >= NVN_FL_MAX_CHUNKS) { (*huge)++; return; }
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t b = nvn_reg_chunk_bit(c);
        w[b >> 6] |= UINT64_C(1) << (b & 63u);
    }
}

static inline int nvn_reg_bloom_hit_range(const _Atomic uint64_t bl[4], uintptr_t lo, uintptr_t hi) {
    if (lo >= hi) return 0;
    uint64_t c0 = (uint64_t)lo >> 16, c1 = (uint64_t)(hi - 1u) >> 16;
    if (c1 - c0 >= NVN_FL_MAX_CHUNKS) return 1;
    for (uint64_t c = c0; c <= c1; c++) {
        uint32_t b = nvn_reg_chunk_bit(c);
        uint64_t w = atomic_load_explicit(&bl[b >> 6], memory_order_relaxed);
        if (w & (UINT64_C(1) << (b & 63u))) return 1;
    }
    return 0;
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic size_t nfl;
    _Atomic uintptr_t min_lo;
    _Atomic uintptr_t max_hi;
    _Atomic uint32_t huge_n;
    _Atomic uint64_t bloom[4];
} nvn_fl_summary;

static inline void nvn_fl_sum_wbegin(nvn_fl_summary *s) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    atomic_store_explicit(&s->nfl, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->min_lo, UINTPTR_MAX, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->huge_n, 0u, memory_order_relaxed);
    for (uint32_t i = 0; i < 4u; i++) atomic_store_explicit(&s->bloom[i], 0u, memory_order_relaxed);
}

static inline void nvn_fl_sum_wadd(nvn_fl_summary *s, uintptr_t lo, uintptr_t hi) {
    if (lo >= hi) return;
    uintptr_t ml = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
    uintptr_t mh = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
    if (lo < ml) atomic_store_explicit(&s->min_lo, lo, memory_order_relaxed);
    if (hi > mh) atomic_store_explicit(&s->max_hi, hi, memory_order_relaxed);
    uint64_t w[4] = {0, 0, 0, 0};
    uint32_t huge = 0;
    nvn_reg_bloom_mark_range(lo, hi, w, &huge);
    if (huge) atomic_fetch_add_explicit(&s->huge_n, huge, memory_order_relaxed);
    for (uint32_t i = 0; i < 4u; i++) {
        if (w[i]) atomic_fetch_or_explicit(&s->bloom[i], w[i], memory_order_relaxed);
    }
}

static inline void nvn_fl_sum_wend(nvn_fl_summary *s, size_t n) {
    atomic_store_explicit(&s->nfl, n, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
}

static inline void nvn_fl_sum_note_add(nvn_fl_summary *s, uintptr_t lo, uintptr_t hi, size_t n) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    nvn_fl_sum_wadd(s, lo, hi);
    nvn_fl_sum_wend(s, n);
}

static inline int nvn_fl_sum_disjoint(const nvn_fl_summary *s, uintptr_t lo, uintptr_t hi) {
    uint32_t s0 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s0 & 1u) return 0;
    size_t n = atomic_load_explicit(&s->nfl, memory_order_relaxed);
    if (n && NVN_REG_FAULT(4)) return 1;
    int disj = 0;
    if (!n) {
        disj = 1;
    } else {
        uintptr_t ml = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
        uintptr_t mh = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
        uint32_t huge = atomic_load_explicit(&s->huge_n, memory_order_relaxed);
        if (hi <= ml || lo >= mh) disj = 1;
        else if (!huge && !nvn_reg_bloom_hit_range(s->bloom, lo, hi)) disj = 1;
    }
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    return disj && s0 == s1;
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic size_t nft;
    _Atomic uintptr_t min_lo;
    _Atomic uintptr_t max_hi;
    _Atomic uint32_t huge_n;
    _Atomic uint64_t addr_bloom[4];
    _Atomic uint64_t ser_bloom[4];
} nvn_ft_summary;

static inline uint32_t nvn_ft_ser_bit(uint64_t serial) {
    uint64_t x = (serial ^ (serial >> 33)) * UINT64_C(0xff51afd7ed558ccd);
    return (uint32_t)((x ^ (x >> 33)) >> 56);
}

static inline void nvn_ft_sum_wbegin(nvn_ft_summary *s) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    atomic_store_explicit(&s->nft, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->min_lo, UINTPTR_MAX, memory_order_relaxed);
    atomic_store_explicit(&s->max_hi, 0u, memory_order_relaxed);
    atomic_store_explicit(&s->huge_n, 0u, memory_order_relaxed);
    for (uint32_t i = 0; i < 4u; i++) {
        atomic_store_explicit(&s->addr_bloom[i], 0u, memory_order_relaxed);
        atomic_store_explicit(&s->ser_bloom[i], 0u, memory_order_relaxed);
    }
}

static inline void nvn_ft_sum_witem(nvn_ft_summary *s, uintptr_t lo, uintptr_t hi, uint64_t serial) {
    if (lo < hi) {
        uintptr_t ml = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
        uintptr_t mh = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
        if (lo < ml) atomic_store_explicit(&s->min_lo, lo, memory_order_relaxed);
        if (hi > mh) atomic_store_explicit(&s->max_hi, hi, memory_order_relaxed);
        uint64_t w[4] = {0, 0, 0, 0};
        uint32_t huge = 0;
        nvn_reg_bloom_mark_range(lo, hi, w, &huge);
        if (huge) atomic_fetch_add_explicit(&s->huge_n, huge, memory_order_relaxed);
        for (uint32_t i = 0; i < 4u; i++) {
            if (w[i]) atomic_fetch_or_explicit(&s->addr_bloom[i], w[i], memory_order_relaxed);
        }
    }
    uint32_t sb = nvn_ft_ser_bit(serial);
    atomic_fetch_or_explicit(&s->ser_bloom[sb >> 6], UINT64_C(1) << (sb & 63u), memory_order_relaxed);
}

static inline void nvn_ft_sum_wend(nvn_ft_summary *s, size_t n) {
    atomic_store_explicit(&s->nft, n, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
}

static inline void nvn_ft_sum_add(nvn_ft_summary *s, uintptr_t lo, uintptr_t hi, uint64_t serial, size_t n) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    nvn_ft_sum_witem(s, lo, hi, serial);
    nvn_ft_sum_wend(s, n);
}

static inline void nvn_ft_sum_set_count(nvn_ft_summary *s, size_t n) {
    atomic_fetch_add_explicit(&s->seq, 1u, memory_order_release);
    nvn_ft_sum_wend(s, n);
}

static inline int nvn_ft_sum_empty(const nvn_ft_summary *s) {
    uint32_t s0 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s0 & 1u) return 0;
    size_t n = atomic_load_explicit(&s->nft, memory_order_relaxed);
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    return !n && s0 == s1;
}

static inline int nvn_ft_sum_no_serial(const nvn_ft_summary *s, uint64_t serial) {
    uint32_t s0 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s0 & 1u) return 0;
    size_t n = atomic_load_explicit(&s->nft, memory_order_relaxed);
    uint32_t sb = nvn_ft_ser_bit(serial);
    uint64_t w = atomic_load_explicit(&s->ser_bloom[sb >> 6], memory_order_relaxed);
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s0 != s1) return 0;
    return !n || !(w & (UINT64_C(1) << (sb & 63u)));
}

static inline int nvn_ft_sum_disjoint(const nvn_ft_summary *s, uintptr_t lo, uintptr_t hi) {
    uint32_t s0 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s0 & 1u) return 0;
    size_t n = atomic_load_explicit(&s->nft, memory_order_relaxed);
    int disj = 0;
    if (!n) {
        disj = 1;
    } else {
        uintptr_t ml = atomic_load_explicit(&s->min_lo, memory_order_relaxed);
        uintptr_t mh = atomic_load_explicit(&s->max_hi, memory_order_relaxed);
        uint32_t huge = atomic_load_explicit(&s->huge_n, memory_order_relaxed);
        if (hi <= ml || lo >= mh) disj = 1;
        else if (!huge && !nvn_reg_bloom_hit_range(s->addr_bloom, lo, hi)) disj = 1;
    }
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    return disj && s0 == s1;
}
#endif
