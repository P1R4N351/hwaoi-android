// Cached shader binding traits, min-fence gw gate, unsealed dedup gate,
// 64-bit quad IB fill, pool-drop bloom & shader-tuple pw_explain index (DM_VK_SPILL_GW_FAST=1).
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W57_TEST_HOOKS
extern _Atomic int g_vka_spill_gw_fast_override;
extern _Atomic int g_aoi_w57_fault;
static inline void vka_spill_gw_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_spill_gw_fast_override, v, memory_order_release);
}
static inline void aoi_w57_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w57_fault, f, memory_order_release);
}
static inline int aoi_w57_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w57_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W57_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w57_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w57_fault_mode(void) { return 0; }
#endif

static inline int vka_spill_gw_fast_on(void) {
#ifdef T_W57_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_spill_gw_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_SPILL_GW_FAST");
        v = (e && *e && atoi(e) == 0) ? 0 : 1;
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

/* 1. Cached shader binding traits (spill bytes + write flag). */
enum {
    VKA_SH_BF_READY = 1u,
    VKA_SH_BF_WRITES = 2u,
    VKA_SH_BF_SPILL = 4u,
};

typedef struct {
    uint32_t spill_bytes;
    uint8_t flags;
} vka_sh_btraits_t;

static inline void vka_sh_btraits_step(vka_sh_btraits_t *bt, int is_spill, uint32_t bytes, int writes) {
    if (writes) bt->flags |= VKA_SH_BF_WRITES;
    if (is_spill && aoi_w57_fault_mode() != 1 && !(bt->flags & VKA_SH_BF_SPILL)) {
        bt->flags |= VKA_SH_BF_SPILL;
        bt->spill_bytes = bytes;
    }
    bt->flags |= VKA_SH_BF_READY;
}

/* 2. O(1) minimum-fence gate for tex_gw_reupload_locked. */
static inline void vka_gw_min_fence_note(uint64_t *minf, uint64_t fence) {
    if (fence && fence < *minf) *minf = fence;
}

static inline int vka_gw_has_landed(uint64_t minf, uint64_t done) {
    if (aoi_w57_fault_mode() == 2) return 0;
    return minf != UINT64_MAX && minf <= done;
}

/* 3. O(1) unsealed counter & bounded dedup window for gw_add. */
static inline int vka_gw_can_dedup(uint32_t unsealed_cnt) {
    if (aoi_w57_fault_mode() == 3) return 0;
    return unsealed_cnt != 0u;
}

static inline int vka_gw_dedup_window(int ngw, uint32_t unsealed_cnt) {
    if (!unsealed_cnt || ngw <= 0) return ngw;
    int w = unsealed_cnt < 16u ? (int)unsealed_cnt : 16;
    return ngw > w ? ngw - w : 0;
}

/* 4a. 64-bit packed quad index buffer generator. */
static inline void vka_fill_quad_ib_u32(uint32_t *ix, uint32_t nquads) {
    uint32_t q = 0u;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    for (; q + 2u <= nquads; q += 2u) {
        uint32_t v0 = q * 4u, v1 = v0 + 4u;
        uint64_t pk[6] = {
            (uint64_t)v0 | ((uint64_t)(v0 + 1u) << 32),
            (uint64_t)(v0 + 2u) | ((uint64_t)v0 << 32),
            (uint64_t)(v0 + 2u) | ((uint64_t)(v0 + 3u) << 32),
            (uint64_t)v1 | ((uint64_t)(v1 + 1u) << 32),
            (uint64_t)(v1 + 2u) | ((uint64_t)v1 << 32),
            (uint64_t)(v1 + 2u) | ((uint64_t)(v1 + 3u) << 32),
        };
        memcpy(ix + (size_t)q * 6u, pk, sizeof pk);
    }
#endif
    for (; q < nquads; q++) {
        uint32_t v = q * 4u;
        ix[(size_t)q * 6u + 0u] = v;
        ix[(size_t)q * 6u + 1u] = v + 1u;
        ix[(size_t)q * 6u + 2u] = v + 2u;
        ix[(size_t)q * 6u + 3u] = v;
        ix[(size_t)q * 6u + 4u] = v + 2u;
        ix[(size_t)q * 6u + 5u] = v + 3u;
    }
    if (aoi_w57_fault_mode() == 4 && nquads > 0u) ix[0] ^= 1u;
}

/* 4b. 64-bit 2-probe pool-pointer bloom filter for vka_tex_drop_pool. */
typedef struct {
    _Atomic uint64_t bits;
} vka_pool_bloom_t;

static inline uint64_t vka_pool_bloom_bits(uintptr_t p) {
    if (!p) return UINT64_C(0);
    uint64_t h = (uint64_t)p * UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 30;
    return (UINT64_C(1) << ((h >> 58) & 63u)) | (UINT64_C(1) << ((h >> 52) & 63u));
}

static inline void vka_pool_bloom_add(vka_pool_bloom_t *b, uintptr_t p) {
    uint64_t m = vka_pool_bloom_bits(p);
    if (m) atomic_fetch_or_explicit(&b->bits, m, memory_order_release);
}

static inline void vka_pool_bloom_store(vka_pool_bloom_t *b, uint64_t m) {
    atomic_store_explicit(&b->bits, m, memory_order_release);
}

static inline int vka_pool_bloom_may(const vka_pool_bloom_t *b, uintptr_t p) {
    if (aoi_w57_fault_mode() == 4) return 0;
    uint64_t m = vka_pool_bloom_bits(p);
    return (atomic_load_explicit(&b->bits, memory_order_acquire) & m) == m;
}

/* 5. O(1) shader-tuple hash chain index for pw_explain. */
typedef struct {
    uint32_t *head;
    uint32_t *next;
    uint32_t *offs;
    uint32_t cap;
    uint32_t count;
    uint32_t ready;
} vka_pw_sh_idx_t;

static inline uint64_t vka_pw_sh_hash(uint8_t kind, const uint64_t key[5]) {
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)kind;
    for (int i = 0; i < 5; i++) {
        h = (h ^ key[i]) * UINT64_C(0xbf58476d1ce4e5b9);
        h ^= h >> 27;
    }
    return h ? h : UINT64_C(1);
}

