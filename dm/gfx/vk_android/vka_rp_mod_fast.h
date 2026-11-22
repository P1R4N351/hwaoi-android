// Fast O(1) render-pass & shader-variant lookup maps, single-alloc index reader & inst_fast guards (Opt 52).
#ifndef VKA_RP_MOD_FAST_H
#define VKA_RP_MOD_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W50_TEST_HOOKS
extern _Atomic int g_vka_rp_mod_fast_override;
extern _Atomic int g_aoi_w50_fault;
static inline void vka_rp_mod_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_rp_mod_fast_override, v, memory_order_release);
}
static inline void aoi_w50_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w50_fault, f, memory_order_release);
}
static inline int aoi_w50_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w50_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W50_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w50_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w50_fault_mode(void) { return 0; }
#endif

static inline int vka_rp_mod_fast_on(void) {
#ifdef T_W50_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_rp_mod_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_RP_MOD_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_rm_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t vka_rpkey_hash_fast(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0, w4 = 0, w5 = 0, w6 = 0;
    memcpy(&w0, b, 8); memcpy(&w1, b + 8u, 8); memcpy(&w2, b + 16u, 8);
    memcpy(&w3, b + 24u, 8); memcpy(&w4, b + 32u, 8); memcpy(&w5, b + 40u, 8);
    memcpy(&w6, b + 48u, 8);
    uint64_t h = vka_rm_mix64(w4 ^ UINT64_C(0x9e3779b97f4a7c15));
    h = vka_rm_mix64(h ^ w0 ^ w1);
    h = vka_rm_mix64(h ^ w2 ^ w3);
    h = vka_rm_mix64(h ^ w5 ^ w6);
    return h ? h : UINT64_C(1);
}

static inline int vka_rpkey_eq_fast(const void *a, const void *b) {
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0;
    uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    memcpy(&a4, pa + 32u, 8); memcpy(&b4, pb + 32u, 8);
    if (a4 != b4) return 0;
    memcpy(&a0, pa, 8); memcpy(&b0, pb, 8);
    if (a0 != b0) return 0;
    memcpy(&a1, pa + 8u, 8); memcpy(&b1, pb + 8u, 8);
    memcpy(&a2, pa + 16u, 8); memcpy(&b2, pb + 16u, 8);
    memcpy(&a3, pa + 24u, 8); memcpy(&b3, pb + 24u, 8);
    memcpy(&a5, pa + 40u, 8); memcpy(&b5, pb + 40u, 8);
    uint64_t diff = (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3) | (a5 ^ b5);
    if (aoi_w50_fault_mode() != 1) {
        memcpy(&a6, pa + 48u, 8); memcpy(&b6, pb + 48u, 8);
        diff |= (a6 ^ b6);
    }
    return diff == 0u;
}

enum { VKA_RP_MAP_CAP = 4096u, VKA_RP_MAP_MASK = VKA_RP_MAP_CAP - 1u };

typedef struct {
    uint16_t idx1[VKA_RP_MAP_CAP];
    uint64_t hashes[VKA_RP_MAP_CAP];
    uint32_t inserted;
    uint32_t mapped_end;
} vka_rp_map_t;

static inline int vka_rp_map_find(const vka_rp_map_t *m, const void *entries,
                                  size_t stride, const void *key_p, uint64_t h) {
    uint32_t slot = (uint32_t)(h & VKA_RP_MAP_MASK);
    for (uint32_t probe = 0u; probe < VKA_RP_MAP_CAP; probe++) {
        uint16_t idx1 = m->idx1[slot];
        if (!idx1) return -1;
        if (m->hashes[slot] == h) {
            const uint8_t *cand = (const uint8_t *)entries + (size_t)(idx1 - 1u) * stride;
            if (vka_rpkey_eq_fast(cand, key_p)) return (int)(idx1 - 1u);
        }
        slot = (slot + 1u) & VKA_RP_MAP_MASK;
    }
    return -1;
}

static inline void vka_rp_map_insert(vka_rp_map_t *m, uint64_t h, uint16_t idx1) {
    if (!idx1 || m->inserted >= VKA_RP_MAP_CAP - 64u) return;
    uint32_t slot = (uint32_t)(h & VKA_RP_MAP_MASK);
    for (uint32_t probe = 0u; probe < VKA_RP_MAP_CAP; probe++) {
        uint16_t cur = m->idx1[slot];
        if (!cur) {
            m->idx1[slot] = idx1;
            m->hashes[slot] = h;
            m->inserted++;
            if ((uint32_t)idx1 == m->mapped_end + 1u) m->mapped_end = (uint32_t)idx1;
            return;
        }
        if (cur == idx1) {
            if ((uint32_t)idx1 == m->mapped_end + 1u) m->mapped_end = (uint32_t)idx1;
            return;
        }
        slot = (slot + 1u) & VKA_RP_MAP_MASK;
    }
}

