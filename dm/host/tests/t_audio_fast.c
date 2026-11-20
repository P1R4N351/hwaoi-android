/* Standalone equivalence, corner-case, benchmark, and Delta #27 fault test for hle_audio_fast.h. */
#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#define DM_AUDIO_FAST_TEST 1
#include "../hle_audio_fast.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t af_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void ref_audren_48k(int16_t *dst, const int32_t * const ch[6], uint32_t k,
                           uint32_t samples, uint64_t *loud_out, int32_t *peak_out) {
    uint64_t loud = 0; int32_t peak = 0;
    for (uint32_t s = 0; s < samples; s++) {
        int32_t l, r;
        if (k >= 6u) {
            int64_t c = (int64_t)ch[2][s] * DM_Q15_CC + (int64_t)ch[3][s] * DM_Q15_CL;
            l = (int32_t)(((int64_t)ch[0][s] * DM_Q15_CF + c + (int64_t)ch[4][s] * DM_Q15_CB) >> 15);
            r = (int32_t)(((int64_t)ch[1][s] * DM_Q15_CF + c + (int64_t)ch[5][s] * DM_Q15_CB) >> 15);
        } else { l = ch[0][s]; r = k > 1u ? ch[1][s] : l; }
        int32_t lr[2] = { l, r };
        for (int p = 0; p < 2; p++) {
            int32_t v = lr[p];
            v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            dst[2u * s + (uint32_t)p] = (int16_t)v;
            loud |= (uint64_t)v;
            int32_t av = v < 0 ? -v : v;
            if (av > peak) peak = av;
        }
    }
    *loud_out = loud != 0u;
    *peak_out = peak;
}

static void ref_clamp_peak(int16_t *dst, const int32_t *st, uint32_t count,
                           uint64_t *loud_out, int32_t *peak_out) {
    uint64_t loud = 0; int32_t peak = 0;
    for (uint32_t s = 0; s < count; s++) {
        int32_t v = st[s]; v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        dst[s] = (int16_t)v; loud |= (uint64_t)v;
        int32_t av = v < 0 ? -v : v; if (av > peak) peak = av;
    }
    *loud_out = loud != 0u;
    *peak_out = peak;
}

static void ref_6to2_s16(int16_t *out, const int16_t *in6, uint64_t frames) {
    for (uint64_t i = 0; i < frames; i++) {
        const int16_t *p = in6 + 6u * i;
        int64_t c = (int64_t)p[2] * DM_Q15_CC + (int64_t)p[3] * DM_Q15_CL;
        int64_t l = ((int64_t)p[0] * DM_Q15_CF + c + (int64_t)p[4] * DM_Q15_CB) >> 15;
        int64_t r = ((int64_t)p[1] * DM_Q15_CF + c + (int64_t)p[5] * DM_Q15_CB) >> 15;
        out[2u * i]      = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2u * i + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
}

static int verify_audren_edge_cases(void) {
    static const uint32_t lens[] = {1, 3, 4, 7, 15, 16, 17, 31, 240};
    static const uint32_t ks[] = {1, 2, 6};
    static int32_t buf[6][256];
    const int32_t *ch[6] = {buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]};
    int16_t r_dst[512], f_dst[512];

    for (size_t li = 0; li < sizeof lens / sizeof lens[0]; li++) {
        uint32_t n = lens[li];
        for (int pat = 0; pat < 4; pat++) {
            for (int c = 0; c < 6; c++) for (uint32_t s = 0; s < n; s++) {
                buf[c][s] = pat == 0 ? 0 : pat == 1 ? -32768 : pat == 2 ? (c == 0 && s == 3 ? -32768 : 0) :
                            (int32_t)(((s * 7919u + (uint32_t)c * 131u) % 200001u) - 100000);
                if (pat == 1 && s == 0) buf[c][s] = -40000;
            }
            for (size_t ki = 0; ki < sizeof ks / sizeof ks[0]; ki++) {
                uint64_t rl = 0, fl = 0; int32_t rp = 0, fp = 0;
                memset(r_dst, 0xa5, sizeof r_dst); memset(f_dst, 0xa5, sizeof f_dst);
                ref_audren_48k(r_dst, ch, ks[ki], n, &rl, &rp);
                dm_audren_downmix_48k(f_dst, ch, ks[ki], n, &fl, &fp);
                if (memcmp(r_dst, f_dst, (size_t)n * 4u) != 0 || rl != fl || rp != fp) {
                    fprintf(stderr, "FAIL: audren_48k mismatch n=%u k=%u pat=%d peak(%d vs %d) loud(%llu vs %llu) fault=%d\n",
                            n, ks[ki], pat, rp, fp, (unsigned long long)rl, (unsigned long long)fl, g_dm_audio_fault);
                    return 1;
                }
            }
        }
        uint64_t rl = 0, fl = 0; int32_t rp = 0, fp = 0;
        ref_clamp_peak(r_dst, buf[0], n, &rl, &rp);
        dm_audren_clamp_peak(f_dst, buf[0], n, &fl, &fp);
        if (memcmp(r_dst, f_dst, (size_t)n * 2u) != 0 || rl != fl || rp != fp) {
            fprintf(stderr, "FAIL: audren_clamp_peak mismatch n=%u peak(%d vs %d) fault=%d\n", n, rp, fp, g_dm_audio_fault);
            return 1;
        }
    }
    return 0;
}

