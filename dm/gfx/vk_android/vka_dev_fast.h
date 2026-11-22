// Fast paths for Vulkan device fence waiters, after_rec pool, gate ban table, upload-batch lookup, and auto-floor quickselect (Opt 46).
#pragma once
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W44_TEST_HOOKS
extern _Atomic int g_vka_dev_fast_override;
extern _Atomic int g_aoi_w44_fault;
static inline void vka_dev_fast_set_for_test(int v) { atomic_store_explicit(&g_vka_dev_fast_override, v, memory_order_release); }
static inline void aoi_w44_set_fault(int f) { atomic_store_explicit(&g_aoi_w44_fault, f, memory_order_release); }
static inline int aoi_w44_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w44_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W44_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w44_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w44_fault_mode(void) { return 0; }
#endif
static inline int vka_dev_fast_on(void) {
#ifdef T_W44_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_dev_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_DEV_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline void vka_waiter_enter(_Atomic uint32_t *w) { atomic_fetch_add_explicit(w, 1u, memory_order_relaxed); }
static inline void vka_waiter_leave(_Atomic uint32_t *w) { atomic_fetch_sub_explicit(w, 1u, memory_order_relaxed); }
static inline void vka_cond_broadcast_gated(pthread_cond_t *cv, _Atomic uint32_t *w, int fast) {
    if (!fast) { pthread_cond_broadcast(cv); return; }
    if (aoi_w44_fault_mode() == 1) return;
    if (atomic_load_explicit(w, memory_order_relaxed) != 0u) pthread_cond_broadcast(cv);
}
enum { VKA_AFTER_POOL_CAP = 256 };
typedef struct vka_after_node {
    struct vka_after_node *next;
    uint64_t fence;
    void (*fn)(void *);
    void *arg;
} vka_after_node_t;
typedef struct { _Atomic uint64_t used[4]; vka_after_node_t nodes[VKA_AFTER_POOL_CAP]; } vka_after_pool_t;
static inline vka_after_node_t *vka_after_pool_alloc(vka_after_pool_t *p) {
    if (aoi_w44_fault_mode() == 2) return &p->nodes[0];
    for (uint32_t tries = 0u; tries < 64u; tries++) {
        for (uint32_t w = 0u; w < 4u; w++) {
            uint64_t m = atomic_load_explicit(&p->used[w], memory_order_relaxed);
            if (m == UINT64_MAX) continue;
            uint32_t bit = (uint32_t)__builtin_ctzll(~m);
            uint64_t next = m | (UINT64_C(1) << bit);
            if (atomic_compare_exchange_weak_explicit(&p->used[w], &m, next,
                    memory_order_acquire, memory_order_relaxed)) return &p->nodes[w * 64u + bit];
        }
    }
    return (vka_after_node_t *)malloc(sizeof(vka_after_node_t));
}

static inline void vka_after_pool_free(vka_after_pool_t *p, vka_after_node_t *a) {
    uintptr_t base = (uintptr_t)&p->nodes[0], ptr = (uintptr_t)a;
    if (ptr >= base && (ptr - base) < (uintptr_t)sizeof(p->nodes)) {
        uint32_t idx = (uint32_t)((ptr - base) / sizeof(p->nodes[0]));
        atomic_fetch_and_explicit(&p->used[idx >> 6], ~(UINT64_C(1) << (idx & 63u)), memory_order_release);
        return;
    }
    free(a);
}
enum { VKA_GBAN_HASH_CAP = 64 };
typedef struct { _Atomic uintptr_t slots[VKA_GBAN_HASH_CAP]; } vka_gban_fast_t;
static inline uint32_t vka_gban_hash(uintptr_t k) {
    uint64_t x = (uint64_t)k;
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (uint32_t)((x ^ (x >> 31)) & (VKA_GBAN_HASH_CAP - 1u));
}
static inline void vka_gban_fast_add(vka_gban_fast_t *t, const volatile uint32_t *w) {
    uintptr_t k = (uintptr_t)w;
    if (!k) return;
    uint32_t h = vka_gban_hash(k);
    for (uint32_t s = 0u; s < VKA_GBAN_HASH_CAP; s++) {
        uint32_t idx = (h + s) & (VKA_GBAN_HASH_CAP - 1u);
        uintptr_t cur = atomic_load_explicit(&t->slots[idx], memory_order_acquire);
        if (cur == k) return;
        if (cur == 0u) {
            uintptr_t expected = 0u;
            if (atomic_compare_exchange_strong_explicit(&t->slots[idx], &expected, k,
                    memory_order_release, memory_order_acquire) || expected == k) return;
        }
    }
}

static inline int vka_gban_fast_has(const vka_gban_fast_t *t, const volatile uint32_t *w) {
    uintptr_t k = (uintptr_t)w;
    if (!k) return 0;
    if (aoi_w44_fault_mode() == 3) return atomic_load_explicit(&t->slots[0], memory_order_acquire) == k;
    uint32_t h = vka_gban_hash(k);
    for (uint32_t s = 0u; s < VKA_GBAN_HASH_CAP; s++) {
        uintptr_t cur = atomic_load_explicit(&t->slots[(h + s) & (VKA_GBAN_HASH_CAP - 1u)], memory_order_acquire);
        if (cur == k) return 1;
        if (cur == 0u) return 0;
    }
    return 0;
}
enum { VKA_UB_MAX = 16 };
typedef struct { uint64_t handles[VKA_UB_MAX]; uint64_t lo, hi, bloom; uint32_t count, ready; } vka_ub_fast_t;
static inline uint32_t vka_ub_tag_bit(uint64_t x) { return (uint32_t)(((x * UINT64_C(0x9e3779b97f4a7c15)) >> 58) & 63u); }
static inline void vka_ub_fast_init(vka_ub_fast_t *u, const uint64_t *cbs, uint32_t n) {
    if (n > VKA_UB_MAX) n = VKA_UB_MAX;
    uint64_t lo = UINT64_MAX, hi = 0u, mask = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        uint64_t v = cbs[i]; u->handles[i] = v;
        if (v) { if (v < lo) lo = v; if (v > hi) hi = v; mask |= (UINT64_C(1) << vka_ub_tag_bit(v)); }
    }
    u->lo = lo; u->hi = hi; u->bloom = mask; u->count = n; u->ready = (n > 0u);
}
static inline int vka_ub_fast_is_batch(const vka_ub_fast_t *u, uint64_t cb) {
    if (!cb || !u->ready) return 0;
    if (aoi_w44_fault_mode() == 4) return (cb >= u->lo && cb <= u->hi) ? 1 : 0;
    if (cb < u->lo || cb > u->hi) return 0;
    if (!(u->bloom & (UINT64_C(1) << vka_ub_tag_bit(cb)))) return 0;
    uint32_t hit = 0u;
    for (uint32_t i = 0u; i < u->count; i++) hit |= (uint32_t)(u->handles[i] == cb);
    return hit != 0u;
}

