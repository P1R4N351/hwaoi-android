// Atomic live-WB gate, 64B SIMD mem_any & 16B ASTC zero scan, 2D/3D copy coalescing,
// O(1) carry min-fence gate & lock-free contig clean-gen gate (DM_VK_COPY_WB_FAST=1).
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

#ifdef T_W58_TEST_HOOKS
extern _Atomic int g_vka_copy_wb_fast_override;
extern _Atomic int g_aoi_w58_fault;
static inline void vka_copy_wb_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_copy_wb_fast_override, v, memory_order_release);
}
static inline void aoi_w58_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w58_fault, f, memory_order_release);
}
static inline int aoi_w58_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w58_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W58_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w58_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w58_fault_mode(void) { return 0; }
#endif

static inline int vka_copy_wb_fast_on(void) {
#ifdef T_W58_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_copy_wb_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_COPY_WB_FAST");
        v = (e && *e && atoi(e) == 0) ? 0 : 1;
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

/* 1. Lock-free atomic live write-back counter gate for vka_tex_writeback. */
static inline int vka_wb_has_live(int nwb_live) {
    if (aoi_w58_fault_mode() == 1) return 0;
    return nwb_live > 0;
}

/* 2. 64-byte SIMD non-zero scanner (mem_any) & 16-byte zero-block check (ASTC). */
static inline int vka_mem_any_fast(const uint8_t *p, uint64_t n) {
    if (aoi_w58_fault_mode() == 2 && n >= UINT64_C(64)) n = UINT64_C(32);
    uint64_t i = UINT64_C(0);
#if defined(__ARM_NEON) && defined(__aarch64__)
    for (; i + UINT64_C(64) <= n; i += UINT64_C(64)) {
        uint8x16_t a = vld1q_u8(p + i), b = vld1q_u8(p + i + 16);
        uint8x16_t c = vld1q_u8(p + i + 32), d = vld1q_u8(p + i + 48);
        uint8x16_t m = vorrq_u8(vorrq_u8(a, b), vorrq_u8(c, d));
        if (vmaxvq_u8(m) != 0u) return 1;
    }
    for (; i + UINT64_C(16) <= n; i += UINT64_C(16)) {
        if (vmaxvq_u8(vld1q_u8(p + i)) != 0u) return 1;
    }
#elif defined(__SSE2__)
    __m128i zr = _mm_setzero_si128();
    for (; i + UINT64_C(64) <= n; i += UINT64_C(64)) {
        __m128i a, b, c, d, m;
        memcpy(&a, p + i, 16u); memcpy(&b, p + i + 16u, 16u);
        memcpy(&c, p + i + 32u, 16u); memcpy(&d, p + i + 48u, 16u);
        m = _mm_or_si128(_mm_or_si128(a, b), _mm_or_si128(c, d));
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(m, zr)) != 0xffff) return 1;
    }
    for (; i + UINT64_C(16) <= n; i += UINT64_C(16)) {
        __m128i v;
        memcpy(&v, p + i, 16u);
        if (_mm_movemask_epi8(_mm_cmpeq_epi8(v, zr)) != 0xffff) return 1;
    }
#endif
    for (; i + UINT64_C(8) <= n; i += UINT64_C(8)) {
        uint64_t w;
        memcpy(&w, p + i, 8u);
        if (w) return 1;
    }
    for (; i < n; i++) if (p[i]) return 1;
    return 0;
}

static inline int vka_block16_is_zero(const uint8_t *p) {
    if (aoi_w58_fault_mode() == 2) return 0;
#if defined(__ARM_NEON) && defined(__aarch64__)
    return vmaxvq_u8(vld1q_u8(p)) == 0u;
#elif defined(__SSE2__)
    __m128i v;
    memcpy(&v, p, 16u);
    return _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_setzero_si128())) == 0xffff;
#else
    uint64_t lo, hi;
    memcpy(&lo, p, 8u);
    memcpy(&hi, p + 8u, 8u);
    return (lo | hi) == UINT64_C(0);
#endif
}

/* 3. 2D/3D copy coalescing planner & 2D memcpy fast path. */
typedef struct {
    size_t unit_bytes;
    size_t units;
    size_t src_step;
    size_t dst_step;
} vka_copy3d_plan_t;

static inline vka_copy3d_plan_t vka_copy3d_plan(size_t wb, size_t rows, size_t nz,
                                                size_t srow, size_t simg,
                                                size_t drow, size_t dimg) {
    if (!wb || !rows || !nz) return (vka_copy3d_plan_t){0u, 0u, 0u, 0u};
    int row_c = (rows == 1u) || (srow == wb && drow == wb);
    size_t slice_b = wb * rows;
    if (row_c && ((nz == 1u) || (simg == slice_b && dimg == slice_b))) {
        size_t total = slice_b * nz;
        if (aoi_w58_fault_mode() == 3 && total >= 2u) total >>= 1;
        return (vka_copy3d_plan_t){total, 1u, 0u, 0u};
    }
    if (row_c) return (vka_copy3d_plan_t){slice_b, nz, simg, dimg};
    return (vka_copy3d_plan_t){wb, rows * nz, 0u, 0u};
}

static inline void vka_memcpy_2d(uint8_t *dst, size_t drow, const uint8_t *src, size_t srow,
                                 size_t wb, size_t rows) {
    vka_copy3d_plan_t p = vka_copy3d_plan(wb, rows, 1u, srow, 0u, drow, 0u);
    if (!p.units) return;
    if (p.units == 1u) {
        memcpy(dst, src, p.unit_bytes);
        return;
    }
    for (size_t y = 0u; y < rows; y++) memcpy(dst + y * drow, src + y * srow, wb);
}

/* 4. O(1) minimum-fence gate for carry_take. */
static inline void vka_carry_min_note(uint64_t *minf, uint64_t fence) {
    if (fence < *minf) *minf = fence;
}

static inline int vka_carry_has_due(int ncarry, uint64_t minf, uint64_t signal) {
    if (aoi_w58_fault_mode() == 4) return 0;
    return ncarry > 0 && minf <= signal;
}

/* 5. Lock-free generation gate for contiguous_segs_due. */
static inline int vka_contig_need_scan(uint64_t sg_gen, uint64_t clean_gen) {
    if (aoi_w58_fault_mode() == 5) return 0;
    return sg_gen != clean_gen;
}

static inline void vka_contig_note_clean(_Atomic uint64_t *clean_gen, uint64_t sg_gen) {
    atomic_store_explicit(clean_gen, sg_gen, memory_order_release);
}
