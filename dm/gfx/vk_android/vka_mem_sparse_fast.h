// Fast O(1) memory-pool id/mem index, pending-giveback & GW_COPY blooms, sparse-block type count & one-lane set (Opt 55).
#ifndef VKA_MEM_SPARSE_FAST_H
#define VKA_MEM_SPARSE_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W53_TEST_HOOKS
extern _Atomic int g_vka_mem_sparse_fast_override;
extern _Atomic int g_aoi_w53_fault;
static inline void vka_mem_sparse_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_mem_sparse_fast_override, v, memory_order_release);
}
static inline void aoi_w53_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w53_fault, f, memory_order_release);
}
static inline int aoi_w53_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w53_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W53_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w53_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w53_fault_mode(void) { return 0; }
#endif

static inline int vka_mem_sparse_fast_on(void) {
#ifdef T_W53_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_mem_sparse_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_MEM_SPARSE_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_ms_mix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t vka_ms_bloom_bits(uint64_t lo, uint64_t hi, unsigned shift) {
    if (hi <= lo) return 0u;
    uint64_t a = lo >> shift, b = (hi - 1u) >> shift;
    if (b >= a + 63u) return ~UINT64_C(0);
    uint64_t m = 0u;
    for (uint64_t p = a; p <= b; p++) m |= UINT64_C(1) << (p & 63u);
    return m;
}

/* 1. 512-slot generation-stamped dual pool index (id & first mem) + lock-free lazy-pool range filter. */
enum { VKA_MEM_PMAP_SLOTS = 512u, VKA_MEM_PMAP_MASK = 511u, VKA_MEM_PMAP_MAX = 256u };
typedef struct {
    uint64_t id_key[VKA_MEM_PMAP_SLOTS];
    uint16_t id_val[VKA_MEM_PMAP_SLOTS];
    uint32_t id_gen[VKA_MEM_PMAP_SLOTS];
    uint64_t mem_key[VKA_MEM_PMAP_SLOTS];
    uint16_t mem_val[VKA_MEM_PMAP_SLOTS];
    uint32_t mem_gen[VKA_MEM_PMAP_SLOTS];
    uint32_t gen;
    uint8_t overflow;
    _Atomic uint64_t lazy_seq;
    _Atomic uint64_t lazy_lo, lazy_hi, lazy_b64k, lazy_b4m;
    _Atomic uint32_t lazy_n;
} vka_mem_pool_map_t;

static inline void vka_mem_pool_begin(vka_mem_pool_map_t *m, uint32_t npool) {
    if (++m->gen == 0u) {
        memset(m->id_gen, 0, sizeof m->id_gen);
        memset(m->mem_gen, 0, sizeof m->mem_gen);
        m->gen = 1u;
    }
    m->overflow = npool > VKA_MEM_PMAP_MAX ? 1u : 0u;
}

static inline void vka_mem_pool_put(vka_mem_pool_map_t *m, uint64_t id, uint64_t mem, int idx) {
    if (m->overflow || idx < 0) return;
    uint32_t g = m->gen, s = (uint32_t)vka_ms_mix64(id) & VKA_MEM_PMAP_MASK;
    for (uint32_t p = 0u; p < VKA_MEM_PMAP_SLOTS; p++, s = (s + 1u) & VKA_MEM_PMAP_MASK) {
        if (m->id_gen[s] != g || m->id_key[s] == id) {
            m->id_key[s] = id; m->id_val[s] = (uint16_t)(idx + 1); m->id_gen[s] = g; break;
        }
    }
    s = (uint32_t)vka_ms_mix64(mem) & VKA_MEM_PMAP_MASK;
    for (uint32_t p = 0u; p < VKA_MEM_PMAP_SLOTS; p++, s = (s + 1u) & VKA_MEM_PMAP_MASK) {
        if (m->mem_gen[s] != g) {
            m->mem_key[s] = mem; m->mem_val[s] = (uint16_t)(idx + 1); m->mem_gen[s] = g; return;
        }
        if (m->mem_key[s] == mem) return; /* keep lowest idx on shared start */
    }
}

