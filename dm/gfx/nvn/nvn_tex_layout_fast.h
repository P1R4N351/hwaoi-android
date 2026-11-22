// Fast paths for NVN texture format LUTs, Tegra X1 block-linear layout math, mip-slice matching, and sparse idle checks (Opt 44).
#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t bpb, bw, bh;
} nvn_fmt_fast_t;

typedef struct {
    _Atomic uintptr_t stl;
    _Atomic uint64_t sval;
    _Atomic uint32_t pending;
} nvn_sparse_fast_state_t;

#ifdef T_W42_TEST_HOOKS
extern _Atomic int g_dm_tex_layout_fast_override;
extern _Atomic int g_aoi_w42_fault;
#endif

static inline int dm_tex_layout_fast_on(void) {
#ifdef T_W42_TEST_HOOKS
    int ov = atomic_load_explicit(&g_dm_tex_layout_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
    const char *e = getenv("DM_TEX_LAYOUT_FAST");
    return !(e && strcmp(e, "0") == 0);
#else
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_TEX_LAYOUT_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
#endif
}

static inline int aoi_w42_fault_mode(void) {
#ifdef T_W42_TEST_HOOKS
    int f = atomic_load_explicit(&g_aoi_w42_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W42_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w42_fault, v, memory_order_relaxed);
    return v;
#else
    return 0;
#endif
}

#ifdef T_W42_TEST_HOOKS
static inline void dm_tex_layout_fast_set_for_test(int mode) {
    atomic_store_explicit(&g_dm_tex_layout_fast_override, mode, memory_order_release);
}
static inline void aoi_w42_set_fault(int mode) {
    atomic_store_explicit(&g_aoi_w42_fault, mode, memory_order_release);
}
#endif

#define NVN_FMT_ROW4(b) {b, 1, 1}, {b, 1, 1}, {b, 1, 1}, {b, 1, 1}
#define NVN_FMT_BC4(b) {b, 4, 4}, {b, 4, 4}, {b, 4, 4}, {b, 4, 4}
#define NVN_FMT_ASTC14 \
    {16, 4, 4}, {16, 5, 4}, {16, 5, 5}, {16, 6, 5}, {16, 6, 6}, {16, 8, 5}, {16, 8, 6}, \
    {16, 8, 8}, {16, 10, 5}, {16, 10, 6}, {16, 10, 8}, {16, 10, 10}, {16, 12, 10}, {16, 12, 12}

static const nvn_fmt_fast_t k_nvn_fmt_lut[0x9D] = {
    [0x00] = {0, 1, 1},
    [0x01] = NVN_FMT_ROW4(1),
    [0x05] = NVN_FMT_ROW4(2), {2, 1, 1},
    [0x0A] = {4, 1, 1}, {4, 1, 1}, {4, 1, 1},
    [0x0D] = NVN_FMT_ROW4(2),
    [0x11] = NVN_FMT_ROW4(4), {4, 1, 1},
    [0x16] = {8, 1, 1}, {8, 1, 1}, {8, 1, 1},
    [0x19] = NVN_FMT_ROW4(3),
    [0x1D] = NVN_FMT_ROW4(6), {6, 1, 1},
    [0x22] = {12, 1, 1}, {12, 1, 1}, {12, 1, 1},
    [0x25] = NVN_FMT_ROW4(4),
    [0x29] = NVN_FMT_ROW4(8), {8, 1, 1},
    [0x2E] = {16, 1, 1}, {16, 1, 1}, {16, 1, 1},
    [0x31] = {1, 1, 1}, {2, 1, 1}, {4, 1, 1}, {4, 1, 1}, {4, 1, 1}, {8, 1, 1}, {4, 1, 1}, {4, 1, 1},
    [0x39] = NVN_FMT_ROW4(2),
    [0x3D] = NVN_FMT_ROW4(4),
    [0x41] = {8, 4, 4}, {8, 4, 4}, {16, 4, 4}, {16, 4, 4},
    [0x45] = {8, 4, 4}, {8, 4, 4}, {16, 4, 4}, {16, 4, 4},
    [0x49] = {8, 4, 4}, {8, 4, 4}, {16, 4, 4}, {16, 4, 4},
    [0x4D] = NVN_FMT_BC4(16),
    [0x51] = {1, 1, 1}, {1, 1, 1}, {2, 1, 1}, {2, 1, 1}, {4, 1, 1}, {4, 1, 1},
    [0x57] = {2, 1, 1}, {2, 1, 1}, {4, 1, 1}, {4, 1, 1}, {8, 1, 1}, {8, 1, 1},
    [0x5D] = {3, 1, 1}, {3, 1, 1}, {6, 1, 1}, {6, 1, 1}, {12, 1, 1}, {12, 1, 1},
    [0x63] = {4, 1, 1}, {4, 1, 1}, {8, 1, 1}, {8, 1, 1}, {16, 1, 1}, {16, 1, 1},
    [0x69] = NVN_FMT_ROW4(4),
    [0x6D] = NVN_FMT_ROW4(4),
    [0x71] = NVN_FMT_ROW4(8), {8, 1, 1},
    [0x76] = {16, 1, 1}, {16, 1, 1}, {16, 1, 1},
    [0x79] = NVN_FMT_ASTC14,
    [0x87] = NVN_FMT_ASTC14,
    [0x95] = NVN_FMT_ROW4(2),
    [0x99] = NVN_FMT_ROW4(4),
};

#undef NVN_FMT_ASTC14
#undef NVN_FMT_BC4
#undef NVN_FMT_ROW4

static inline nvn_fmt_fast_t nvn_fmt_lookup_fast(int f) {
    if ((unsigned)f >= 0x9Du) return (nvn_fmt_fast_t){0, 1, 1};
    nvn_fmt_fast_t t = k_nvn_fmt_lut[f];
    if (aoi_w42_fault_mode() == 1 && f >= 0x79 && f <= 0x94) t.bpb = 20u;
    return t;
}

static inline uint32_t nvn_blk_fast(uint32_t s, uint32_t b) {
    uint32_t num = s + b - 1u;
    switch (b) {
        case 1u: return s;
        case 4u: return num >> 2;
        case 8u: return num >> 3;
        case 5u: return (uint32_t)(((uint64_t)num * UINT64_C(0xCCCCCCCD)) >> 34);
        case 6u:
            if (aoi_w42_fault_mode() == 2) return s / 6u;
            return (uint32_t)(((uint64_t)num * UINT64_C(0xAAAAAAAB)) >> 34);
        case 10u: return (uint32_t)(((uint64_t)num * UINT64_C(0xCCCCCCCD)) >> 35);
        case 12u: return (uint32_t)(((uint64_t)num * UINT64_C(0xAAAAAAAB)) >> 35);
        default: return b ? num / b : 0u;
    }
}

static inline unsigned nvn_pick_tile_fast(uint32_t gobs) {
    if (gobs < 2u) return 0u;
    if (gobs >= 16u) return 4u;
    return 31u - (unsigned)__builtin_clz(gobs);
}

static inline unsigned nvn_adj_tile_fast(unsigned shift, uint32_t unit, uint32_t dim) {
    if (!shift || !unit || dim <= unit) return 0u;
    if (aoi_w42_fault_mode() == 3) return shift;
    unsigned u_log2 = (unit == 8u) ? 3u : 0u;
    unsigned lim = 32u - u_log2 - (unsigned)__builtin_clz(dim - 1u);
    return shift < lim ? shift : lim;
}

static inline void nvn_sparse_publish(nvn_sparse_fast_state_t *st, uintptr_t stl, uint64_t sval, uint32_t pending) {
    atomic_store_explicit(&st->sval, sval, memory_order_release);
    atomic_store_explicit(&st->pending, pending, memory_order_release);
    atomic_store_explicit(&st->stl, stl, memory_order_release);
}

static inline int nvn_sparse_try_idle(const nvn_sparse_fast_state_t *st, int hung, uintptr_t *out_stl, uint64_t *out_sval) {
    uintptr_t stl = atomic_load_explicit(&st->stl, memory_order_acquire);
    if (!stl) { *out_stl = 0; *out_sval = 0; return 1; }
    if (atomic_load_explicit(&st->pending, memory_order_acquire) != 0u) return 0;
    if (hung) { *out_stl = 0; *out_sval = 0; return 1; }
    uint64_t sv = atomic_load_explicit(&st->sval, memory_order_acquire);
    if (atomic_load_explicit(&st->pending, memory_order_acquire) != 0u) return 0;
    if (aoi_w42_fault_mode() == 5 && sv) sv = 0u;
    *out_stl = stl; *out_sval = sv;
    return 1;
}

static inline int nvn_sparse_bmap_take(uint64_t bits[16], uint8_t *cursor) {
    uint32_t start = ((uint32_t)(*cursor)) & 15u;
    for (uint32_t step = 0u; step < 16u; step++) {
        uint32_t w = (start + step) & 15u;
        uint64_t inv = ~bits[w];
        if (!inv) continue;
        uint32_t k = (uint32_t)__builtin_ctzll(inv);
        bits[w] |= (UINT64_C(1) << k);
        *cursor = (uint8_t)w;
        return (int)(w * 64u + k);
    }
    return -1;
}

static inline int nvn_sparse_bmap_give(uint64_t bits[16], uint8_t *cursor, uint32_t k) {
    uint32_t w = k >> 6, b = k & 63u;
    if (w >= 16u || !((bits[w] >> b) & UINT64_C(1))) return 0;
    bits[w] &= ~(UINT64_C(1) << b);
    if (w < (uint32_t)(*cursor)) *cursor = (uint8_t)w;
    return 1;
}
