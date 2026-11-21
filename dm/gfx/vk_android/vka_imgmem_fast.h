// Fast Vulkan image-memory block suballocator, zero-share hash & bit-transposed memory-type LUT (DM_VK_IMGMEM_FAST=1, default on; =0 disables).
#pragma once
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

enum { VKZ_IDX_SLOTS = 2048, VKZ_IDX_MASK = 2047, VKZ_IDX_MAX_N = 1536 };

static _Atomic int g_vka_imgmem_fast_state;

static inline int vka_imgmem_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_imgmem_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_IMGMEM_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_imgmem_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef T_W32_TEST_HOOKS
static _Atomic int g_vka_w32_fault;
static inline void vka_imgmem_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_imgmem_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w32_set_fault(int mode) {
    atomic_store_explicit(&g_vka_w32_fault, mode, memory_order_release);
}
static inline int vka_w32_fault_mode(void) {
    return atomic_load_explicit(&g_vka_w32_fault, memory_order_acquire);
}
#else
static inline int vka_w32_fault_mode(void) { return 0; }
#endif

static inline int vke_simd_skip4(const uint8_t *p, uint32_t n) {
#if defined(__aarch64__)
    uint8x16_t b0 = vld1q_u8(p);
    uint8x16_t b1 = vld1q_u8(p + 16u);
    uint32x4_t ns = vuzp2q_u32(vreinterpretq_u32_u8(b0), vreinterpretq_u32_u8(b1));
    return vmaxvq_u32(ns) < n;
#elif defined(__SSE2__)
    __m128i b0, b1;
    memcpy(&b0, p, sizeof b0);
    memcpy(&b1, p + 16u, sizeof b1);
    __m128i ns = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(b0), _mm_castsi128_ps(b1), 0xDD));
    __m128i bias = _mm_set1_epi32((int)0x80000000u);
    __m128i lt = _mm_cmplt_epi32(_mm_xor_si128(ns, bias), _mm_set1_epi32((int)(n ^ 0x80000000u)));
    return _mm_movemask_epi8(lt) == 0xffff;
#else
    uint32_t p0[2], p1[2], p2[2], p3[2];
    memcpy(p0, p, 8u);
    memcpy(p1, p + 8u, 8u);
    memcpy(p2, p + 16u, 8u);
    memcpy(p3, p + 24u, 8u);
    return (p0[1] < n) & (p1[1] < n) & (p2[1] < n) & (p3[1] < n);
#endif
}

static inline uint32_t vke_scan_fit4(const uint8_t *raw, uint32_t nf, uint32_t n, uint32_t align,
                                     int max_valid, uint32_t *out_max, uint64_t *out_s, int fault_mode) {
    uint32_t i = 0u, mx = 0u;
    if (max_valid && !out_max) {
        for (; i + 4u <= nf; i += 4u) {
            if (vke_simd_skip4(raw + (size_t)i * 8u, n)) continue;
            for (uint32_t j = 0u; j < 4u; j++) {
                uint32_t pair[2];
                memcpy(pair, raw + (size_t)(i + j) * 8u, sizeof pair);
                if (pair[1] < n) continue;
                uint64_t s = ((uint64_t)pair[0] + align - 1u) & ~(uint64_t)(align - 1u);
                if (s + n <= (uint64_t)pair[0] + pair[1]) {
                    if (fault_mode == 1 && s + n + 1u <= (uint64_t)pair[0] + pair[1]) s += 1u;
                    *out_s = s;
                    return i + j;
                }
            }
        }
    }
    for (; i < nf; i++) {
        uint32_t pair[2];
        memcpy(pair, raw + (size_t)i * 8u, sizeof pair);
        if (pair[1] > mx) mx = pair[1];
        if (pair[1] < n) continue;
        uint64_t s = ((uint64_t)pair[0] + align - 1u) & ~(uint64_t)(align - 1u);
        if (s + n <= (uint64_t)pair[0] + pair[1]) {
            if (fault_mode == 1 && s + n + 1u <= (uint64_t)pair[0] + pair[1]) s += 1u;
            *out_s = s;
            return i;
        }
    }
    if (out_max) *out_max = mx;
    return UINT32_MAX;
}

