// Fast paths for PSO integer format class, fout masks, kmap eq, last-key check, heavy set, and dynamic-state slices (Opt 50).
#ifndef VKA_PSO_KEY_FAST_H
#define VKA_PSO_KEY_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W48_TEST_HOOKS
extern _Atomic int g_vka_pso_key_fast_override;
extern _Atomic int g_aoi_w48_fault;
static inline void vka_pso_key_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_pso_key_fast_override, v, memory_order_release);
}
static inline void aoi_w48_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w48_fault, f, memory_order_release);
}
static inline int aoi_w48_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w48_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W48_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w48_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w48_fault_mode(void) { return 0; }
#endif

static inline int vka_pso_key_fast_on(void) {
#ifdef T_W48_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_pso_key_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_PSO_KEY_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline int vka_vkf_int_class_fast(uint32_t f) {
    if (aoi_w48_fault_mode() == 1 && f == 41u) return 0;
    static const uint8_t k_lut[128] = {
        [13] = 1, [14] = 2, [20] = 1, [21] = 2, [27] = 1, [28] = 2,
        [34] = 1, [35] = 2, [41] = 1, [42] = 2, [48] = 1, [49] = 2,
        [55] = 1, [56] = 2, [62] = 1, [63] = 2, [68] = 1, [69] = 2,
        [74] = 1, [75] = 2, [81] = 1, [82] = 2, [88] = 1, [89] = 2,
        [95] = 1, [96] = 2, [98] = 1, [99] = 2, [101] = 1, [102] = 2,
        [104] = 1, [105] = 2, [107] = 1, [108] = 2, [110] = 1, [111] = 2,
        [113] = 1, [114] = 2, [116] = 1, [117] = 2, [119] = 1, [120] = 2
    };
    return f < 128u ? (int)k_lut[f] : 0;
}

static inline void vka_fout_masks_fast(uint32_t fout_mask, uint32_t fout_types,
                                       const uint32_t *cfmt, uint32_t ncolor,
                                       uint32_t *um, uint32_t *sm) {
    uint32_t n = ncolor < 8u ? ncolor : 8u;
    uint32_t mask = n ? ((1u << n) - 1u) : 0u;
    uint32_t active = fout_mask & mask;
    if (aoi_w48_fault_mode() == 2) active &= 0x0fu;
    uint32_t u = 0u, s = 0u;
    while (active) {
        unsigned i = (unsigned)__builtin_ctz(active);
        active &= active - 1u;
        int c = vka_vkf_int_class_fast(cfmt[i]);
        if (c && ((fout_types >> (2u * i)) & 3u) == 0u) {
            if (c == 1) u |= 1u << i;
            else s |= 1u << i;
        }
    }
    *um = u;
    *sm = s;
}

static inline int vka_km_eq_fast(const void *a, const void *b, size_t klen) {
    const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
    if (klen >= 8u) {
        uint64_t a0 = 0, b0 = 0;
        memcpy(&a0, pa, 8);
        memcpy(&b0, pb, 8);
        if (a0 != b0) return 0;
        if (aoi_w48_fault_mode() == 3 && klen >= 16u) return 1;
        uint64_t al = 0, bl = 0;
        memcpy(&al, pa + klen - 8u, 8);
        memcpy(&bl, pb + klen - 8u, 8);
        if (al != bl) return 0;
    }
    size_t i = (klen >= 8u) ? 8u : 0u;
    size_t end = (klen >= 8u) ? (klen - 8u) : klen;
    while (i + 16u <= end) {
        uint64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        memcpy(&x0, pa + i, 8);
        memcpy(&y0, pb + i, 8);
        memcpy(&x1, pa + i + 8u, 8);
        memcpy(&y1, pb + i + 8u, 8);
        if ((x0 ^ y0) | (x1 ^ y1)) return 0;
        i += 16u;
    }
    while (i + 8u <= end) {
        uint64_t x = 0, y = 0;
        memcpy(&x, pa + i, 8);
        memcpy(&y, pb + i, 8);
        if (x != y) return 0;
        i += 8u;
    }
    return i < end ? (memcmp(pa + i, pb + i, end - i) == 0) : 1;
}

static inline int vka_gkey_same_last_fast(const void *last_sh, uint64_t last_lay, const void *last_k,
                                          const void *sh, uint64_t lay, const void *k,
                                          size_t sh_bytes, size_t k_bytes) {
    if (aoi_w48_fault_mode() != 4 && last_lay != lay) return 0;
    if (!vka_km_eq_fast(last_sh, sh, sh_bytes)) return 0;
    return vka_km_eq_fast(last_k, k, k_bytes);
}