static inline void vka_pw_sh_idx_free(vka_pw_sh_idx_t *ix) {
    free(ix->head);
    free(ix->next);
    free(ix->offs);
    ix->head = ix->next = ix->offs = NULL;
    ix->cap = ix->count = ix->ready = 0u;
}

static inline uint32_t vka_pw_sh_idx_count(const uint8_t *buf, size_t len, size_t rec_sz, size_t off_klen) {
    uint32_t n = 0u;
    for (size_t o = 0u; o + rec_sz <= len;) {
        uint32_t klen = 0u;
        memcpy(&klen, buf + o + off_klen, sizeof klen);
        size_t step = rec_sz + (((size_t)klen + 7u) & ~(size_t)7);
        o += step;
        n++;
    }
    return n;
}

static inline int vka_pw_sh_idx_build(vka_pw_sh_idx_t *ix, const uint8_t *buf, size_t len,
                                      size_t rec_sz, size_t off_klen) {
    vka_pw_sh_idx_free(ix);
    if (!buf || !len) { ix->ready = 1u; return 1; }
    uint32_t n = vka_pw_sh_idx_count(buf, len, rec_sz, off_klen);
    if (!n) { ix->ready = 1u; return 1; }
    uint32_t cap = 64u;
    while (cap < n * 2u && cap < (1u << 20)) cap <<= 1;
    ix->head = (uint32_t *)calloc(cap, sizeof(uint32_t));
    ix->next = (uint32_t *)calloc(n, sizeof(uint32_t));
    ix->offs = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    if (!ix->head || !ix->next || !ix->offs) { vka_pw_sh_idx_free(ix); return 0; }
    uint32_t i = 0u;
    for (size_t o = 0u; o + rec_sz <= len && i < n; i++) {
        uint32_t klen = 0u;
        memcpy(&klen, buf + o + off_klen, sizeof klen);
        ix->offs[i] = (uint32_t)o;
        o += rec_sz + (((size_t)klen + 7u) & ~(size_t)7);
    }
    uint32_t mask = cap - 1u;
    for (uint32_t r = i; r > 0u; r--) {
        uint32_t j = r - 1u;
        const uint8_t *rec = buf + ix->offs[j];
        uint8_t kind = rec[0];
        uint64_t key[5];
        memcpy(key, rec + 8u, sizeof key);
        uint32_t b = (uint32_t)vka_pw_sh_hash(kind, key) & mask;
        ix->next[j] = ix->head[b];
        ix->head[b] = j + 1u;
    }
    ix->cap = cap;
    ix->count = i;
    ix->ready = 1u;
    return 1;
}

static inline uint32_t vka_pw_sh_idx_first(const vka_pw_sh_idx_t *ix, uint8_t kind, const uint64_t key[5]) {
    if (!ix->ready || !ix->cap || !ix->head) return 0u;
    uint32_t b = (uint32_t)vka_pw_sh_hash(kind, key) & (ix->cap - 1u);
    if (aoi_w57_fault_mode() == 5) b ^= 1u;
    return ix->head[b];
}
