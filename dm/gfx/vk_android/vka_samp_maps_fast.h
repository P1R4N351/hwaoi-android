// Fast paths for 40B sampler eq/hash map, wrap LUT, ASTC zero fill, and /proc/self/maps parsing (Opt 51).
#ifndef VKA_SAMP_MAPS_FAST_H
#define VKA_SAMP_MAPS_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W49_TEST_HOOKS
extern _Atomic int g_vka_samp_maps_fast_override;
extern _Atomic int g_aoi_w49_fault;
static inline void vka_samp_maps_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_samp_maps_fast_override, v, memory_order_release);
}
static inline void aoi_w49_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w49_fault, f, memory_order_release);
}
static inline int aoi_w49_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w49_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W49_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w49_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w49_fault_mode(void) { return 0; }
#endif

static inline int vka_samp_maps_fast_on(void) {
#ifdef T_W49_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_samp_maps_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_SAMP_MAPS_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline int vka_samp_par_eq_fast(const void *a, const void *b) {
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
    uint64_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0;
    memcpy(&a0, pa, 8);
    memcpy(&b0, pb, 8);
    if (aoi_w49_fault_mode() == 1) return a0 == b0;
    memcpy(&a1, pa + 8u, 8);
    memcpy(&b1, pb + 8u, 8);
    memcpy(&a2, pa + 16u, 8);
    memcpy(&b2, pb + 16u, 8);
    memcpy(&a3, pa + 24u, 8);
    memcpy(&b3, pb + 24u, 8);
    memcpy(&a4, pa + 32u, 8);
    memcpy(&b4, pb + 32u, 8);
    return ((a0 ^ b0) | (a1 ^ b1) | (a2 ^ b2) | (a3 ^ b3) | (a4 ^ b4)) == 0u;
}

static inline uint64_t vka_samp_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t vka_samp_par_hash_fast(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    uint64_t w0 = 0, w1 = 0, w2 = 0, w3 = 0, w4 = 0;
    memcpy(&w0, b, 8);
    memcpy(&w1, b + 8u, 8);
    memcpy(&w2, b + 16u, 8);
    memcpy(&w3, b + 24u, 8);
    memcpy(&w4, b + 32u, 8);
    uint64_t h = vka_samp_mix64(w0 ^ UINT64_C(0x9e3779b97f4a7c15));
    h = vka_samp_mix64(h ^ w1);
    h = vka_samp_mix64(h ^ w2);
    h = vka_samp_mix64(h ^ w3);
    h = vka_samp_mix64(h ^ w4);
    return h ? h : UINT64_C(1);
}

enum { VKA_SAMP_MAP_CAP = 8192u, VKA_SAMP_MAP_MASK = VKA_SAMP_MAP_CAP - 1u };

typedef struct {
    uint16_t idx1[VKA_SAMP_MAP_CAP];
    uint64_t hashes[VKA_SAMP_MAP_CAP];
} vka_samp_map_t;

static inline int vka_samp_map_find(const vka_samp_map_t *m, const void *entries,
                                    size_t stride, const void *key_p, uint64_t h) {
    if (aoi_w49_fault_mode() == 2) return -1;
    uint32_t slot = (uint32_t)(h & VKA_SAMP_MAP_MASK);
    for (uint32_t probe = 0u; probe < VKA_SAMP_MAP_CAP; probe++) {
        uint16_t idx1 = m->idx1[slot];
        if (!idx1) return -1;
        if (m->hashes[slot] == h) {
            const uint8_t *cand = (const uint8_t *)entries + (size_t)(idx1 - 1u) * stride;
            if (vka_samp_par_eq_fast(cand, key_p)) return (int)(idx1 - 1u);
        }
        slot = (slot + 1u) & VKA_SAMP_MAP_MASK;
    }
    return -1;
}

static inline void vka_samp_map_insert(vka_samp_map_t *m, uint64_t h, uint16_t idx1) {
    if (!idx1) return;
    uint32_t slot = (uint32_t)(h & VKA_SAMP_MAP_MASK);
    for (uint32_t probe = 0u; probe < VKA_SAMP_MAP_CAP; probe++) {
        if (!m->idx1[slot]) {
            m->idx1[slot] = idx1;
            m->hashes[slot] = h;
            return;
        }
        slot = (slot + 1u) & VKA_SAMP_MAP_MASK;
    }
}

static inline int vka_wrap_fast(int w) {
    if (aoi_w49_fault_mode() == 3 && w == 4) return 3;
    static const uint8_t k_wr[8] = {2, 0, 4, 4, 4, 3, 1, 2};
    return (unsigned)w < 8u ? (int)k_wr[w] : 2;
}

static inline void vka_astc_zero_blocks_fast(uint8_t *p, size_t n) {
    if (aoi_w49_fault_mode() == 4) return;
    static const uint8_t k_z8[8] = {0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (size_t i = 0u; i + 16u <= n; i += 16u) {
        uint64_t a = 0, b = 0;
        memcpy(&a, p + i, 8);
        memcpy(&b, p + i + 8u, 8);
        if ((a | b) == 0u) memcpy(p + i, k_z8, 8);
    }
}

static inline int vka_maps_parse_hex_fast(const char *s, size_t len, size_t *pos, uintptr_t *out) {
    size_t i = *pos;
    uintptr_t v = 0;
    int digits = 0;
    while (i < len) {
        unsigned c = (unsigned char)s[i], d = 0u;
        if (c - (unsigned)'0' <= 9u) d = c - (unsigned)'0';
        else if ((c | 32u) - (unsigned)'a' <= 5u) d = (c | 32u) - (unsigned)'a' + 10u;
        else break;
        v = (v << 4u) | d;
        digits = 1;
        i++;
    }
    *pos = i;
    *out = v;
    return digits;
}

static inline int vka_maps_parse_line_fast(const char *s, size_t len,
                                           uintptr_t *lo_out, uintptr_t *hi_out, int *prot_out) {
    size_t i = 0u;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    uintptr_t a = 0, b = 0;
    if (!vka_maps_parse_hex_fast(s, len, &i, &a)) return 0;
    if (i >= len || s[i] != '-') return 0;
    i++;
    if (!vka_maps_parse_hex_fast(s, len, &i, &b)) return 0;
    if (i >= len || (s[i] != ' ' && s[i] != '\t')) return 0;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= len || (unsigned char)s[i] <= (unsigned char)' ') return 0;
    char p0 = s[i];
    char p1 = (i + 1u < len && (unsigned char)s[i + 1u] > (unsigned char)' ') ? s[i + 1u] : '\0';
    char p2 = (p1 && i + 2u < len && (unsigned char)s[i + 2u] > (unsigned char)' ') ? s[i + 2u] : '\0';
    int p = (p0 == 'r' ? 1 : 0) | (p1 == 'w' ? 2 : 0) | (p2 == 'x' ? 4 : 0);
    if (aoi_w49_fault_mode() == 5) p &= ~2;
    *lo_out = a;
    *hi_out = b;
    *prot_out = p;
    return 1;
}
#endif
