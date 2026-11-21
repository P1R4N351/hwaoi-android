// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    DM_FPS_PCACHE_SLOTS = 16u
};

#ifdef T_W41_TEST_HOOKS
extern _Atomic int g_dm_fps_fast_state;
extern _Atomic int g_dm_w41_fault;
static _Atomic int g_dm_w41_env_fault;
static inline void dm_fps_fast_set_for_test(int forced) {
    int v = forced < 0 ? 0 : (forced ? 2 : 1);
    atomic_store_explicit(&g_dm_fps_fast_state, v, memory_order_release);
}
static inline void dm_fps_w41_set_fault(int m) {
    atomic_store_explicit(&g_dm_w41_fault, m, memory_order_release);
}
static inline int dm_fps_w41_fault(void) {
    int m = atomic_load_explicit(&g_dm_w41_fault, memory_order_acquire);
    if (m) return m;
    int c = atomic_load_explicit(&g_dm_w41_env_fault, memory_order_acquire);
    if (!c) {
        const char *e = getenv("AOI_W41_FAULT");
        int v = (e && *e) ? atoi(e) : 0;
        c = v > 0 ? v + 1 : 1;
        atomic_store_explicit(&g_dm_w41_env_fault, c, memory_order_release);
    }
    return c - 1;
}
#else
static _Atomic int g_dm_fps_fast_state;
static inline int dm_fps_w41_fault(void) { return 0; }
#endif

static inline int dm_fps_fast_on(void) {
    int s = atomic_load_explicit(&g_dm_fps_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_FPS_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_dm_fps_fast_state, s, memory_order_release);
    }
    return s == 2;
}

typedef struct {
    uint32_t seq;
    uint64_t pages[DM_FPS_PCACHE_SLOTS];
} dm_fps_pcache_tls;

typedef int (*dm_fps_slow_rd_fn)(uint64_t a, void *out, size_t n);
typedef int (*dm_fps_heap_ok_fn)(uint64_t a, size_t n);
typedef uint32_t (*dm_fps_map_seq_fn)(void);

static inline int dm_fps_rd_cached_page(
    uint64_t a, void *out, size_t n, uint32_t seq, int fault,
    dm_fps_pcache_tls *tls, dm_fps_map_seq_fn map_seq, dm_fps_slow_rd_fn slow_rd) {
    if (!tls || ((a ^ (a + n - 1u)) >> 12) != 0u) return slow_rd(a, out, n);
    uint64_t pg = a & ~UINT64_C(0xfff);
    unsigned slot = (unsigned)(((pg >> 12) ^ (pg >> 20)) & (DM_FPS_PCACHE_SLOTS - 1u));
    if (fault != 3 && tls->seq != seq) {
        memset(tls->pages, 0, sizeof tls->pages);
        tls->seq = seq;
    }
    if ((tls->pages[slot] == (pg | UINT64_C(1))) && (fault == 3 || ((seq & 1u) == 0u && tls->seq == seq))) {
        memcpy(out, (const void *)(uintptr_t)a, n);
        if (fault == 3 || (map_seq && map_seq() == seq)) return 1;
        return slow_rd(a, out, n);
    }
    if ((seq & 1u) != 0u || !seq) return slow_rd(a, out, n);
    if (!slow_rd(a, out, n)) return 0;
    if (map_seq && map_seq() == seq) tls->pages[slot] = pg | UINT64_C(1);
    return 1;
}

static inline int dm_fps_rd_fast(
    uint64_t a, void *out, size_t n, uint64_t mod_base, size_t mod_span,
    dm_fps_pcache_tls *tls, dm_fps_heap_ok_fn heap_ok,
    dm_fps_map_seq_fn map_seq, dm_fps_slow_rd_fn slow_rd) {
    if (!out || !slow_rd) return 0;
    if (a < 0x1000u) return 0;
    if (n == 0u) return 1;
    if (a > UINT64_MAX - n) return 0;
    if (!dm_fps_fast_on()) return slow_rd(a, out, n);
    int fault = dm_fps_w41_fault();
    if (fault == 1) {
        if (!slow_rd(a, out, n)) return 0;
        *(uint8_t *)out ^= 0x5au;
        return 1;
    }
    if (mod_span > 0u && a >= mod_base && n <= mod_span && a - mod_base <= mod_span - n) {
        if (fault == 2) { memset(out, 0, n); return 1; }
        memcpy(out, (const void *)(uintptr_t)a, n);
        return 1;
    }
    uint32_t seq = map_seq ? map_seq() : 0u;
    if (seq != 0u && (seq & 1u) == 0u && heap_ok && heap_ok(a, n)) {
        memcpy(out, (const void *)(uintptr_t)a, n);
        if (map_seq() == seq) return 1;
    }
    if (fault == 3 || (seq != 0u && (seq & 1u) == 0u))
        return dm_fps_rd_cached_page(a, out, n, seq, fault, tls, map_seq, slow_rd);
    return slow_rd(a, out, n);
}

static inline float drs_steps_nearest_fast(const float *e, uint32_t n, float r) {
    if (!e || !n) return 0.0f;
    if (dm_fps_w41_fault() == 4) return e[n - 1u];
    if (n == 1u) return e[0];
    int rising = (r > 0.0f) && (e[0] > 0.0f);
    uint32_t k = 0u;
    for (uint32_t i = 1u; i < n; i++) {
        if (!(e[i] > e[i - 1u])) { rising = 0; break; }
        if (e[i] <= r) k = i;
    }
    float inv_r = 1.0f / r;
    if (!rising) {
        float best = e[0], bd = fabsf(1.0f / e[0] - inv_r);
        for (uint32_t i = 1u; i < n; i++) {
            float d = fabsf(1.0f / e[i] - inv_r);
            if (d < bd) { bd = d; best = e[i]; }
        }
        return best;
    }
    if (e[0] >= r) return e[0];
    float best = e[k], bd = fabsf(1.0f / best - inv_r);
    if (k + 1u < n) {
        float hi = e[k + 1u], d1 = fabsf(1.0f / hi - inv_r);
        if (d1 < bd) best = hi;
    }
    return best;
}

static inline float drs_steps_snap_fast(const float *e, uint32_t n, float cap, float x) {
    if (!e || !n) return 0.0f;
    if (dm_fps_w41_fault() == 5) return e[0];
    float v = cap > x ? x : cap;
    uint32_t i = 0u;
    if (n > 1u && e[1] <= v) {
        i = 1u;
        while (i + 1u < n && e[i + 1u] <= v) i++;
    }
    return e[i];
}