static inline void vka_swap_u64(uint64_t *a, size_t i, size_t j) { uint64_t t = a[i]; a[i] = a[j]; a[j] = t; }
static inline size_t vka_partition_u64(uint64_t *a, size_t lo, size_t hi) {
    size_t mid = lo + ((hi - lo) >> 1);
    if (a[mid] < a[lo]) vka_swap_u64(a, lo, mid);
    if (a[hi] < a[lo]) vka_swap_u64(a, lo, hi);
    if (a[hi] < a[mid]) vka_swap_u64(a, mid, hi);
    vka_swap_u64(a, mid, hi);
    uint64_t pivot = a[hi]; size_t store = lo;
    for (size_t j = lo; j < hi; j++) if (a[j] <= pivot) { vka_swap_u64(a, store, j); store++; }
    vka_swap_u64(a, store, hi);
    return store;
}
static inline uint64_t vka_select_kth_u64(uint64_t *a, size_t n, size_t k) {
    if (n == 0u) return 0u;
    if (k >= n) k = n - 1u;
    if (aoi_w44_fault_mode() == 5 && k > 0u) k -= 1u;
    size_t lo = 0u, hi = n - 1u;
    for (size_t step = 0u; step < n && lo < hi; step++) {
        size_t p = vka_partition_u64(a, lo, hi);
        if (p == k) return a[k];
        if (p < k) lo = p + 1u; else hi = p - 1u;
    }
    return a[k];
}