static inline void vka_gkey_pack_fast(void *fk_ptr, size_t lay_off, size_t lay_sz, size_t k_off,
                                      const void *sh, size_t sh_bytes,
                                      const void *lay_ptr, const void *k, size_t k_bytes) {
    uint8_t *dst = (uint8_t *)fk_ptr;
    if (lay_off > sh_bytes) memset(dst + sh_bytes, 0, lay_off - sh_bytes);
    if (k_off > lay_off + lay_sz) memset(dst + lay_off + lay_sz, 0, k_off - (lay_off + lay_sz));
    memcpy(dst, sh, sh_bytes);
    memcpy(dst + lay_off, lay_ptr, lay_sz);
    memcpy(dst + k_off, k, k_bytes);
}

enum { VKA_HEAVY_FAST_CAP = 2048u, VKA_HEAVY_FAST_MASK = VKA_HEAVY_FAST_CAP - 1u };

typedef struct {
    _Atomic uint64_t bloom;
    _Atomic uint64_t slots[VKA_HEAVY_FAST_CAP];
} vka_heavy_set_t;

static inline uint64_t vka_heavy_mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static inline int vka_heavy_has_fast(const vka_heavy_set_t *s, uint64_t k) {
    if (!k) return 0;
    uint64_t h = vka_heavy_mix64(k);
    uint64_t bm = (UINT64_C(1) << (h & 63u)) | (UINT64_C(1) << ((h >> 6) & 63u));
    if ((atomic_load_explicit(&s->bloom, memory_order_acquire) & bm) != bm) return 0;
    uint32_t base = (uint32_t)(h & VKA_HEAVY_FAST_MASK);
    for (uint32_t p = 0u; p < VKA_HEAVY_FAST_CAP; p++) {
        uint64_t cur = atomic_load_explicit(&s->slots[(base + p) & VKA_HEAVY_FAST_MASK], memory_order_acquire);
        if (cur == k) return 1;
        if (!cur) return 0;
    }
    return 0;
}

static inline void vka_heavy_add_fast(vka_heavy_set_t *s, uint64_t k) {
    if (!k) return;
    uint64_t h = vka_heavy_mix64(k);
    uint32_t base = (uint32_t)(h & VKA_HEAVY_FAST_MASK);
    int stored = 0;
    for (uint32_t p = 0u; p < VKA_HEAVY_FAST_CAP; p++) {
        uint32_t idx = (base + p) & VKA_HEAVY_FAST_MASK;
        uint64_t expected = 0u;
        if (atomic_compare_exchange_strong_explicit(&s->slots[idx], &expected, k,
                memory_order_acq_rel, memory_order_acquire) || expected == k) {
            stored = 1;
            break;
        }
    }
    if (stored && aoi_w48_fault_mode() != 5) {
        uint64_t bm = (UINT64_C(1) << (h & 63u)) | (UINT64_C(1) << ((h >> 6) & 63u));
        atomic_fetch_or_explicit(&s->bloom, bm, memory_order_acq_rel);
    }
}

enum {
    VKA_DYN_VIEWPORT = 0,
    VKA_DYN_SCISSOR = 1,
    VKA_DYN_LINE_WIDTH = 2,
    VKA_DYN_DEPTH_BIAS = 3,
    VKA_DYN_BLEND_CONSTANTS = 4,
    VKA_DYN_STENCIL_COMPARE_MASK = 6,
    VKA_DYN_STENCIL_WRITE_MASK = 7,
    VKA_DYN_STENCIL_REFERENCE = 8,
    VKA_DYN_CULL_MODE = 1000267000,
    VKA_DYN_FRONT_FACE = 1000267001,
    VKA_DYN_PRIMITIVE_TOPOLOGY = 1000267002,
    VKA_DYN_DEPTH_TEST_ENABLE = 1000267006,
    VKA_DYN_DEPTH_WRITE_ENABLE = 1000267007,
    VKA_DYN_DEPTH_COMPARE_OP = 1000267008,
    VKA_DYN_DEPTH_BOUNDS_TEST_ENABLE = 1000267009,
    VKA_DYN_STENCIL_TEST_ENABLE = 1000267010,
    VKA_DYN_STENCIL_OP = 1000267011,
    VKA_DYN_VERTEX_INPUT_EXT = 1000352000,
    VKA_DYN_RASTERIZER_DISCARD_ENABLE = 1000377001,
    VKA_DYN_DEPTH_BIAS_ENABLE = 1000377002,
    VKA_DYN_PRIMITIVE_RESTART_ENABLE = 1000377004
};

