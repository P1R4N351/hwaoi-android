/* Vectorized Q15 downmix, saturation clamp, and PCM16 gain helpers (DM_AUDIO_FAST=1, default on). */
#pragma once
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

enum { DM_Q15_CF = 32768, DM_Q15_CC = 23167, DM_Q15_CL = 8224, DM_Q15_CB = 23167 };

static _Atomic int g_dm_audio_fast = -1;
#ifdef DM_AUDIO_FAST_TEST
static int g_dm_audio_fault;
#define DM_AUD_FAULT() (g_dm_audio_fault)
#else
#define DM_AUD_FAULT() (0)
#endif

static inline int dm_audio_fast_sync(void) {
    const char *e = getenv("DM_AUDIO_FAST");
    int on = !(e && e[0] == '0' && !e[1]);
    atomic_store_explicit(&g_dm_audio_fast, on, memory_order_release);
    return on;
}

static inline int dm_audio_fast_on(void) {
    int v = atomic_load_explicit(&g_dm_audio_fast, memory_order_acquire);
    return v >= 0 ? v : dm_audio_fast_sync();
}

static inline int32_t dm_clamp_s16_s32(int32_t v) {
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

#if defined(__aarch64__) && defined(__ARM_NEON)
static inline int32x2_t dm_audren_6to2_pair_l(const int32_t *f, const int32_t *c,
                                                const int32_t *lfe, const int32_t *b, uint32_t s) {
    int64x2_t acc = vmull_n_s32(vld1_s32(f + s), DM_Q15_CF);
    acc = vmlal_n_s32(acc, vld1_s32(c + s), DM_Q15_CC);
    acc = vmlal_n_s32(acc, vld1_s32(lfe + s), DM_Q15_CL);
    acc = vmlal_n_s32(acc, vld1_s32(b + s), DM_Q15_CB);
    return vreinterpret_s32_u32(vshrn_n_s64(acc, 15));
}

static inline void dm_audren_finish4(int16_t *dst, int32x4_t vl, int32x4_t vr,
                                     uint32x4_t *vloud, int32x4_t *vpeak, uint32_t s) {
    const int32x4_t vlo = vdupq_n_s32(-32768), vhi = vdupq_n_s32(32767);
    int32x4_t cl = vminq_s32(vmaxq_s32(vl, vlo), vhi);
    int32x4_t cr = vminq_s32(vmaxq_s32(vr, vlo), vhi);
    int16x4x2_t st = {{ vqmovn_s32(cl), vqmovn_s32(cr) }};
    vst2_s16(dst + (size_t)s * 2u, st);
    *vloud = vorrq_u32(*vloud, vorrq_u32(vreinterpretq_u32_s32(cl), vreinterpretq_u32_s32(cr)));
    *vpeak = vmaxq_s32(*vpeak, vmaxq_s32(vabsq_s32(cl), vabsq_s32(cr)));
}
#endif

static inline void dm_audren_downmix_48k(int16_t * restrict dst, const int32_t * const ch[6],
                                         uint32_t k, uint32_t samples,
                                         uint64_t *loud_out, int32_t *peak_out) {
    uint32_t s = 0, loud_nz = 0;
    int32_t peak = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    if (DM_AUD_FAULT() != 1) {
        uint32x4_t vloud = vdupq_n_u32(0);
        int32x4_t vpeak = vdupq_n_s32(0);
        for (; s + 4u <= samples; s += 4u) {
            int32x4_t vl, vr;
            if (k >= 6u) {
                vl = vcombine_s32(dm_audren_6to2_pair_l(ch[0], ch[2], ch[3], ch[4], s),
                                  dm_audren_6to2_pair_l(ch[0], ch[2], ch[3], ch[4], s + 2u));
                vr = vcombine_s32(dm_audren_6to2_pair_l(ch[1], ch[2], ch[3], ch[5], s),
                                  dm_audren_6to2_pair_l(ch[1], ch[2], ch[3], ch[5], s + 2u));
            } else {
                vl = vld1q_s32(ch[0] + s);
                vr = k > 1u ? vld1q_s32(ch[1] + s) : vl;
            }
            dm_audren_finish4(dst, vl, vr, &vloud, &vpeak, s);
        }
        loud_nz = vmaxvq_u32(vloud) != 0u;
        peak = vmaxvq_s32(vpeak);
    }
#endif
    for (; s < samples; s++) {
        int32_t l, r;
        if (k >= 6u) {
            int64_t c = (int64_t)ch[2][s] * DM_Q15_CC + (int64_t)ch[3][s] * DM_Q15_CL;
            l = (int32_t)(((int64_t)ch[0][s] * DM_Q15_CF + c + (int64_t)ch[4][s] * DM_Q15_CB) >> 15);
            r = (int32_t)(((int64_t)ch[1][s] * DM_Q15_CF + c + (int64_t)ch[5][s] * DM_Q15_CB) >> 15);
        } else { l = ch[0][s]; r = k > 1u ? ch[1][s] : l; }
        l = dm_clamp_s16_s32(l); r = dm_clamp_s16_s32(r);
        dst[2u * s] = (int16_t)l; dst[2u * s + 1u] = (int16_t)r;
        if ((l | r) != 0) loud_nz = 1u;
        if (DM_AUD_FAULT() == 1) {
            int16_t al = (int16_t)l < 0 ? (int16_t)-(int16_t)l : (int16_t)l;
            int16_t ar = (int16_t)r < 0 ? (int16_t)-(int16_t)r : (int16_t)r;
            if (al > peak) peak = al;
            if (ar > peak) peak = ar;
        } else {
            int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            if (al > peak) peak = al;
            if (ar > peak) peak = ar;
        }
    }
    *loud_out = (uint64_t)loud_nz;
    *peak_out = peak;
}

static inline void dm_audren_clamp_peak(int16_t * restrict dst, const int32_t * restrict st,
                                        uint32_t count, uint64_t *loud_out, int32_t *peak_out) {
    uint32_t s = 0, loud_nz = 0;
    int32_t peak = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    if (DM_AUD_FAULT() != 1) {
        const int32x4_t vlo = vdupq_n_s32(-32768), vhi = vdupq_n_s32(32767);
        uint32x4_t vloud = vdupq_n_u32(0);
        int32x4_t vpeak = vdupq_n_s32(0);
        for (; s + 4u <= count; s += 4u) {
            int32x4_t v = vminq_s32(vmaxq_s32(vld1q_s32(st + s), vlo), vhi);
            vst1_s16(dst + s, vqmovn_s32(v));
            vloud = vorrq_u32(vloud, vreinterpretq_u32_s32(v));
            vpeak = vmaxq_s32(vpeak, vabsq_s32(v));
        }
        loud_nz = vmaxvq_u32(vloud) != 0u;
        peak = vmaxvq_s32(vpeak);
    }
#endif
    for (; s < count; s++) {
        int32_t v = dm_clamp_s16_s32(st[s]);
        dst[s] = (int16_t)v;
        if (v != 0) loud_nz = 1u;
        int32_t av = (DM_AUD_FAULT() == 1) ? ((int16_t)v < 0 ? -(int16_t)v : (int16_t)v) : (v < 0 ? -v : v);
        if (av > peak) peak = av;
    }
    *loud_out = (uint64_t)loud_nz;
    *peak_out = peak;
}

static inline void dm_audout_downmix_6to2_s16(int16_t * restrict out,
                                              const int16_t * restrict in6, uint64_t frames) {
    uint64_t i = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    if (DM_AUD_FAULT() != 2) {
        const int32x4_t vlo = vdupq_n_s32(-32768), vhi = vdupq_n_s32(32767);
        for (; i + 4u <= frames; i += 4u) {
            const int16_t a_fl[4] = { in6[0], in6[6], in6[12], in6[18] };
            const int16_t a_fr[4] = { in6[1], in6[7], in6[13], in6[19] };
            const int16_t a_cc[4] = { in6[2], in6[8], in6[14], in6[20] };
            const int16_t a_ll[4] = { in6[3], in6[9], in6[15], in6[21] };
            const int16_t a_bl[4] = { in6[4], in6[10], in6[16], in6[22] };
            const int16_t a_br[4] = { in6[5], in6[11], in6[17], in6[23] };
            int32x4_t fl = vmovl_s16(vld1_s16(a_fl)), fr = vmovl_s16(vld1_s16(a_fr));
            int32x4_t cc = vmovl_s16(vld1_s16(a_cc)), ll = vmovl_s16(vld1_s16(a_ll));
            int32x4_t bl = vmovl_s16(vld1_s16(a_bl)), br = vmovl_s16(vld1_s16(a_br));
            int64x2_t c0 = vmlal_n_s32(vmull_n_s32(vget_low_s32(cc), DM_Q15_CC), vget_low_s32(ll), DM_Q15_CL);
            int64x2_t c1 = vmlal_n_s32(vmull_n_s32(vget_high_s32(cc), DM_Q15_CC), vget_high_s32(ll), DM_Q15_CL);
            int64x2_t l0 = vmlal_n_s32(vaddq_s64(vmull_n_s32(vget_low_s32(fl), DM_Q15_CF), c0), vget_low_s32(bl), DM_Q15_CB);
            int64x2_t l1 = vmlal_n_s32(vaddq_s64(vmull_n_s32(vget_high_s32(fl), DM_Q15_CF), c1), vget_high_s32(bl), DM_Q15_CB);
            int64x2_t r0 = vmlal_n_s32(vaddq_s64(vmull_n_s32(vget_low_s32(fr), DM_Q15_CF), c0), vget_low_s32(br), DM_Q15_CB);
            int64x2_t r1 = vmlal_n_s32(vaddq_s64(vmull_n_s32(vget_high_s32(fr), DM_Q15_CF), c1), vget_high_s32(br), DM_Q15_CB);
            int32x4_t ls = vminq_s32(vmaxq_s32(vcombine_s32(vreinterpret_s32_u32(vshrn_n_s64(l0, 15)), vreinterpret_s32_u32(vshrn_n_s64(l1, 15))), vlo), vhi);
            int32x4_t rs = vminq_s32(vmaxq_s32(vcombine_s32(vreinterpret_s32_u32(vshrn_n_s64(r0, 15)), vreinterpret_s32_u32(vshrn_n_s64(r1, 15))), vlo), vhi);
            int16x4x2_t st = {{ vqmovn_s32(ls), vqmovn_s32(rs) }};
            vst2_s16(out, st);
            in6 += 24; out += 8;
        }
    }
#endif
    for (; i < frames; i++, in6 += 6) {
        if (DM_AUD_FAULT() == 2) {
            int32_t c = (int32_t)in6[2] * DM_Q15_CC + (int32_t)in6[3] * DM_Q15_CL;
            int32_t l = ((int32_t)in6[0] * DM_Q15_CF + c + (int32_t)in6[4] * DM_Q15_CB) >> 15;
            int32_t r = ((int32_t)in6[1] * DM_Q15_CF + c + (int32_t)in6[5] * DM_Q15_CB) >> 15;
            *out++ = (int16_t)dm_clamp_s16_s32(l);
            *out++ = (int16_t)dm_clamp_s16_s32(r);
        } else {
            int64_t c = (int64_t)in6[2] * DM_Q15_CC + (int64_t)in6[3] * DM_Q15_CL;
            int64_t l = ((int64_t)in6[0] * DM_Q15_CF + c + (int64_t)in6[4] * DM_Q15_CB) >> 15;
            int64_t r = ((int64_t)in6[1] * DM_Q15_CF + c + (int64_t)in6[5] * DM_Q15_CB) >> 15;
            *out++ = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            *out++ = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
    }
}

static inline void dm_audout_scale_s16(int16_t * restrict s16, uint64_t ns, float v) {
    uint64_t i = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    if (DM_AUD_FAULT() != 3) {
        const float32x4_t vlo = vdupq_n_f32(-32768.0f), vhi = vdupq_n_f32(32767.0f);
        for (; i + 4u <= ns; i += 4u) {
            float32x4_t f = vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vld1_s16(s16 + i))), v);
            f = vminq_f32(vmaxq_f32(f, vlo), vhi);
            vst1_s16(s16 + i, vmovn_s32(vcvtq_s32_f32(f)));
        }
    }
#endif
    for (; i < ns; i++) {
        float x = s16[i] * v;
        s16[i] = (DM_AUD_FAULT() == 3) ? (int16_t)x : (int16_t)(x > 32767.f ? 32767.f : x < -32768.f ? -32768.f : x);
    }
}
