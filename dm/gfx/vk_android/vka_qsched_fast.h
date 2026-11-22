// Fast paths for NVN queue scheduler, fence window, event table, and granule bitmaps (Opt 45).
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W43_TEST_HOOKS
extern _Atomic int g_vka_qsched_fast_override;
extern _Atomic int g_aoi_w43_fault;
#endif

static inline int vka_qsched_fast_on(void) {
#ifdef T_W43_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_qsched_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
    const char *e = getenv("DM_VK_QSCHED_FAST");
    return !(e && strcmp(e, "0") == 0);
#else
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_QSCHED_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
#endif
}

static inline int aoi_w43_fault_mode(void) {
#ifdef T_W43_TEST_HOOKS
    int f = atomic_load_explicit(&g_aoi_w43_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W43_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w43_fault, v, memory_order_relaxed);
    return v;
#else
    return 0;
#endif
}

#ifdef T_W43_TEST_HOOKS
static inline void vka_qsched_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_qsched_fast_override, v, memory_order_release);
}
static inline void aoi_w43_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w43_fault, f, memory_order_release);
}
#endif

static inline void vka_qs_shr256(uint64_t m[4], uint32_t adv) {
    if (adv == 0u) return;
    if (adv >= 256u) {
        m[0] = m[1] = m[2] = m[3] = 0u;
        return;
    }
    uint32_t ws = adv >> 6, bs = adv & 63u;
    uint64_t tmp[4] = {0u, 0u, 0u, 0u};
    for (uint32_t i = 0u; i + ws < 4u; i++) {
        uint64_t lo_p = m[i + ws] >> bs;
        uint64_t hi_p = (bs != 0u && i + ws + 1u < 4u) ? (m[i + ws + 1u] << (64u - bs)) : 0u;
        tmp[i] = lo_p | hi_p;
    }
    memcpy(m, tmp, sizeof tmp);
}

static inline uint32_t vka_qs_ctz256(const uint64_t m[4]) {
    uint32_t adv = 0u;
    for (uint32_t w = 0u; w < 4u; w++) {
        if (m[w] == ~UINT64_C(0)) { adv += 64u; continue; }
        adv += (uint32_t)__builtin_ctzll(~m[w]);
        break;
    }
    return adv;
}

static inline int vka_qs_mark_test(const uint64_t m[4], uint64_t lo, uint64_t f) {
    if (f <= lo) return 1;
    uint64_t d = f - lo;
    if (d > 256u) return 0;
    uint32_t idx = (aoi_w43_fault_mode() == 1) ? ((uint32_t)d & 255u) : (uint32_t)(d - 1u);
    return (int)((m[idx >> 6] >> (idx & 63u)) & 1u);
}

static inline void vka_qs_ovf_add(uint64_t *ovf, int *novf_io, int ovf_cap, uint64_t f) {
    int n = *novf_io;
    for (int i = 0; i < n; i++) {
        if (ovf[i] == f) return;
    }
    if (n < ovf_cap) ovf[(*novf_io)++] = f;
}

static inline void vka_qs_ovf_promote(uint64_t m[4], uint64_t lo, uint64_t *ovf, int *novf_io) {
    int n = *novf_io, keep = 0;
    for (int i = 0; i < n; i++) {
        uint64_t v = ovf[i];
        if (v > lo && v - lo <= 256u) {
            uint32_t idx = (uint32_t)(v - lo - 1u);
            m[idx >> 6] |= UINT64_C(1) << (idx & 63u);
        } else if (v > lo) {
            ovf[keep++] = v;
        }
    }
    *novf_io = keep;
}

static inline void vka_qs_mark_set_and_drain(uint64_t m[4], uint64_t *lo_io,
                                             uint64_t *ovf, int *novf_io, int ovf_cap, uint64_t f) {
    uint64_t lo = *lo_io;
    if (f <= lo) return;
    uint64_t d = f - lo;
    if (d <= 256u) {
        uint32_t idx = (uint32_t)(d - 1u);
        m[idx >> 6] |= UINT64_C(1) << (idx & 63u);
    } else {
        vka_qs_ovf_add(ovf, novf_io, ovf_cap, f);
        return;
    }
    for (int pass = 0; pass <= 256 && (m[0] & 1u); pass++) {
        uint32_t adv = vka_qs_ctz256(m);
        if (adv == 0u) break;
        vka_qs_shr256(m, adv);
        lo += adv;
        if (*novf_io > 0) vka_qs_ovf_promote(m, lo, ovf, novf_io);
    }
    *lo_io = lo;
}

static inline int vka_qs_ev_can_skip_peak(uint32_t live_ex, int exact_bad, uint32_t evpeak) {
    if (aoi_w43_fault_mode() == 2 && evpeak > 0u) return 1;
    return !exact_bad && live_ex <= evpeak;
}

static inline void vka_qs_ev_note_signal(uint64_t f_tag[256], uint32_t f_cnt[256],
                                         uint32_t *live_ex, int *exact_bad,
                                         uint64_t trk_done, int existed, uint64_t old_f, uint64_t new_f) {
    if (aoi_w43_fault_mode() == 2 || *exact_bad) return;
    if (existed && old_f > trk_done) {
        uint32_t s = (uint32_t)(old_f & 255u);
        if (f_tag[s] != old_f || f_cnt[s] == 0u) { *exact_bad = 1; return; }
        f_cnt[s]--;
        if (f_cnt[s] == 0u) f_tag[s] = 0u;
        (*live_ex)--;
    }
    if (new_f > trk_done) {
        uint32_t s = (uint32_t)(new_f & 255u);
        if (f_cnt[s] != 0u && f_tag[s] != new_f) { *exact_bad = 1; return; }
        f_tag[s] = new_f;
        f_cnt[s]++;
        (*live_ex)++;
    }
}

