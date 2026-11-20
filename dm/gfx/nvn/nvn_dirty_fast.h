#ifndef DM_NVN_DIRTY_FAST_H
#define DM_NVN_DIRTY_FAST_H
/* Wave 6 (DM_DIRTY_FAST=1):
 *   1) Lock-free double-buffered Augmented Interval Index (g_rix[2][16384], 0 heap allocs)
 *      replacing O(N) live-pool scans across find_span(), on_fault(), and prep_write()
 *      while remaining exact for overlapping 4 KiB-aligned pools sharing 16 KiB host pages.
 *   2) 64-page superpage generation summary (cmax[]) + range r->latest early exit with
 *      GEN_UNPROT failure counter safety. */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { DIRTY_RIX_CAP = 16384, DIRTY_RIX_OUT = 64 };

typedef struct {
    uintptr_t base, end, max_end;
    int32_t slot, pad;
} dirty_rix_ent;

static dirty_rix_ent g_dirty_rix[2][DIRTY_RIX_CAP];
static _Atomic int g_dirty_rix_n[2], g_dirty_rix_cur, g_dirty_rix_ovf;
static _Atomic uint64_t g_dirty_rix_seq;
static _Atomic int g_dirty_fast_cached;

static inline int dirty_fast_on(void) {
    int v = atomic_load_explicit(&g_dirty_fast_cached, memory_order_relaxed);
    if (!v) {
        const char *e = getenv("DM_DIRTY_FAST");
        v = (e && !strcmp(e, "0")) ? -1 : 1;
        atomic_store_explicit(&g_dirty_fast_cached, v, memory_order_relaxed);
    }
    return v > 0;
}

static inline int dirty_w6_fault(void) {
#ifdef W6_TEST_HOOKS
    const char *e = getenv("T_W6_FAULT");
    return e ? atoi(e) : 0;
#else
    return 0;
#endif
}

static inline void dirty_max_up(_Atomic uint64_t *dst, uint64_t g) {
    uint64_t cur = atomic_load_explicit(dst, memory_order_relaxed);
    for (unsigned spin = 0; spin < 1024u && cur < g; spin++) {
        if (atomic_compare_exchange_strong_explicit(dst, &cur, g, memory_order_release, memory_order_acquire))
            break;
    }
}

static inline int dirty_rix_less(uintptr_t ba, uintptr_t ea, int32_t sa,
                                 uintptr_t bb, uintptr_t eb, int32_t sb) {
    if (ba != bb) return ba < bb;
    if (ea != eb) return ea < eb;
    return sa < sb;
}

/* Caller holds g_slot_mu. Insert live slot in sorted (base, end, slot) order with prefix max_end. */
static inline void dirty_rix_insert(int32_t slot, uintptr_t base, uintptr_t end) {
    if (atomic_load_explicit(&g_dirty_rix_ovf, memory_order_relaxed)) return;
    int cur = atomic_load_explicit(&g_dirty_rix_cur, memory_order_relaxed);
    int n = atomic_load_explicit(&g_dirty_rix_n[cur], memory_order_relaxed);
    if (n >= DIRTY_RIX_CAP) {
        atomic_store_explicit(&g_dirty_rix_ovf, 1, memory_order_release);
        return;
    }
    int next = !cur, m = 0, placed = 0;
    uintptr_t pmax = 0;
    atomic_fetch_add_explicit(&g_dirty_rix_seq, 1u, memory_order_acq_rel);
    for (int i = 0; i < n; i++) {
        dirty_rix_ent e = g_dirty_rix[cur][i];
        if (!placed && dirty_rix_less(base, end, slot, e.base, e.end, e.slot)) {
            if (end > pmax) pmax = end;
            g_dirty_rix[next][m++] = (dirty_rix_ent){base, end, pmax, slot, 0};
            placed = 1;
        }
        if (e.end > pmax) pmax = e.end;
        e.max_end = pmax;
        g_dirty_rix[next][m++] = e;
    }
    if (!placed) {
        if (end > pmax) pmax = end;
        g_dirty_rix[next][m++] = (dirty_rix_ent){base, end, pmax, slot, 0};
    }
    atomic_store_explicit(&g_dirty_rix_n[next], m, memory_order_release);
    atomic_store_explicit(&g_dirty_rix_cur, next, memory_order_release);
    atomic_fetch_add_explicit(&g_dirty_rix_seq, 1u, memory_order_release);
}

/* Caller holds g_slot_mu. Remove retired slot; reset overflow once live_after == 0. */
static inline void dirty_rix_remove(int32_t slot, int live_after) {
    if (atomic_load_explicit(&g_dirty_rix_ovf, memory_order_relaxed)) {
        if (live_after == 0) {
            int cur = atomic_load_explicit(&g_dirty_rix_cur, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_dirty_rix_seq, 1u, memory_order_acq_rel);
            atomic_store_explicit(&g_dirty_rix_n[!cur], 0, memory_order_release);
            atomic_store_explicit(&g_dirty_rix_cur, !cur, memory_order_release);
            atomic_store_explicit(&g_dirty_rix_ovf, 0, memory_order_release);
            atomic_fetch_add_explicit(&g_dirty_rix_seq, 1u, memory_order_release);
        }
        return;
    }
    int cur = atomic_load_explicit(&g_dirty_rix_cur, memory_order_relaxed);
    int n = atomic_load_explicit(&g_dirty_rix_n[cur], memory_order_relaxed);
    int next = !cur, m = 0;
    uintptr_t pmax = 0;
    atomic_fetch_add_explicit(&g_dirty_rix_seq, 1u, memory_order_acq_rel);
    for (int i = 0; i < n; i++) {
        dirty_rix_ent e = g_dirty_rix[cur][i];
        if (e.slot == slot) continue;
        if (e.end > pmax) pmax = e.end;
        e.max_end = pmax;
        g_dirty_rix[next][m++] = e;
    }
    atomic_store_explicit(&g_dirty_rix_n[next], m, memory_order_release);
    atomic_store_explicit(&g_dirty_rix_cur, next, memory_order_release);
    atomic_fetch_add_explicit(&g_dirty_rix_seq, 1u, memory_order_release);
}

