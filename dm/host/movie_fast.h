// Fast movie/codec YUV->NV12/RGBA/BGRA, VP9 keyframe header, and slot/pump helpers (DM_MOVIE_FAST=1, default on; =0 disables).
// Portable ISO C11 (with optional aarch64 NEON fast paths) so host unit/ASan/TSan suites exercise exact helpers.
#pragma once
#include <assert.h>
#include <pthread.h>
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

static _Atomic int g_dm_movie_fast_state;

static inline int dm_movie_fast_on(void) {
    int s = atomic_load_explicit(&g_dm_movie_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_MOVIE_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_dm_movie_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef T_W31_TEST_HOOKS
static _Atomic int g_dm_w31_fault;
static inline void dm_movie_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_dm_movie_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void dm_w31_set_fault(int mode) {
    atomic_store_explicit(&g_dm_w31_fault, mode, memory_order_release);
}
static inline int dm_w31_fault_mode(void) {
    return atomic_load_explicit(&g_dm_w31_fault, memory_order_acquire);
}
#else
static inline int dm_w31_fault_mode(void) { return 0; }
#endif

static inline uint64_t dm_nv12_swar_zip4(uint32_t u4, uint32_t v4, int fault_mode) {
    uint64_t ux = (uint64_t)u4;
    uint64_t vx = (uint64_t)v4;
    ux = (ux | (ux << 16)) & UINT64_C(0x0000ffff0000ffff);
    ux = (ux | (ux << 8)) & UINT64_C(0x00ff00ff00ff00ff);
    vx = (vx | (vx << 16)) & UINT64_C(0x0000ffff0000ffff);
    vx = (vx | (vx << 8)) & UINT64_C(0x00ff00ff00ff00ff);
    return fault_mode == 1 ? (ux | (vx << 16)) : (ux | (vx << 8));
}

static inline int dm_nv12_uv_simd_span(const uint8_t * restrict u, const uint8_t * restrict v,
                                        uint8_t * restrict o, int x, int limit, int fault_mode) {
#if defined(__aarch64__)
    for (; x + 16 <= limit; x += 16) {
        uint8x16_t uu = vld1q_u8(u + x), vv = vld1q_u8(v + x);
        uint8x16x2_t t = fault_mode == 1 ? (uint8x16x2_t){{vv, uu}} : (uint8x16x2_t){{uu, vv}};
        vst2q_u8(o + 2 * x, t);
    }
#elif defined(__SSE2__)
    for (; x + 16 <= limit; x += 16) {
        __m128i uu = _mm_loadu_si128((const __m128i *)(u + x));
        __m128i vv = _mm_loadu_si128((const __m128i *)(v + x));
        if (fault_mode == 1) { __m128i tmp = uu; uu = vv; vv = tmp; }
        _mm_storeu_si128((__m128i *)(o + 2 * x), _mm_unpacklo_epi8(uu, vv));
        _mm_storeu_si128((__m128i *)(o + 2 * x + 16), _mm_unpackhi_epi8(uu, vv));
    }
#else
    (void)u; (void)v; (void)o; (void)limit; (void)fault_mode;
#endif
    return x;
}

static inline int dm_nv12_uv_swar_span(const uint8_t * restrict u, const uint8_t * restrict v,
                                        uint8_t * restrict o, int x, int limit, int fault_mode) {
    x = dm_nv12_uv_simd_span(u, v, o, x, limit, fault_mode);
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    for (; x + 8 <= limit; x += 8) {
        uint32_t u0 = 0u, u1 = 0u, v0 = 0u, v1 = 0u;
        memcpy(&u0, u + x, sizeof u0); memcpy(&u1, u + x + 4, sizeof u1);
        memcpy(&v0, v + x, sizeof v0); memcpy(&v1, v + x + 4, sizeof v1);
        uint64_t q0 = dm_nv12_swar_zip4(u0, v0, fault_mode);
        uint64_t q1 = dm_nv12_swar_zip4(u1, v1, fault_mode);
        memcpy(o + 2 * x, &q0, sizeof q0); memcpy(o + 2 * x + 8, &q1, sizeof q1);
    }
    if (x + 4 <= limit) {
        uint32_t u0 = 0u, v0 = 0u;
        memcpy(&u0, u + x, sizeof u0); memcpy(&v0, v + x, sizeof v0);
        uint64_t q0 = dm_nv12_swar_zip4(u0, v0, fault_mode);
        memcpy(o + 2 * x, &q0, sizeof q0);
        x += 4;
    }
#else
    (void)u; (void)v; (void)o; (void)limit; (void)fault_mode;
#endif
    return x;
}

static inline void dm_movie_rgba_fill_px(uint8_t *dst, size_t nbytes) {
    assert(dst != NULL || nbytes == 0u);
    size_t k = 0u;
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const uint64_t blk2 = UINT64_C(0xff000000ff000000);
    for (; k + 16u <= nbytes; k += 16u) {
        memcpy(dst + k, &blk2, sizeof blk2);
        memcpy(dst + k + 8u, &blk2, sizeof blk2);
    }
    if (k + 8u <= nbytes) {
        memcpy(dst + k, &blk2, sizeof blk2);
        k += 8u;
    }
#endif
    for (; k + 3u < nbytes; k += 4u) {
        dst[k] = 0u; dst[k + 1u] = 0u; dst[k + 2u] = 0u; dst[k + 3u] = 255u;
    }
}

static inline void dm_movie_rgba_pad_borders(uint8_t *dst, int cw, int ch, int w, int h, int fault_mode) {
    assert(dst != NULL && w > 0 && h > 0 && cw >= 0 && ch >= 0 && cw <= w && ch <= h);
    if (cw < w) {
        size_t tail_bytes = (size_t)(w - cw) * 4u;
        size_t row_bytes = (size_t)w * 4u;
        size_t x_off = (size_t)cw * 4u;
        for (int y = 0; y < ch; y++) dm_movie_rgba_fill_px(dst + (size_t)y * row_bytes + x_off, tail_bytes);
    }
    if (ch < h && fault_mode != 2) {
        dm_movie_rgba_fill_px(dst + (size_t)ch * (size_t)w * 4u, (size_t)(h - ch) * (size_t)w * 4u);
    }
}

static inline size_t dm_movie_rgba_to_bgra_simd(uint8_t * restrict dst, const uint8_t * restrict src,
                                               size_t nbytes, int fault_mode) {
    size_t k = 0u;
#if defined(__aarch64__)
    static const uint8_t idx_swp[16] = {2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15};
    static const uint8_t idx_id[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8x16_t idx = vld1q_u8(fault_mode == 3 ? idx_id : idx_swp);
    for (; k + 64u <= nbytes; k += 64u) {
        uint8x16_t v0 = vqtbl1q_u8(vld1q_u8(src + k), idx);
        uint8x16_t v1 = vqtbl1q_u8(vld1q_u8(src + k + 16u), idx);
        uint8x16_t v2 = vqtbl1q_u8(vld1q_u8(src + k + 32u), idx);
        uint8x16_t v3 = vqtbl1q_u8(vld1q_u8(src + k + 48u), idx);
        vst1q_u8(dst + k, v0); vst1q_u8(dst + k + 16u, v1);
        vst1q_u8(dst + k + 32u, v2); vst1q_u8(dst + k + 48u, v3);
    }
    for (; k + 16u <= nbytes; k += 16u) vst1q_u8(dst + k, vqtbl1q_u8(vld1q_u8(src + k), idx));
#elif defined(__SSE2__)
    if (fault_mode != 3) {
        const __m128i mga = _mm_set1_epi32((int)0xff00ff00u), mr = _mm_set1_epi32(0x000000ff);
        for (; k + 64u <= nbytes; k += 64u) {
            for (size_t j = 0u; j < 64u; j += 16u) {
                __m128i v = _mm_loadu_si128((const __m128i *)(src + k + j));
                __m128i rb = _mm_or_si128(_mm_and_si128(_mm_srli_epi32(v, 16), mr), _mm_slli_epi32(_mm_and_si128(v, mr), 16));
                _mm_storeu_si128((__m128i *)(dst + k + j), _mm_or_si128(_mm_and_si128(v, mga), rb));
            }
        }
        for (; k + 16u <= nbytes; k += 16u) {
            __m128i v = _mm_loadu_si128((const __m128i *)(src + k));
            __m128i rb = _mm_or_si128(_mm_and_si128(_mm_srli_epi32(v, 16), mr), _mm_slli_epi32(_mm_and_si128(v, mr), 16));
            _mm_storeu_si128((__m128i *)(dst + k), _mm_or_si128(_mm_and_si128(v, mga), rb));
        }
    }
#else
    (void)dst; (void)src; (void)nbytes; (void)fault_mode;
#endif
    return k;
}

static inline void dm_movie_rgba_to_bgra(uint8_t * restrict dst, const uint8_t * restrict src,
                                         size_t nbytes, int fault_mode) {
    assert(dst != NULL && src != NULL && dst != src);
    size_t k = dm_movie_rgba_to_bgra_simd(dst, src, nbytes, fault_mode);
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    for (; k + 16u <= nbytes; k += 16u) {
        uint64_t p0 = 0u, p1 = 0u;
        memcpy(&p0, src + k, sizeof p0);
        memcpy(&p1, src + k + 8u, sizeof p1);
        if (fault_mode != 3) {
            p0 = (p0 & UINT64_C(0xff00ff00ff00ff00)) | ((p0 & UINT64_C(0x00ff000000ff0000)) >> 16) | ((p0 & UINT64_C(0x000000ff000000ff)) << 16);
            p1 = (p1 & UINT64_C(0xff00ff00ff00ff00)) | ((p1 & UINT64_C(0x00ff000000ff0000)) >> 16) | ((p1 & UINT64_C(0x000000ff000000ff)) << 16);
        }
        memcpy(dst + k, &p0, sizeof p0);
        memcpy(dst + k + 8u, &p1, sizeof p1);
    }
#endif
    for (; k + 3u < nbytes; k += 4u) {
        dst[k] = fault_mode == 3 ? src[k] : src[k + 2u];
        dst[k + 1u] = src[k + 1u];
        dst[k + 2u] = fault_mode == 3 ? src[k + 2u] : src[k];
        dst[k + 3u] = src[k + 3u];
    }
}

static inline uint64_t dm_vp9_be64(const uint8_t b[8]) {
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8) | (uint64_t)b[7];
}

static inline unsigned dm_vp9_bits(uint64_t hi, uint64_t lo, int pos, int k) {
    assert(pos >= 0 && k > 0 && k <= 24 && pos + k <= 128);
    uint64_t mask = (UINT64_C(1) << k) - 1u;
    if (pos + k <= 64) return (unsigned)((hi >> (64 - pos - k)) & mask);
    if (pos >= 64) return (unsigned)((lo >> (128 - pos - k)) & mask);
    int r = pos + k - 64;
    uint64_t top = hi & ((UINT64_C(1) << (k - r)) - 1u);
    return (unsigned)((top << r) | (lo >> (64 - r)));
}

static inline int dm_movie_vp9_size_fast(const uint8_t *p, int n, int *w, int *h, int fault_mode) {
    if (!p || !w || !h || n < 9) return 0;
    uint8_t buf[16] = {0};
    memcpy(buf, p, (size_t)(n < 16 ? n : 16));
    uint64_t hi = dm_vp9_be64(buf);
    uint64_t lo = dm_vp9_be64(buf + 8);
    if (dm_vp9_bits(hi, lo, 0, 2) != 2u) return 0;
    unsigned lo_p = dm_vp9_bits(hi, lo, 2, 1), hi_p = dm_vp9_bits(hi, lo, 3, 1);
    unsigned prof = (hi_p << 1) | lo_p;
    int pos = 4 + (prof == 3u ? 1 : 0);
    if (dm_vp9_bits(hi, lo, pos, 2) != 0u) return 0;
    pos += 4;
    if (dm_vp9_bits(hi, lo, pos, 24) != 0x498342u) return 0;
    pos += 24;
    if (prof >= 2u && fault_mode != 4) pos += 1;
    unsigned cs = dm_vp9_bits(hi, lo, pos, 3);
    pos += 3;
    if (cs != 7u) {
        pos += 1;
        if (prof == 1u || prof == 3u) pos += 3;
    } else if (prof == 1u || prof == 3u) {
        pos += 1;
    }
    if (((pos + 31) >> 3) >= n) return 0;
    *w = (int)dm_vp9_bits(hi, lo, pos, 16) + 1;
    *h = (int)dm_vp9_bits(hi, lo, pos + 16, 16) + 1;
    return 1;
}

static inline int dm_movie_first_free_slot(uint64_t used_mask, int nout, int fault_mode) {
    if (nout <= 0) return -1;
    uint64_t avail = nout >= 64 ? UINT64_MAX : ((UINT64_C(1) << nout) - 1u);
    uint64_t effective = fault_mode == 5 ? (used_mask | UINT64_C(1)) : used_mask;
    uint64_t free_bits = (~effective) & avail;
    return free_bits ? __builtin_ctzll(free_bits) : -1;
}

static inline void dm_movie_pump_wake(int *pk_go, int pk_waiting, uint64_t *elided,
                                      pthread_cond_t *pkc, int fast) {
    assert(pk_go != NULL && pkc != NULL);
    int was_go = *pk_go;
    *pk_go = 1;
    if (!fast || (!was_go && pk_waiting > 0)) {
        pthread_cond_signal(pkc);
    } else if (elided) {
        (*elided)++;
    }
}