static inline int vka_mem_pool_find_id(const vka_mem_pool_map_t *m, uint64_t id) {
    if (m->overflow || !m->gen) return -1;
    uint32_t g = m->gen, s = (uint32_t)vka_ms_mix64(id) & VKA_MEM_PMAP_MASK;
    for (uint32_t p = 0u; p < VKA_MEM_PMAP_SLOTS; p++, s = (s + 1u) & VKA_MEM_PMAP_MASK) {
        if (m->id_gen[s] != g) break;
        if (m->id_key[s] == id) {
            int idx = (int)m->id_val[s] - 1;
            if (aoi_w53_fault_mode() == 1) idx ^= 1;
            return idx;
        }
    }
    return -1;
}

static inline int vka_mem_pool_find_mem(const vka_mem_pool_map_t *m, uint64_t mem) {
    if (m->overflow || !m->gen) return -1;
    uint32_t g = m->gen, s = (uint32_t)vka_ms_mix64(mem) & VKA_MEM_PMAP_MASK;
    for (uint32_t p = 0u; p < VKA_MEM_PMAP_SLOTS; p++, s = (s + 1u) & VKA_MEM_PMAP_MASK) {
        if (m->mem_gen[s] != g) return -1;
        if (m->mem_key[s] == mem) return (int)m->mem_val[s] - 1;
    }
    return -1;
}

static inline void vka_mem_lazy_publish(vka_mem_pool_map_t *m, uint32_t n, uint64_t lo,
                                        uint64_t hi, uint64_t b64k, uint64_t b4m) {
    uint64_t s = atomic_load_explicit(&m->lazy_seq, memory_order_relaxed);
    atomic_store_explicit(&m->lazy_seq, s + 1u, memory_order_release);
    atomic_store_explicit(&m->lazy_lo, lo, memory_order_relaxed);
    atomic_store_explicit(&m->lazy_hi, hi, memory_order_relaxed);
    atomic_store_explicit(&m->lazy_b64k, b64k, memory_order_relaxed);
    atomic_store_explicit(&m->lazy_b4m, b4m, memory_order_relaxed);
    atomic_store_explicit(&m->lazy_n, n, memory_order_relaxed);
    atomic_store_explicit(&m->lazy_seq, s + 2u, memory_order_release);
}

static inline int vka_mem_lazy_may_overlap(const vka_mem_pool_map_t *m, uint64_t lo, uint64_t hi) {
    if (hi <= lo) return 0;
    for (unsigned tr = 0u; tr < 16u; tr++) {
        uint64_t s0 = atomic_load_explicit(&m->lazy_seq, memory_order_acquire);
        if (s0 & 1u) continue;
        uint32_t n = atomic_load_explicit(&m->lazy_n, memory_order_relaxed);
        uint64_t mlo = atomic_load_explicit(&m->lazy_lo, memory_order_relaxed);
        uint64_t mhi = atomic_load_explicit(&m->lazy_hi, memory_order_relaxed);
        uint64_t b64 = atomic_load_explicit(&m->lazy_b64k, memory_order_relaxed);
        uint64_t b4m = atomic_load_explicit(&m->lazy_b4m, memory_order_relaxed);
        if (atomic_load_explicit(&m->lazy_seq, memory_order_acquire) != s0) continue;
        if (!n) return 0;
        if (hi <= mlo || lo >= mhi) return 0;
        if (!(vka_ms_bloom_bits(lo, hi, 16u) & b64)) return 0;
        if (!(vka_ms_bloom_bits(lo, hi, 22u) & b4m)) return 0;
        return 1;
    }
    return 1;
}

/* 2. Lock-free pending-giveback seqlock bloom + 64-slot join-queue dedup set. */
typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t lo, hi, b64k, b1m;
    _Atomic uint32_t count;
} vka_pend_filter_t;

static inline void vka_pend_publish_ranges(vka_pend_filter_t *f, const uintptr_t *lo,
                                           const uintptr_t *hi, size_t stride_uptr, int n) {
    uint64_t mlo = UINT64_MAX, mhi = 0u, b64 = 0u, b1m = 0u;
    for (int i = 0; i < n; i++) {
        uint64_t a = (uint64_t)lo[i * (int)stride_uptr];
        uint64_t b = (uint64_t)hi[i * (int)stride_uptr];
        if (b <= a) continue;
        if (a < mlo) mlo = a;
        if (b > mhi) mhi = b;
        b64 |= vka_ms_bloom_bits(a, b, 16u);
        b1m |= vka_ms_bloom_bits(a, b, 20u);
    }
    uint64_t s = atomic_load_explicit(&f->seq, memory_order_relaxed);
    atomic_store_explicit(&f->seq, s + 1u, memory_order_release);
    atomic_store_explicit(&f->lo, mlo, memory_order_relaxed);
    atomic_store_explicit(&f->hi, mhi, memory_order_relaxed);
    atomic_store_explicit(&f->b64k, b64, memory_order_relaxed);
    atomic_store_explicit(&f->b1m, b1m, memory_order_relaxed);
    atomic_store_explicit(&f->count, n > 0 ? (uint32_t)n : 0u, memory_order_relaxed);
    atomic_store_explicit(&f->seq, s + 2u, memory_order_release);
}