static inline uint64_t vkz_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline uint64_t vkz_hash_bytes(const void *key, uint32_t klen) {
    const uint8_t *p = (const uint8_t *)key;
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15) ^ ((uint64_t)klen * UINT64_C(0xd6e8feb86659fd93));
    uint32_t pos = 0u;
    for (; pos + 8u <= klen; pos += 8u) {
        uint64_t w = 0u;
        memcpy(&w, p + pos, sizeof w);
        h = vkz_mix64(h ^ w);
    }
    if (pos < klen) {
        uint64_t w = 0u;
        memcpy(&w, p + pos, klen - pos);
        h = vkz_mix64(h ^ w);
    }
    return h ? h : UINT64_C(1);
}

static inline uint64_t vkz_hash_u64(uint64_t h) {
    return vkz_mix64(h ^ UINT64_C(0x9e3779b97f4a7c15));
}

static inline void vkz_slot_put(int16_t *slots, uint32_t ideal, int16_t val) {
    for (uint32_t step = 0u; step < VKZ_IDX_SLOTS; step++) {
        uint32_t pos = (ideal + step) & VKZ_IDX_MASK;
        if (!slots[pos]) { slots[pos] = val; return; }
    }
}

static inline void vkz_slot_rebind(int16_t *slots, uint32_t ideal, int16_t old_v, int16_t new_v) {
    for (uint32_t step = 0u; step < VKZ_IDX_SLOTS; step++) {
        uint32_t pos = (ideal + step) & VKZ_IDX_MASK;
        int16_t cur = slots[pos];
        if (!cur) return;
        if (cur == old_v) { slots[pos] = new_v; return; }
    }
}

typedef struct {
    uint32_t count;
    uint32_t valid_mask;
    uint32_t prop_mask[6];
    uint32_t flags[32];
    int ready;
} vka_memtype_lut;

static inline void vka_memtype_lut_build(vka_memtype_lut *lut, uint32_t count, const uint32_t *flags_arr) {
    assert(lut != NULL);
    memset(lut, 0, sizeof *lut);
    if (!flags_arr || count == 0u || count > 32u) return;
    lut->count = count;
    lut->valid_mask = count == 32u ? UINT32_MAX : ((UINT32_C(1) << count) - 1u);
    for (uint32_t i = 0u; i < count; i++) {
        uint32_t fl = flags_arr[i];
        lut->flags[i] = fl;
        for (int b = 0; b < 6; b++) {
            if (fl & (1u << b)) lut->prop_mask[b] |= (1u << i);
        }
    }
    lut->ready = 1;
}

static inline uint32_t vka_memtype_pick_fast(const vka_memtype_lut *lut, uint32_t bits,
                                             uint32_t want, uint32_t avoid, int fault_mode) {
    if (!lut || !lut->ready) return UINT32_MAX;
    uint32_t m = bits & lut->valid_mask;
    for (int b = 0; b < 6; b++) {
        if (want & (1u << b)) m &= lut->prop_mask[b];
        if (avoid & (1u << b)) m &= ~lut->prop_mask[b];
    }
    uint32_t hi_w = want & ~0x3fu, hi_a = avoid & ~0x3fu;
    if ((hi_w | hi_a) && m) {
        uint32_t cand = m;
        m = 0u;
        for (int step = 0; step < 32 && cand; step++) {
            int i = __builtin_ctz(cand);
            cand &= cand - 1u;
            if ((lut->flags[i] & hi_w) == hi_w && !(lut->flags[i] & hi_a)) m |= (1u << i);
        }
    }
    if (!m) return UINT32_MAX;
    if (fault_mode == 4 && (m & (m - 1u)) != 0u) return 31u - (uint32_t)__builtin_clz(m);
    return (uint32_t)__builtin_ctz(m);
}

static inline int vka_imem_empty_should_free(uint32_t empty_count, int fault_mode) {
    if (fault_mode == 5) return empty_count >= 1u;
    return empty_count > 1u;
}