enum { VKA_MOD_MAP_CAP = 4096u, VKA_MOD_MAP_MASK = VKA_MOD_MAP_CAP - 1u };

typedef struct {
    uint64_t s_u, a, b, mod_u;
    uint32_t tag;
    uint8_t used;
} vka_mod_pair_slot_t;

typedef struct {
    vka_mod_pair_slot_t slots[VKA_MOD_MAP_CAP];
    uint32_t inserted;
    uint8_t partial;
} vka_mod_pair_map_t;

static inline uint32_t vka_mod_pair_hash(uint32_t tag, uint64_t s_u, uint64_t a, uint64_t b) {
    uint64_t h = vka_rm_mix64(s_u ^ ((uint64_t)tag << 32) ^ UINT64_C(0x9e3779b97f4a7c15));
    h = vka_rm_mix64(h ^ a);
    h = vka_rm_mix64(h ^ b);
    return (uint32_t)h & VKA_MOD_MAP_MASK;
}

static inline int vka_mod_pair_find(const vka_mod_pair_map_t *m, uint32_t tag,
                                    uint64_t s_u, uint64_t a, uint64_t b, uint64_t *mod_out) {
    uint32_t slot = vka_mod_pair_hash(tag, s_u, a, b);
    int fault2 = aoi_w50_fault_mode() == 2;
    for (uint32_t probe = 0u; probe < VKA_MOD_MAP_CAP; probe++) {
        const vka_mod_pair_slot_t *e = &m->slots[slot];
        if (!e->used) return 0;
        if (e->tag == tag && e->s_u == s_u && e->a == a && (fault2 || e->b == b)) {
            *mod_out = e->mod_u;
            return 1;
        }
        slot = (slot + 1u) & VKA_MOD_MAP_MASK;
    }
    return 0;
}

static inline void vka_mod_pair_put(vka_mod_pair_map_t *m, uint32_t tag,
                                    uint64_t s_u, uint64_t a, uint64_t b, uint64_t mod_u) {
    if (m->inserted >= VKA_MOD_MAP_CAP - 64u) { m->partial = 1u; return; }
    uint32_t slot = vka_mod_pair_hash(tag, s_u, a, b);
    for (uint32_t probe = 0u; probe < VKA_MOD_MAP_CAP; probe++) {
        vka_mod_pair_slot_t *e = &m->slots[slot];
        if (!e->used) {
            e->tag = tag; e->s_u = s_u; e->a = a; e->b = b; e->mod_u = mod_u; e->used = 1u;
            m->inserted++;
            return;
        }
        if (e->tag == tag && e->s_u == s_u && e->a == a && e->b == b) {
            e->mod_u = mod_u;
            return;
        }
        slot = (slot + 1u) & VKA_MOD_MAP_MASK;
    }
    m->partial = 1u;
}

typedef struct {
    uint64_t s_u, w0, w1, w2, w3, w4, mod_u;
    uint8_t used;
} vka_mod_tex_slot_t;

typedef struct {
    vka_mod_tex_slot_t slots[VKA_MOD_MAP_CAP];
    uint32_t inserted;
    uint8_t partial;
} vka_mod_tex_map_t;

static inline uint32_t vka_texmod_key_hash_fast(uint64_t s_u, const void *k40) {
    const uint8_t *p = (const uint8_t *)k40;
    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0, w4 = 0;
    memcpy(&w0, p, 8); memcpy(&w1, p + 8u, 8); memcpy(&w2, p + 16u, 8);
    memcpy(&w3, p + 24u, 8); memcpy(&w4, p + 32u, 8);
    uint64_t h = vka_rm_mix64(s_u ^ w0);
    h = vka_rm_mix64(h ^ w1 ^ w2);
    h = vka_rm_mix64(h ^ w3 ^ w4);
    return (uint32_t)h & VKA_MOD_MAP_MASK;
}

static inline int vka_texmod_key_eq_fast(const void *a40, const void *b40) {
    const uint8_t *pa = (const uint8_t *)a40, *pb = (const uint8_t *)b40;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
    uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0;
    memcpy(&a0, pa, 8); memcpy(&b0, pb, 8);
    memcpy(&a1, pa + 8u, 8); memcpy(&b1, pb + 8u, 8);
    memcpy(&a2, pa + 16u, 8); memcpy(&b2, pb + 16u, 8);
    memcpy(&a3, pa + 24u, 8); memcpy(&b3, pb + 24u, 8);
    uint64_t d = (a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3);
    if (aoi_w50_fault_mode() != 3) {
        memcpy(&a4, pa + 32u, 8); memcpy(&b4, pb + 32u, 8);
        d |= (a4 ^ b4);
    }
    return d == 0u;
}

