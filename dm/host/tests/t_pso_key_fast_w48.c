// Wave 48 (Optimization 50): parity, concurrency, TSan, benchmark, and fault tests for vka_pso_key_fast.h.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/vk_android/vka_pso_key_fast.h"

_Atomic int g_vka_pso_key_fast_override = -1;
_Atomic int g_aoi_w48_fault = -1;
static int g_fail = 0;

#define EXPECT_EQ_U64(msg, want, got) do { \
    uint64_t wa_ = (uint64_t)(want), ga_ = (uint64_t)(got); \
    if (wa_ != ga_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s: want=%llu got=%llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w48_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int ref_vkf_int_class(uint32_t f) {
    static const uint16_t u[] = {
        13, 20, 27, 34, 41, 48, 55, 62, 68, 74, 81, 88, 95, 98, 101, 104, 107, 110, 113, 116, 119
    };
    for (size_t i = 0; i < sizeof u / sizeof *u; i++) {
        if (f == u[i]) return 1;
        if (f == (uint32_t)u[i] + 1u) return 2;
    }
    return 0;
}

static void ref_fout_masks(uint32_t fout_mask, uint32_t fout_types, const uint32_t *cfmt,
                           uint32_t ncolor, uint32_t *um, uint32_t *sm) {
    *um = 0u;
    *sm = 0u;
    for (uint32_t i = 0; i < ncolor && i < 8u; i++) {
        int c = ref_vkf_int_class(cfmt[i]);
        if (c && ((fout_mask >> i) & 1u) && ((fout_types >> (2u * i)) & 3u) == 0u) {
            if (c == 1) *um |= 1u << i;
            else *sm |= 1u << i;
        }
    }
}

static uint32_t ref_dyn_full(int mesh, int vin_static, uint32_t *out) {
    static const uint32_t base[] = {
        VKA_DYN_VIEWPORT, VKA_DYN_SCISSOR, VKA_DYN_CULL_MODE, VKA_DYN_FRONT_FACE,
        VKA_DYN_PRIMITIVE_TOPOLOGY, VKA_DYN_DEPTH_TEST_ENABLE, VKA_DYN_DEPTH_WRITE_ENABLE,
        VKA_DYN_DEPTH_COMPARE_OP, VKA_DYN_DEPTH_BOUNDS_TEST_ENABLE, VKA_DYN_STENCIL_TEST_ENABLE,
        VKA_DYN_STENCIL_OP, VKA_DYN_STENCIL_COMPARE_MASK, VKA_DYN_STENCIL_WRITE_MASK,
        VKA_DYN_STENCIL_REFERENCE, VKA_DYN_DEPTH_BIAS, VKA_DYN_DEPTH_BIAS_ENABLE,
        VKA_DYN_RASTERIZER_DISCARD_ENABLE, VKA_DYN_PRIMITIVE_RESTART_ENABLE,
        VKA_DYN_BLEND_CONSTANTS, VKA_DYN_LINE_WIDTH
    };
    uint32_t nd = 0u;
    for (size_t i = 0; i < sizeof base / sizeof *base; i++) {
        if (!mesh || (base[i] != VKA_DYN_PRIMITIVE_TOPOLOGY && base[i] != VKA_DYN_PRIMITIVE_RESTART_ENABLE)) {
            out[nd++] = base[i];
        }
    }
    if (!vin_static && !mesh) out[nd++] = VKA_DYN_VERTEX_INPUT_EXT;
    return nd;
}

static int ref_dyn_part(uint32_t d) {
    switch (d) {
    case VKA_DYN_VERTEX_INPUT_EXT:
    case VKA_DYN_PRIMITIVE_TOPOLOGY:
    case VKA_DYN_PRIMITIVE_RESTART_ENABLE: return 0;
    case VKA_DYN_VIEWPORT:
    case VKA_DYN_SCISSOR:
    case VKA_DYN_LINE_WIDTH:
    case VKA_DYN_DEPTH_BIAS:
    case VKA_DYN_DEPTH_BIAS_ENABLE:
    case VKA_DYN_CULL_MODE:
    case VKA_DYN_FRONT_FACE:
    case VKA_DYN_RASTERIZER_DISCARD_ENABLE: return 1;
    case VKA_DYN_BLEND_CONSTANTS: return 3;
    default: return 2;
    }
}

static void test_fmt_and_fout_parity(void) {
    for (uint32_t f = 0u; f < 256u; f++) {
        EXPECT_EQ_U64("vkf_int_class", ref_vkf_int_class(f), vka_vkf_int_class_fast(f));
    }
    uint32_t fmts[8] = {41u, 37u, 75u, 98u, 116u, 120u, 13u, 42u};
    for (uint32_t nc = 0u; nc <= 8u; nc++) {
        for (uint32_t fm = 0u; fm < 256u; fm += 17u) {
            uint32_t ft = (fm * 0x55u) & 0xffffu;
            uint32_t ru = 0u, rs = 0u, fu = 0u, fs = 0u;
            ref_fout_masks(fm, ft, fmts, nc, &ru, &rs);
            vka_fout_masks_fast(fm, ft, fmts, nc, &fu, &fs);
            EXPECT_EQ_U64("fout_u", ru, fu);
            EXPECT_EQ_U64("fout_s", rs, fs);
        }
    }
    uint32_t r5 = 0u, rs5 = 0u, f5 = 0u, fs5 = 0u;
    ref_fout_masks(0xffu, 0u, fmts, 8u, &r5, &rs5);
    vka_fout_masks_fast(0xffu, 0u, fmts, 8u, &f5, &fs5);
    EXPECT_EQ_U64("fout_u_all8", r5, f5);
    EXPECT_EQ_U64("fout_s_all8", rs5, fs5);
}

typedef struct {
    void *sh[8];
    uint64_t layout;
    uint8_t k[1024];
} w48_fake_gkey_t;

static void test_km_eq_and_last_key(void) {
    static const size_t lens[7] = {3u, 8u, 12u, 24u, 80u, 320u, 1120u};
    uint8_t a[1200], b[1200];
    for (size_t li = 0; li < 7u; li++) {
        size_t n = lens[li];
        for (size_t j = 0; j < n; j++) a[j] = (uint8_t)((j * 37u + 11u) & 0xffu);
        memcpy(b, a, n);
        EXPECT_EQ_U64("km_eq_same", 1, vka_km_eq_fast(a, b, n));
        for (size_t flip = 0; flip < n; flip += (n > 64u ? 19u : 1u)) {
            b[flip] ^= 0x01u;
            EXPECT_EQ_U64("km_eq_flip", 0, vka_km_eq_fast(a, b, n));
            b[flip] ^= 0x01u;
        }
        b[n - 1u] ^= 0x80u;
        EXPECT_EQ_U64("km_eq_tail", 0, vka_km_eq_fast(a, b, n));
    }
    w48_fake_gkey_t g1 = {0}, g2 = {0};
    void *sh[8] = {(void *)0x1000, (void *)0x2000};
    uint64_t lay1 = 0xdeadbeefULL, lay2 = 0xcafebabeULL;
    uint8_t kb[1024];
    for (size_t i = 0; i < sizeof kb; i++) kb[i] = (uint8_t)(i ^ 0x5au);
    memset(&g1, 0xaa, sizeof g1);
    vka_gkey_pack_fast(&g1, offsetof(w48_fake_gkey_t, layout), sizeof g1.layout,
                       offsetof(w48_fake_gkey_t, k), sh, sizeof sh, &lay1, kb, 256u);
    vka_gkey_pack_fast(&g2, offsetof(w48_fake_gkey_t, layout), sizeof g2.layout,
                       offsetof(w48_fake_gkey_t, k), sh, sizeof sh, &lay1, kb, 256u);
    EXPECT_EQ_U64("gkey_pack_cmp", 0, memcmp(&g1, &g2, offsetof(w48_fake_gkey_t, k) + 256u));
    EXPECT_EQ_U64("same_last_hit", 1,
        vka_gkey_same_last_fast(g1.sh, g1.layout, g1.k, sh, lay1, kb, sizeof sh, 256u));
    EXPECT_EQ_U64("same_last_lay_miss", 0,
        vka_gkey_same_last_fast(g1.sh, g1.layout, g1.k, sh, lay2, kb, sizeof sh, 256u));
    kb[120] ^= 0x04u;
    EXPECT_EQ_U64("same_last_k_miss", 0,
        vka_gkey_same_last_fast(g1.sh, g1.layout, g1.k, sh, lay1, kb, sizeof sh, 256u));
}

static void test_dyn_slices_parity(void) {
    uint32_t rfull[24], rsub[24];
    for (int mesh = 0; mesh <= 1; mesh++) {
        for (int vs = 0; vs <= 1; vs++) {
            uint32_t rn = ref_dyn_full(mesh, vs, rfull);
            uint32_t fn = 0u;
            const uint32_t *farr = vka_dyn_full_fast(mesh, vs, &fn);
            EXPECT_EQ_U64("dyn_full_n", rn, fn);
            for (uint32_t i = 0; i < rn; i++) EXPECT_EQ_U64("dyn_full_item", rfull[i], farr[i]);
            for (int part = 0; part < 5; part++) {
                uint32_t rsn = 0u, fsn = 0u;
                for (uint32_t i = 0; i < rn; i++) {
                    int p = ref_dyn_part(rfull[i]);
                    if (p == part || (part == 4 && (p == 1 || p == 2))) rsub[rsn++] = rfull[i];
                }
                const uint32_t *fsl = vka_dyn_slice_fast(mesh, vs, part, &fsn);
                EXPECT_EQ_U64("dyn_slice_n", rsn, fsn);
                for (uint32_t i = 0; i < rsn; i++) EXPECT_EQ_U64("dyn_slice_item", rsub[i], fsl[i]);
            }
        }
    }
}

typedef struct {
    vka_heavy_set_t *set;
    uint32_t tid;
    uint32_t errs;
} w48_thr_arg_t;

static void *w48_heavy_worker(void *arg) {
    w48_thr_arg_t *a = (w48_thr_arg_t *)arg;
    for (uint32_t i = 1u; i <= 256u; i++) {
        uint64_t k = (uint64_t)(a->tid + 1u) * UINT64_C(0x100000001b3) + i * UINT64_C(0x9e3779b9);
        vka_heavy_add_fast(a->set, k);
        if (!vka_heavy_has_fast(a->set, k)) a->errs++;
        if (vka_heavy_has_fast(a->set, k ^ UINT64_C(0xffffffff00000001))) a->errs++;
    }
    return NULL;
}

static void test_heavy_set_tsan(void) {
    vka_heavy_set_t s;
    memset(&s, 0, sizeof s);
    w48_thr_arg_t args[4];
    pthread_t th[4];
    for (uint32_t t = 0u; t < 4u; t++) {
        args[t] = (w48_thr_arg_t){&s, t, 0u};
        pthread_create(&th[t], NULL, w48_heavy_worker, &args[t]);
    }
    for (uint32_t t = 0u; t < 4u; t++) {
        pthread_join(th[t], NULL);
        EXPECT_EQ_U64("heavy_worker_errs", 0, args[t].errs);
    }
    for (uint32_t t = 0u; t < 4u; t++) {
        for (uint32_t i = 1u; i <= 256u; i++) {
            uint64_t k = (uint64_t)(t + 1u) * UINT64_C(0x100000001b3) + i * UINT64_C(0x9e3779b9);
            EXPECT_EQ_U64("heavy_final_has", 1, vka_heavy_has_fast(&s, k));
        }
    }
}

static uint64_t run_bench_pass(int fast, uint32_t iters) {
    vka_pso_key_fast_set_for_test(fast);
    static w48_fake_gkey_t g_last, g_fk;
    static const void *sh[8] = {(void *)0x1100, (void *)0x2200};
    uint64_t lay = 0x123456789abcdef0ULL, sink = 0u;
    uint32_t kcfmt[8] = {37u, 41u, 100u, 75u, 42u, 116u, 98u, 120u};
    uint8_t kbuf[256];
    memset(kbuf, 0x3cu, sizeof kbuf);
    vka_heavy_set_t hs;
    memset(&hs, 0, sizeof hs);
    uint64_t hkeys[16];
    for (int i = 0; i < 16; i++) {
        hkeys[i] = UINT64_C(0x5000000000000001) + (uint64_t)i * 7717u;
        vka_heavy_add_fast(&hs, hkeys[i]);
    }
    vka_gkey_pack_fast(&g_last, offsetof(w48_fake_gkey_t, layout), sizeof g_last.layout,
                       offsetof(w48_fake_gkey_t, k), sh, sizeof sh, &lay, kbuf, 256u);
    uint32_t dummy[24];
    for (uint32_t it = 0u; it < iters; it++) {
        if (fast) {
            if (vka_gkey_same_last_fast(g_last.sh, g_last.layout, g_last.k, sh, lay, kbuf, sizeof sh, 256u))
                sink += 1u;
            uint32_t um = 0u, sm = 0u, dn = 0u;
            vka_fout_masks_fast(0xc3u, 0u, kcfmt, 8u, &um, &sm);
            const uint32_t *sl = vka_dyn_slice_fast(0, 0, (int)(it & 3u), &dn);
            sink += um + sm + sl[0] + (uint64_t)vka_heavy_has_fast(&hs, hkeys[it & 15u] + ((it & 31u) ? 1u : 0u));
        } else {
            memset(&g_fk, 0, offsetof(w48_fake_gkey_t, k) + 256u);
            memcpy(g_fk.sh, sh, sizeof sh);
            g_fk.layout = lay;
            memcpy(g_fk.k, kbuf, 256u);
            if (!memcmp(&g_fk, &g_last, offsetof(w48_fake_gkey_t, k) + 256u)) sink += 1u;
            uint32_t um = 0u, sm = 0u, rn = ref_dyn_full(0, 0, dummy), dn = 0u;
            ref_fout_masks(0xc3u, 0u, kcfmt, 8u, &um, &sm);
            for (uint32_t j = 0; j < rn; j++) if (ref_dyn_part(dummy[j]) == (int)(it & 3u)) dummy[dn++] = dummy[j];
            int hit = 0;
            uint64_t probe = hkeys[it & 15u] + ((it & 31u) ? 1u : 0u);
            for (int j = 0; j < 16; j++) if (hkeys[j] == probe) { hit = 1; break; }
            sink += um + sm + dummy[0] + (uint64_t)hit;
        }
    }
    return sink;
}

int main(void) {
    const char *ef = getenv("AOI_W48_FAULT");
    if (ef && *ef) aoi_w48_set_fault(atoi(ef));
    vka_pso_key_fast_set_for_test(1);
    test_fmt_and_fout_parity();
    test_km_eq_and_last_key();
    test_dyn_slices_parity();
    test_heavy_set_tsan();
    uint32_t iters = 150000u;
    uint64_t t0 = w48_now_ns();
    uint64_t s_slow = run_bench_pass(0, iters);
    uint64_t dt_slow = w48_now_ns() - t0;
    uint64_t t1 = w48_now_ns();
    uint64_t s_fast = run_bench_pass(1, iters);
    uint64_t dt_fast = w48_now_ns() - t1;
    EXPECT_EQ_U64("bench_sink", s_slow, s_fast);
    printf("t_pso_key_fast_w48: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu %s\n",
           (double)dt_slow / 1e6, (double)dt_fast / 1e6,
           dt_fast ? (double)dt_slow / (double)dt_fast : 0.0,
           (unsigned long long)s_fast, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