static inline const uint32_t *vka_dyn_full_fast(int mesh, int vin_static, uint32_t *out_count) {
    static const uint32_t k_nonmesh[21] = {
        VKA_DYN_VIEWPORT, VKA_DYN_SCISSOR, VKA_DYN_CULL_MODE, VKA_DYN_FRONT_FACE,
        VKA_DYN_PRIMITIVE_TOPOLOGY, VKA_DYN_DEPTH_TEST_ENABLE, VKA_DYN_DEPTH_WRITE_ENABLE,
        VKA_DYN_DEPTH_COMPARE_OP, VKA_DYN_DEPTH_BOUNDS_TEST_ENABLE, VKA_DYN_STENCIL_TEST_ENABLE,
        VKA_DYN_STENCIL_OP, VKA_DYN_STENCIL_COMPARE_MASK, VKA_DYN_STENCIL_WRITE_MASK,
        VKA_DYN_STENCIL_REFERENCE, VKA_DYN_DEPTH_BIAS, VKA_DYN_DEPTH_BIAS_ENABLE,
        VKA_DYN_RASTERIZER_DISCARD_ENABLE, VKA_DYN_PRIMITIVE_RESTART_ENABLE,
        VKA_DYN_BLEND_CONSTANTS, VKA_DYN_LINE_WIDTH, VKA_DYN_VERTEX_INPUT_EXT
    };
    static const uint32_t k_mesh[18] = {
        VKA_DYN_VIEWPORT, VKA_DYN_SCISSOR, VKA_DYN_CULL_MODE, VKA_DYN_FRONT_FACE,
        VKA_DYN_DEPTH_TEST_ENABLE, VKA_DYN_DEPTH_WRITE_ENABLE, VKA_DYN_DEPTH_COMPARE_OP,
        VKA_DYN_DEPTH_BOUNDS_TEST_ENABLE, VKA_DYN_STENCIL_TEST_ENABLE, VKA_DYN_STENCIL_OP,
        VKA_DYN_STENCIL_COMPARE_MASK, VKA_DYN_STENCIL_WRITE_MASK, VKA_DYN_STENCIL_REFERENCE,
        VKA_DYN_DEPTH_BIAS, VKA_DYN_DEPTH_BIAS_ENABLE, VKA_DYN_RASTERIZER_DISCARD_ENABLE,
        VKA_DYN_BLEND_CONSTANTS, VKA_DYN_LINE_WIDTH
    };
    if (mesh) {
        *out_count = 18u;
        return k_mesh;
    }
    *out_count = vin_static ? 20u : 21u;
    return k_nonmesh;
}

static inline const uint32_t *vka_dyn_slice_fast(int mesh, int vin_static, int part, uint32_t *out_count) {
    static const uint32_t k_vi[3] = {
        VKA_DYN_PRIMITIVE_TOPOLOGY, VKA_DYN_PRIMITIVE_RESTART_ENABLE, VKA_DYN_VERTEX_INPUT_EXT
    };
    static const uint32_t k_pre[8] = {
        VKA_DYN_VIEWPORT, VKA_DYN_SCISSOR, VKA_DYN_CULL_MODE, VKA_DYN_FRONT_FACE,
        VKA_DYN_DEPTH_BIAS, VKA_DYN_DEPTH_BIAS_ENABLE, VKA_DYN_RASTERIZER_DISCARD_ENABLE,
        VKA_DYN_LINE_WIDTH
    };
    static const uint32_t k_fs[9] = {
        VKA_DYN_DEPTH_TEST_ENABLE, VKA_DYN_DEPTH_WRITE_ENABLE, VKA_DYN_DEPTH_COMPARE_OP,
        VKA_DYN_DEPTH_BOUNDS_TEST_ENABLE, VKA_DYN_STENCIL_TEST_ENABLE, VKA_DYN_STENCIL_OP,
        VKA_DYN_STENCIL_COMPARE_MASK, VKA_DYN_STENCIL_WRITE_MASK, VKA_DYN_STENCIL_REFERENCE
    };
    static const uint32_t k_fo[1] = { VKA_DYN_BLEND_CONSTANTS };
    static const uint32_t k_prefs[17] = {
        VKA_DYN_VIEWPORT, VKA_DYN_SCISSOR, VKA_DYN_CULL_MODE, VKA_DYN_FRONT_FACE,
        VKA_DYN_DEPTH_TEST_ENABLE, VKA_DYN_DEPTH_WRITE_ENABLE, VKA_DYN_DEPTH_COMPARE_OP,
        VKA_DYN_DEPTH_BOUNDS_TEST_ENABLE, VKA_DYN_STENCIL_TEST_ENABLE, VKA_DYN_STENCIL_OP,
        VKA_DYN_STENCIL_COMPARE_MASK, VKA_DYN_STENCIL_WRITE_MASK, VKA_DYN_STENCIL_REFERENCE,
        VKA_DYN_DEPTH_BIAS, VKA_DYN_DEPTH_BIAS_ENABLE, VKA_DYN_RASTERIZER_DISCARD_ENABLE,
        VKA_DYN_LINE_WIDTH
    };
    if (part == 0) {
        *out_count = mesh ? 0u : (vin_static ? 2u : 3u);
        return mesh ? NULL : k_vi;
    }
    if (part == 1) { *out_count = 8u; return k_pre; }
    if (part == 2) { *out_count = 9u; return k_fs; }
    if (part == 3) { *out_count = 1u; return k_fo; }
    if (part == 4) { *out_count = 17u; return k_prefs; }
    *out_count = 0u;
    return NULL;
}
#endif