static inline int vka_pend_may_overlap(const vka_pend_filter_t *f, uint64_t lo, uint64_t hi) {
    if (aoi_w53_fault_mode() == 2) return 0;
    if (hi <= lo) return 0;
    for (unsigned tr = 0u; tr < 16u; tr++) {
        uint64_t s0 = atomic_load_explicit(&f->seq, memory_order_acquire);
        if (s0 & 1u) continue;
        uint32_t n = atomic_load_explicit(&f->count, memory_order_relaxed);
        uint64_t mlo = atomic_load_explicit(&f->lo, memory_order_relaxed);
        uint64_t mhi = atomic_load_explicit(&f->hi, memory_order_relaxed);
        uint64_t b64 = atomic_load_explicit(&f->b64k, memory_order_relaxed);
        uint64_t b1m = atomic_load_explicit(&f->b1m, memory_order_relaxed);
        if (atomic_load_explicit(&f->seq, memory_order_acquire) != s0) continue;
        if (!n) return 0;
        if (hi <= mlo || lo >= mhi) return 0;
        if (!(vka_ms_bloom_bits(lo, hi, 16u) & b64)) return 0;
        if (!(vka_ms_bloom_bits(lo, hi, 20u) & b1m)) return 0;
        return 1;
    }
    return 1;
}

enum { VKA_JOIN_SET_SLOTS = 64u, VKA_JOIN_SET_MASK = 63u };
typedef struct {
    uint64_t id[VKA_JOIN_SET_SLOTS], a[VKA_JOIN_SET_SLOTS], e[VKA_JOIN_SET_SLOTS];
    uint32_t sgen[VKA_JOIN_SET_SLOTS];
    uint32_t gen;
} vka_join_set_t;

static inline void vka_join_set_clear(vka_join_set_t *js) {
    if (++js->gen == 0u) { memset(js->sgen, 0, sizeof js->sgen); js->gen = 1u; }
}

static inline uint32_t vka_join_hash(uint64_t id, uint64_t a, uint64_t e) {
    uint64_t h = vka_ms_mix64(id) ^ vka_ms_mix64(a + 1u) ^ vka_ms_mix64(e + 3u);
    return (uint32_t)h & VKA_JOIN_SET_MASK;
}

static inline int vka_join_set_has(const vka_join_set_t *js, uint64_t id, uint64_t a, uint64_t e) {
    if (!js->gen) return 0;
    uint32_t g = js->gen, s = vka_join_hash(id, a, e);
    for (uint32_t p = 0u; p < VKA_JOIN_SET_SLOTS; p++, s = (s + 1u) & VKA_JOIN_SET_MASK) {
        if (js->sgen[s] != g) return 0;
        if (js->id[s] == id && js->a[s] == a && js->e[s] == e) return 1;
    }
    return 0;
}

static inline void vka_join_set_put(vka_join_set_t *js, uint64_t id, uint64_t a, uint64_t e) {
    if (!js->gen) js->gen = 1u;
    uint32_t g = js->gen, s = vka_join_hash(id, a, e);
    for (uint32_t p = 0u; p < VKA_JOIN_SET_SLOTS; p++, s = (s + 1u) & VKA_JOIN_SET_MASK) {
        if (js->sgen[s] != g || (js->id[s] == id && js->a[s] == a && js->e[s] == e)) {
            js->id[s] = id; js->a[s] = a; js->e[s] = e; js->sgen[s] = g; return;
        }
    }
}

/* 3. Stack-local GW_COPY running prefix bloom (vka_tex.c). */
typedef struct {
    uint64_t lo, hi, b4k, b256k;
    uint32_t count;
} vka_gw_prefix_t;