static int verify_audout_exhaustive(void) {
    static const uint64_t flens[] = {1, 2, 3, 4, 5, 7, 17, 128};
    static int16_t in6[128 * 6], r_out[128 * 2], f_out[128 * 2];
    for (size_t li = 0; li < sizeof flens / sizeof flens[0]; li++) {
        uint64_t nf = flens[li];
        for (int pat = 0; pat < 3; pat++) {
            for (uint64_t i = 0; i < nf * 6u; i++)
                in6[i] = pat == 0 ? (int16_t)-32768 : pat == 1 ? (int16_t)32767 : (int16_t)((i * 2113u + 7u) % 65536u - 32768);
            ref_6to2_s16(r_out, in6, nf);
            dm_audout_downmix_6to2_s16(f_out, in6, nf);
            if (memcmp(r_out, f_out, (size_t)nf * 4u) != 0) {
                fprintf(stderr, "FAIL: audout_6to2 mismatch nf=%llu pat=%d fault=%d\n",
                        (unsigned long long)nf, pat, g_dm_audio_fault);
                return 1;
            }
        }
    }
    static int16_t s_ref[65536], s_fast[65536];
    static const float gains[] = {0.0f, 0.37f, 1.5f, 4.0f};
    for (size_t gi = 0; gi < sizeof gains / sizeof gains[0]; gi++) {
        for (int x = 0; x < 65536; x++) { s_ref[x] = s_fast[x] = (int16_t)(x - 32768); }
        for (int x = 0; x < 65536; x++) {
            float v = s_ref[x] * gains[gi];
            s_ref[x] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
        }
        dm_audout_scale_s16(s_fast, 65536u, gains[gi]);
        if (memcmp(s_ref, s_fast, sizeof s_ref) != 0) {
            fprintf(stderr, "FAIL: audout_scale_s16 mismatch gain=%.2f fault=%d\n", gains[gi], g_dm_audio_fault);
            return 1;
        }
    }
    return 0;
}

static void bench_aoi_audio(void) {
    enum { ITERS = 20000, S = 240 };
    static int32_t chbuf[6][S];
    const int32_t *ch[6] = {chbuf[0], chbuf[1], chbuf[2], chbuf[3], chbuf[4], chbuf[5]};
    static int16_t dst[S * 2], in6[S * 6], out2[S * 2];
    for (int c = 0; c < 6; c++) for (int s = 0; s < S; s++) {
        chbuf[c][s] = ((s * 997 + c * 173) % 50000) - 25000;
        in6[s * 6 + c] = (int16_t)chbuf[c][s];
    }
    uint64_t l0 = 0, l1 = 0; int32_t p0 = 0, p1 = 0;
    uint64_t t0 = af_now_ns();
    for (int it = 0; it < ITERS; it++) {
        uint64_t ll = 0; int32_t pp = 0;
        ref_audren_48k(dst, ch, 6u, S, &ll, &pp);
        ref_6to2_s16(out2, in6, S);
        l0 ^= ll; p0 ^= pp; out2[0] += (int16_t)(it & 1);
    }
    uint64_t dt_ref = af_now_ns() - t0;

    uint64_t t1 = af_now_ns();
    for (int it = 0; it < ITERS; it++) {
        uint64_t ll = 0; int32_t pp = 0;
        dm_audren_downmix_48k(dst, ch, 6u, S, &ll, &pp);
        dm_audout_downmix_6to2_s16(out2, in6, S);
        l1 ^= ll; p1 ^= pp; out2[0] += (int16_t)(it & 1);
    }
    uint64_t dt_fast = af_now_ns() - t1;
    assert(l0 == l1 && p0 == p1);
    printf("aoi_audio_fast: frames=%dx%d(5.1->2) base=%.2f ms fast=%.2f ms speedup=%.2fx\n",
           ITERS, S, dt_ref / 1e6, dt_fast / 1e6, (double)dt_ref / (dt_fast ? dt_fast : 1));
}

int main(void) {
    const char *fe = getenv("T_AUDIO_FAST_FAULT");
    g_dm_audio_fault = fe ? atoi(fe) : 0;
    if (verify_audren_edge_cases() != 0) return 1;
    if (verify_audout_exhaustive() != 0) return 1;
    bench_aoi_audio();
    printf("t_audio_fast PASS (fault=%d)\n", g_dm_audio_fault);
    return 0;
}