/* Collect candidate slots overlapping [a, end_q) (where end > min_end). Returns count; sets *trusted=1 on stable index. */
static inline int dirty_rix_collect(uintptr_t a, uintptr_t end_q, uintptr_t min_end, int inclusive_end,
                                    int *out, int cap, int *trusted) {
    *trusted = 0;
    uint64_t seq0 = atomic_load_explicit(&g_dirty_rix_seq, memory_order_acquire);
    if ((seq0 & 1u) || atomic_load_explicit(&g_dirty_rix_ovf, memory_order_acquire)) return 0;
    int cur = atomic_load_explicit(&g_dirty_rix_cur, memory_order_acquire);
    int n = atomic_load_explicit(&g_dirty_rix_n[cur], memory_order_acquire);
    if (n < 0 || n > DIRTY_RIX_CAP) return 0;
    const dirty_rix_ent *ents = g_dirty_rix[cur];
    int lo = 0, hi = n;
    for (int step = 0; step < 16 && lo < hi; step++) {
        int mid = (lo + hi) >> 1;
        uintptr_t mb = __atomic_load_n(&ents[mid].base, __ATOMIC_RELAXED);
        if (end_q == 0u ? (mb <= a) : (mb < end_q)) lo = mid + 1;
        else hi = mid;
    }
    int count = 0, fault = dirty_w6_fault(), overflow = 0;
    for (int k = lo - 1; k >= 0; k--) {
        uintptr_t mend = __atomic_load_n(&ents[k].max_end, __ATOMIC_RELAXED);
        if (inclusive_end ? (mend < min_end) : (mend <= min_end)) break;
        uintptr_t e = __atomic_load_n(&ents[k].end, __ATOMIC_RELAXED);
        if (inclusive_end ? (e >= min_end) : (e > min_end)) {
            if (count < cap) out[count++] = __atomic_load_n(&ents[k].slot, __ATOMIC_RELAXED);
            else { overflow = 1; break; }
        }
        if (fault == 3) break;
    }
    atomic_thread_fence(memory_order_acquire);
    *trusted = !overflow && atomic_load_explicit(&g_dirty_rix_seq, memory_order_relaxed) == seq0;
    return count;
}

static inline int dirty_since_cmax(const _Atomic uint64_t *gen, const _Atomic uint64_t *cmax,
                                   uintptr_t p, uintptr_t q, uint64_t stamp) {
    for (uintptr_t i = p; i <= q;) {
        uintptr_t c = i >> 6;
        uintptr_t c_end = ((c + 1u) << 6) - 1u;
        if (c_end > q) c_end = q;
        if (atomic_load_explicit(&cmax[c], memory_order_acquire) > stamp) {
            for (; i <= c_end; i++) {
                if (atomic_load_explicit(&gen[i], memory_order_acquire) > stamp) return 1;
            }
        } else {
            i = c_end + 1u;
        }
    }
    return 0;
}

static inline int dirty_bounds_cmax(const _Atomic uint64_t *gen, const _Atomic uint64_t *cmax,
                                    uintptr_t p, uintptr_t q, uint64_t stamp,
                                    uintptr_t *f_out, uintptr_t *l_out) {
    uintptr_t f = UINTPTR_MAX, l = 0;
    for (uintptr_t i = p; i <= q && f == UINTPTR_MAX;) {
        uintptr_t c = i >> 6, c_end = ((c + 1u) << 6) - 1u;
        if (c_end > q) c_end = q;
        if (atomic_load_explicit(&cmax[c], memory_order_acquire) > stamp) {
            for (; i <= c_end; i++) {
                if (atomic_load_explicit(&gen[i], memory_order_acquire) > stamp) { f = i; break; }
            }
        } else {
            i = c_end + 1u;
        }
    }
    if (f == UINTPTR_MAX) return 0;
    l = f;
    for (uintptr_t j = q; j > f;) {
        uintptr_t c = j >> 6, c_lo = c << 6;
        if (c_lo < f) c_lo = f;
        if (atomic_load_explicit(&cmax[c], memory_order_acquire) > stamp) {
            for (uintptr_t k = j + 1u; k > c_lo; k--) {
                if (atomic_load_explicit(&gen[k - 1u], memory_order_acquire) > stamp) { l = k - 1u; j = f; break; }
            }
            if (j == f) break;
        }
        if (c_lo <= f) break;
        j = c_lo - 1u;
    }
    *f_out = f; *l_out = l;
    return 1;
}
#endif