static inline void vka_gw_pfx_add(vka_gw_prefix_t *p, uint64_t wlo, uint64_t whi) {
    if (whi <= wlo) return;
    if (!p->count) { p->lo = wlo; p->hi = whi; }
    else { if (wlo < p->lo) p->lo = wlo; if (whi > p->hi) p->hi = whi; }
    p->b4k |= vka_ms_bloom_bits(wlo, whi, 12u);
    p->b256k |= vka_ms_bloom_bits(wlo, whi, 18u);
    p->count++;
}

static inline int vka_gw_pfx_may_overlap(const vka_gw_prefix_t *p, uint64_t slo, uint64_t shi) {
    if (!p->count || shi <= p->lo || slo >= p->hi) return 0;
    if (aoi_w53_fault_mode() == 3) return 0;
    if (!(vka_ms_bloom_bits(slo, shi, 12u) & p->b4k)) return 0;
    if (!(vka_ms_bloom_bits(slo, shi, 18u) & p->b256k)) return 0;
    return 1;
}

/* 4. O(N) sparse-block memory-type room counter (vka_sparse.c). */
enum { VKA_SPARSE_TYPES = 32u };
typedef struct { uint32_t room[VKA_SPARSE_TYPES]; } vka_sparse_type_cnt_t;

static inline void vka_sparse_cnt_add(vka_sparse_type_cnt_t *c, uint32_t type) {
    if (type < VKA_SPARSE_TYPES) c->room[type]++;
}

static inline int vka_sparse_has_other(const vka_sparse_type_cnt_t *c, uint32_t type) {
    if (type >= VKA_SPARSE_TYPES) return -1;
    if (aoi_w53_fault_mode() == 4) return c->room[type] >= 1u ? 1 : 0;
    return c->room[type] > 1u ? 1 : 0;
}

static inline void vka_sparse_cnt_dec(vka_sparse_type_cnt_t *c, uint32_t type) {
    if (type < VKA_SPARSE_TYPES && c->room[type] > 0u) c->room[type]--;
}

/* 5. 256-slot one-lane subgroup key set (vka_shader.c). */
enum { VKA_ONE_SET_SLOTS = 256u, VKA_ONE_SET_MASK = 255u, VKA_ONE_SET_MAX = 128u };
typedef struct {
    uint64_t keys[VKA_ONE_SET_SLOTS];
    uint8_t mask[VKA_ONE_SET_SLOTS];
    uint8_t built, overflow;
} vka_one_lane_set_t;

static inline void vka_one_lane_insert(vka_one_lane_set_t *s, uint64_t key, uint8_t bit, uint32_t *n) {
    uint32_t slot = (uint32_t)vka_ms_mix64(key) & VKA_ONE_SET_MASK;
    for (uint32_t p = 0u; p < VKA_ONE_SET_SLOTS; p++, slot = (slot + 1u) & VKA_ONE_SET_MASK) {
        if (!s->mask[slot]) { s->keys[slot] = key; s->mask[slot] = bit; (*n)++; return; }
        if (s->keys[slot] == key) { s->mask[slot] |= bit; return; }
    }
}

static inline void vka_one_lane_build(vka_one_lane_set_t *s, const uint64_t *env, int nenv,
                                      const uint64_t *prof, int nprof) {
    memset(s, 0, sizeof *s);
    uint32_t total = (uint32_t)(nenv > 0 ? nenv : 0) + (uint32_t)(nprof > 0 ? nprof : 0);
    if (total > VKA_ONE_SET_MAX) { s->overflow = 1u; s->built = 1u; return; }
    uint32_t n = 0u;
    for (int i = 0; i < nenv; i++) if (env[i]) vka_one_lane_insert(s, env[i], 1u, &n);
    for (int i = 0; i < nprof; i++) if (prof[i]) vka_one_lane_insert(s, prof[i], 2u, &n);
    s->built = 1u;
}

static inline int vka_one_lane_lookup(const vka_one_lane_set_t *s, uint64_t key, int prof_on) {
    if (aoi_w53_fault_mode() == 5) return 0;
    if (!s->built || s->overflow) return -1;
    uint32_t slot = (uint32_t)vka_ms_mix64(key) & VKA_ONE_SET_MASK;
    uint8_t want = (uint8_t)(1u | (prof_on ? 2u : 0u));
    for (uint32_t p = 0u; p < VKA_ONE_SET_SLOTS; p++, slot = (slot + 1u) & VKA_ONE_SET_MASK) {
        if (!s->mask[slot]) return 0;
        if (s->keys[slot] == key) return (s->mask[slot] & want) != 0u;
    }
    return 0;
}
#endif