static inline int vka_mod_tex_find(const vka_mod_tex_map_t *m, uint64_t s_u,
                                   const void *k40, uint64_t *mod_out) {
    const uint8_t *p = (const uint8_t *)k40;
    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0, w4 = 0;
    memcpy(&w0, p, 8); memcpy(&w1, p + 8u, 8); memcpy(&w2, p + 16u, 8);
    memcpy(&w3, p + 24u, 8); memcpy(&w4, p + 32u, 8);
    uint32_t slot = vka_texmod_key_hash_fast(s_u, k40);
    int fault3 = aoi_w50_fault_mode() == 3;
    for (uint32_t probe = 0u; probe < VKA_MOD_MAP_CAP; probe++) {
        const vka_mod_tex_slot_t *e = &m->slots[slot];
        if (!e->used) return 0;
        if (e->s_u == s_u &&
            (((e->w0 ^ w0) | (e->w1 ^ w1) | (e->w2 ^ w2) | (e->w3 ^ w3) |
              (fault3 ? UINT64_C(0) : (e->w4 ^ w4))) == 0u)) {
            *mod_out = e->mod_u;
            return 1;
        }
        slot = (slot + 1u) & VKA_MOD_MAP_MASK;
    }
    return 0;
}

static inline void vka_mod_tex_put(vka_mod_tex_map_t *m, uint64_t s_u,
                                   const void *k40, uint64_t mod_u) {
    if (m->inserted >= VKA_MOD_MAP_CAP - 64u) { m->partial = 1u; return; }
    const uint8_t *p = (const uint8_t *)k40;
    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0, w4 = 0;
    memcpy(&w0, p, 8); memcpy(&w1, p + 8u, 8); memcpy(&w2, p + 16u, 8);
    memcpy(&w3, p + 24u, 8); memcpy(&w4, p + 32u, 8);
    uint32_t slot = vka_texmod_key_hash_fast(s_u, k40);
    for (uint32_t probe = 0u; probe < VKA_MOD_MAP_CAP; probe++) {
        vka_mod_tex_slot_t *e = &m->slots[slot];
        if (!e->used) {
            e->s_u = s_u; e->w0 = w0; e->w1 = w1; e->w2 = w2; e->w3 = w3; e->w4 = w4;
            e->mod_u = mod_u; e->used = 1u;
            m->inserted++;
            return;
        }
        if (e->s_u == s_u && (((e->w0 ^ w0) | (e->w1 ^ w1) | (e->w2 ^ w2) |
                               (e->w3 ^ w3) | (e->w4 ^ w4)) == 0u)) {
            e->mod_u = mod_u;
            return;
        }
        slot = (slot + 1u) & VKA_MOD_MAP_MASK;
    }
    m->partial = 1u;
}

static inline void vka_widen_idx_tail_fast(uint32_t *o, uint32_t count, size_t isz) {
    if (isz == 1u) {
        const uint8_t *src = (const uint8_t *)o + (size_t)count * 3u;
        uint32_t i = 0u;
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_BIG_ENDIAN__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
        for (; i + 4u <= count; i += 4u) {
            uint32_t r = 0u;
            memcpy(&r, src + i, 4);
            o[i] = r & 0xffu; o[i + 1u] = (r >> 8) & 0xffu;
            o[i + 2u] = (r >> 16) & 0xffu; o[i + 3u] = r >> 24;
        }
#endif
        for (; i < count; i++) o[i] = src[i];
    } else if (isz == 2u) {
        const uint8_t *src = (const uint8_t *)o + (size_t)count * 2u;
        uint32_t i = 0u;
        for (; i < count; i++) {
            uint16_t v = 0u;
            memcpy(&v, src + (size_t)i * 2u, 2);
            o[i] = v;
        }
    }
    if (aoi_w50_fault_mode() == 4 && count > 0u && isz < 4u) o[0] ^= 1u;
}

static inline int vka_inst_ubo_dirty_ok_fast(const uint32_t dirty[8], int iv, int ip) {
    uint64_t allow64 = (iv >= 0 && iv < 32 ? (UINT64_C(1) << (unsigned)iv) : UINT64_C(0)) |
                       (ip >= 0 && ip < 32 ? (UINT64_C(1) << (32u + (unsigned)ip)) : UINT64_C(0));
    const uint8_t *p = (const uint8_t *)dirty;
    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;
    memcpy(&w0, p, 8); memcpy(&w1, p + 8u, 8); memcpy(&w2, p + 16u, 8); memcpy(&w3, p + 24u, 8);
    if (aoi_w50_fault_mode() == 5) return (w0 & ~allow64) == UINT64_C(0);
    return ((w0 & ~allow64) | w1 | w2 | w3) == UINT64_C(0);
}

static inline int vka_scissor_eq_fast(const void *a16, const void *b16) {
    const uint8_t *pa = (const uint8_t *)a16, *pb = (const uint8_t *)b16;
    uint64_t a0 = 0, a1 = 0, b0 = 0, b1 = 0;
    memcpy(&a0, pa, 8); memcpy(&b0, pb, 8);
    memcpy(&a1, pa + 8u, 8); memcpy(&b1, pb + 8u, 8);
    return ((a0 ^ b0) | (a1 ^ b1)) == UINT64_C(0);
}
#endif