static inline int vka_qs_ev_advance_done(uint64_t f_tag[256], uint32_t f_cnt[256],
                                         uint32_t *live_ex, int *exact_bad,
                                         uint64_t *trk_done, uint64_t d) {
    if (*exact_bad || d < *trk_done) return 0;
    uint64_t delta = d - *trk_done;
    if (delta > 256u) { *exact_bad = 1; return 0; }
    for (uint64_t step = 1u; step <= delta; step++) {
        uint64_t f = *trk_done + step;
        uint32_t s = (uint32_t)(f & 255u);
        if (f_cnt[s] != 0u) {
            if (f_tag[s] != f) { *exact_bad = 1; return 0; }
            *live_ex -= f_cnt[s];
            f_cnt[s] = 0u;
            f_tag[s] = 0u;
        }
    }
    *trk_done = d;
    return 1;
}

static inline void vka_qs_ev_resync(uint64_t f_tag[256], uint32_t f_cnt[256],
                                    uint32_t *live_ex, int *exact_bad, uint64_t *trk_done, uint64_t d,
                                    const void *ev_base, size_t stride, uint32_t cap,
                                    size_t off_w, size_t off_f) {
    memset(f_cnt, 0, 256u * sizeof f_cnt[0]);
    *trk_done = d; *live_ex = 0u; *exact_bad = 0;
    const uint8_t *base = (const uint8_t *)ev_base;
    for (uint32_t i = 0u; i < cap; i++) {
        const volatile uint32_t *w; uint64_t f;
        memcpy(&w, base + (size_t)i * stride + off_w, sizeof w);
        memcpy(&f, base + (size_t)i * stride + off_f, sizeof f);
        if (w && f > d) vka_qs_ev_note_signal(f_tag, f_cnt, live_ex, exact_bad, d, 0, 0u, f);
    }
}

static inline void vka_qs_park_set(uint16_t *mask, int qidx) {
    if (aoi_w43_fault_mode() == 3) return;
    if (qidx >= 0 && qidx < 16) *mask |= (uint16_t)(1u << (unsigned)qidx);
}

static inline void vka_qs_park_sync_clear(uint16_t *mask, int qidx, const void *head) {
    if (!head && qidx >= 0 && qidx < 16) *mask &= (uint16_t)~(1u << (unsigned)qidx);
}

static inline uint32_t vka_qs_gw_bucket(const volatile uint32_t *w) {
    uint64_t x = ((uint64_t)(uintptr_t)w >> 2) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(x >> 58) & 63u;
}

static inline void vka_qs_gw_rehash(uint8_t gw_hash[64], const void *words, size_t stride,
                                    int ngw, size_t off_word) {
    memset(gw_hash, 0, 64u);
    const uint8_t *base = (const uint8_t *)words;
    for (int i = 0; i < ngw; i++) {
        const volatile uint32_t *w;
        memcpy(&w, base + (size_t)i * stride + off_word, sizeof w);
        uint32_t b = vka_qs_gw_bucket(w);
        for (uint32_t step = 0u; step < 64u; step++) {
            uint32_t sl = (b + step) & 63u;
            if (!gw_hash[sl]) { gw_hash[sl] = (uint8_t)(i + 1); break; }
        }
    }
}

static inline int vka_gmap_all_set(const uint64_t *gmap, uint64_t k0, uint64_t k1) {
    if (k1 < k0) return 1;
    uint64_t w0 = k0 >> 6, w1 = k1 >> 6;
    uint64_t head = ~UINT64_C(0) << (k0 & 63u);
    uint64_t tail = ~UINT64_C(0) >> (63u - (k1 & 63u));
    if (w0 == w1) {
        uint64_t mask = head & tail;
        return (gmap[w0] & mask) == mask;
    }
    if ((gmap[w0] & head) != head) return 0;
    for (uint64_t w = w0 + 1u; w < w1; w++) {
        if (gmap[w] != ~UINT64_C(0)) return 0;
    }
    if (aoi_w43_fault_mode() == 5) return 1;
    return (gmap[w1] & tail) == tail;
}

static inline void vka_gmap_set_range(uint64_t *gmap, uint64_t k0, uint64_t k1) {
    if (k1 < k0) return;
    uint64_t w0 = k0 >> 6, w1 = k1 >> 6;
    uint64_t head = ~UINT64_C(0) << (k0 & 63u);
    uint64_t tail = ~UINT64_C(0) >> (63u - (k1 & 63u));
    if (w0 == w1) { gmap[w0] |= head & tail; return; }
    gmap[w0] |= head;
    for (uint64_t w = w0 + 1u; w < w1; w++) gmap[w] = ~UINT64_C(0);
    gmap[w1] |= tail;
}

static inline int vka_page_nonzero_4k(const void *page) {
    const uint64_t *w = (const uint64_t *)page;
    for (int i = 0; i < 512; i += 8) {
        if (w[i] | w[i + 1] | w[i + 2] | w[i + 3] | w[i + 4] | w[i + 5] | w[i + 6] | w[i + 7]) {
            return 1;
        }
    }
    return 0;
}
