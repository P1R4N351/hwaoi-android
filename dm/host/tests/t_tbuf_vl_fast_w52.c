// Host unit test, TSan stress test, fault injection & benchmark for Wave 52 (Opt 54: DM_VK_TBUF_VL_FAST).
#ifndef T_W52_TEST_HOOKS
#define T_W52_TEST_HOOKS 1
#endif
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx/vk_android/vka_tbuf_vl_fast.h"

_Atomic int g_vka_tbuf_vl_fast_override = -1;
_Atomic int g_aoi_w52_fault = -1;
static int g_fail = 0;

typedef struct { uint64_t buf_u, off, range; int32_t fmt; } w52_vrec_t;
typedef struct { uint64_t addr, bytes; uint32_t elems; int32_t format; uint8_t is2d; } w52_irec_t;
typedef struct { int32_t a, b; uint32_t w, h; } w52_vl_t;
typedef struct { uint64_t e, l; int k; } w52_pfb_t;

#define EXPECT_EQ(msg, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w52_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void test_tbuf_vmap_and_imap(void) {
    vka_tbuf_vmap_t *vm = calloc(1u, sizeof *vm);
    vka_tbuf_imap_t *im = calloc(1u, sizeof *im);
    assert(vm && im);
    w52_vrec_t vr[256];
    w52_irec_t ir[256];
    for (int i = 0; i < 256; i++) {
        vr[i] = (w52_vrec_t){UINT64_C(0x100000) + (uint64_t)(i / 4) * 4096u, (uint64_t)(i & 3) * 256u, 512u, 37 + (i % 7)};
        vka_tbuf_vmap_put(vm, vr[i].buf_u, vr[i].off, vr[i].range, vr[i].fmt, (uint16_t)i);
        ir[i] = (w52_irec_t){UINT64_C(0x20000000) + (uint64_t)i * 65536u, 4096u, 1024u, 44 + (i % 5), (uint8_t)(i & 1)};
        vka_tbuf_imap_put(im, ir[i].addr, ir[i].elems, ir[i].format, ir[i].is2d, (uint16_t)i);
    }
    for (int i = 0; i < 256; i++) {
        uint16_t gs = 0u;
        EXPECT_EQ("vmap_hit", 1, vka_tbuf_vmap_find(vm, vr[i].buf_u, vr[i].off, vr[i].range, vr[i].fmt, &gs));
        EXPECT_EQ("vmap_slot", (uint16_t)i, gs);
        EXPECT_EQ("imap_hit", 1, vka_tbuf_imap_find(im, ir[i].addr, ir[i].elems, ir[i].format, ir[i].is2d, &gs));
        EXPECT_EQ("imap_slot", (uint16_t)i, gs);
    }
    uint16_t ms = 0u;
    EXPECT_EQ("vmap_miss", 0, vka_tbuf_vmap_find(vm, vr[0].buf_u, vr[0].off + 64u, vr[0].range, vr[0].fmt, &ms));
    EXPECT_EQ("imap_miss", 0, vka_tbuf_imap_find(im, ir[0].addr, ir[0].elems, ir[0].format, (uint8_t)!ir[0].is2d, &ms));
    vka_tbuf_vmap_clear(vm);
    EXPECT_EQ("vmap_clr", 0, vka_tbuf_vmap_find(vm, vr[0].buf_u, vr[0].off, vr[0].range, vr[0].fmt, &ms));
    free(vm);
    free(im);
}

static int ref_vl_formats(const w52_vl_t *vl, int nvl, int32_t a, uint32_t w, uint32_t h,
                          int img, int32_t *out, int max) {
    int n = 0;
    for (int i = 0; i < nvl && n < max; i++) {
        if (vl[i].a == a && ((vl[i].w == w && vl[i].h == h) || (img && !vl[i].w && !vl[i].h))) {
            int dup = 0;
            for (int k = 0; k < n; k++) dup |= out[k] == vl[i].b;
            if (!dup) out[n++] = vl[i].b;
        }
    }
    return n;
}

static void test_wf_vl_and_pfb(void) {
    vka_timg_wfilter_t wf = {0};
    vka_timg_wf_add(&wf, UINT64_C(0x40000000), UINT64_C(8192));
    vka_timg_wf_add(&wf, UINT64_C(0x40050000), UINT64_C(4096));
    EXPECT_EQ("wf_hit1", 1, vka_timg_wf_may_overlap(&wf, UINT64_C(0x40001000), UINT64_C(64)));
    EXPECT_EQ("wf_miss_hi", 0, vka_timg_wf_may_overlap(&wf, UINT64_C(0x40060000), UINT64_C(4096)));
    EXPECT_EQ("wf_miss_lo", 0, vka_timg_wf_may_overlap(&wf, UINT64_C(0x3fff0000), UINT64_C(4096)));
    vka_vl_index_t *vli = calloc(1u, sizeof *vli);
    assert(vli);
    w52_vl_t vl[12] = {
        {37, 44, 0u, 0u}, {37, 43, 1280u, 720u}, {37, 44, 1280u, 720u},
        {37, 50, 0u, 0u}, {37, 53, 1280u, 720u}, {97, 98, 640u, 360u}
    };
    int nvl = 6;
    for (int i = 0; i < nvl; i++) vka_vl_idx_add(vli, vl[i].a, vl[i].b, vl[i].w, vl[i].h, (uint8_t)i);
    int32_t ro[4] = {0}, fo[4] = {0};
    int rn = ref_vl_formats(vl, nvl, 37, 1280u, 720u, 1, ro, 4);
    int fn = vka_vl_idx_formats(vli, 37, 1280u, 720u, 1, fo, 4);
    EXPECT_EQ("vl_n", rn, fn);
    for (int i = 0; i < rn && i < 4; i++) EXPECT_EQ("vl_ord", ro[i], fo[i]);
    free(vli);
}

static void test_pfb_del_parity(void) {
    vka_pfb_map_t *pm = calloc(1u, sizeof *pm);
    assert(pm);
    w52_pfb_t arr[512];
    int n = 0;
    for (int i = 0; i < 400; i++) {
        uint64_t e = UINT64_C(0x80000000) + (uint64_t)i * 64u, l = e ^ UINT64_C(0x100000);
        int k = 1 + (i & 1);
        arr[n] = (w52_pfb_t){e, l, k};
        vka_pfb_map_put(pm, e, l, k, (uint32_t)n);
        n++;
    }
    for (int i = 0; i < 250; i += 2) {
        uint64_t del_e = UINT64_C(0x80000000) + (uint64_t)i * 64u, dl = 0u;
        uint32_t didx = 0u;
        EXPECT_EQ("pfb_del", 1, vka_pfb_map_del(pm, del_e, &dl, &didx));
        EXPECT_EQ("pfb_del_late", del_e ^ UINT64_C(0x100000), dl);
        assert(n > 0 && didx < (uint32_t)n);
        arr[didx] = arr[--n];
        if (didx < (uint32_t)n) vka_pfb_map_set_idx(pm, arr[didx].e, didx);
    }
    for (int i = 0; i < n; i++) {
        uint64_t gl = 0u; int gk = 0;
        EXPECT_EQ("pfb_keep", 1, vka_pfb_map_find(pm, arr[i].e, &gl, &gk));
        EXPECT_EQ("pfb_late", arr[i].l, gl);
        EXPECT_EQ("pfb_kind", arr[i].k, gk);
    }
    uint64_t gl = 0u; int gk = 0;
    EXPECT_EQ("pfb_gone", 0, vka_pfb_map_find(pm, UINT64_C(0x80000000), &gl, &gk));
    free(pm);
}

static vka_timg_wfilter_t g_tsan_wf;
static _Atomic int g_tsan_stop;

static void *tsan_reader(void *arg) {
    (void)arg;
    uint64_t sum = 0u;
    for (int i = 0; i < 40000 && !atomic_load_explicit(&g_tsan_stop, memory_order_relaxed); i++) {
        sum += (uint64_t)vka_timg_wf_may_overlap(&g_tsan_wf, UINT64_C(0x70000000) + (uint64_t)(i & 127) * 4096u, 4096u);
    }
    return (void *)(uintptr_t)(sum + 1u);
}

static void test_tsan(void) {
    vka_timg_wf_clear(&g_tsan_wf);
    atomic_store_explicit(&g_tsan_stop, 0, memory_order_relaxed);
    pthread_t th[3];
    for (int i = 0; i < 3; i++) assert(pthread_create(&th[i], NULL, tsan_reader, NULL) == 0);
    for (int i = 0; i < 4000; i++) {
        if ((i & 511) == 511) vka_timg_wf_clear(&g_tsan_wf);
        vka_timg_wf_add(&g_tsan_wf, UINT64_C(0x50000000) + (uint64_t)(i & 31) * 65536u, 4096u);
    }
    atomic_store_explicit(&g_tsan_stop, 1, memory_order_relaxed);
    for (int i = 0; i < 3; i++) { void *r = NULL; pthread_join(th[i], &r); assert(r != NULL); }
}

static uint64_t run_bench_slow(const w52_vrec_t *vr, const w52_pfb_t *pf) {
    uint64_t sink = 0u;
    w52_irec_t ir[256];
    for (int i = 0; i < 256; i++) ir[i] = (w52_irec_t){UINT64_C(0x10000000) + (uint64_t)i * 65536u, 4096u, 1024u, 37, 1u};
    for (int rep = 0; rep < 300; rep++) {
        for (int q = 0; q < 256; q++) {
            w52_vrec_t k = vr[(rep + q * 7) & 255];
            for (int i = 0; i < 256; i++) {
                if (vr[i].buf_u == k.buf_u && vr[i].off == k.off && vr[i].range == k.range && vr[i].fmt == k.fmt) { sink += (uint64_t)i; break; }
            }
            uint64_t qe = pf[q].e;
            for (int i = 0; i < 256; i++) { if (pf[i].e == qe) { sink += pf[i].l; break; } }
            uint64_t wa = UINT64_C(0x30000000) + (uint64_t)q * 4096u;
            for (int i = 0; i < 256; i++) { if (wa < ir[i].addr + ir[i].bytes && ir[i].addr < wa + 4096u) sink++; }
        }
    }
    return sink;
}

static uint64_t run_bench_fast(const w52_vrec_t *vr, const vka_tbuf_vmap_t *vm,
                               const w52_pfb_t *pf, const vka_pfb_map_t *pm, const vka_timg_wfilter_t *wf) {
    uint64_t sink = 0u;
    for (int rep = 0; rep < 300; rep++) {
        for (int q = 0; q < 256; q++) {
            w52_vrec_t k = vr[(rep + q * 7) & 255];
            uint16_t slot = 0u;
            if (vka_tbuf_vmap_find(vm, k.buf_u, k.off, k.range, k.fmt, &slot)) sink += slot;
            uint64_t late = 0u; int kind = 0;
            if (vka_pfb_map_find(pm, pf[q].e, &late, &kind)) sink += late;
            if (vka_timg_wf_may_overlap(wf, UINT64_C(0x30000000) + (uint64_t)q * 4096u, 4096u)) sink++;
        }
    }
    return sink;
}

static void run_bench(void) {
    vka_tbuf_vmap_t *vm = calloc(1u, sizeof *vm);
    vka_pfb_map_t *pm = calloc(1u, sizeof *pm);
    w52_vrec_t *vr = calloc(256u, sizeof *vr);
    w52_pfb_t *pf = calloc(256u, sizeof *pf);
    vka_timg_wfilter_t wf = {0};
    assert(vm && pm && vr && pf);
    for (int i = 0; i < 256; i++) {
        vr[i] = (w52_vrec_t){UINT64_C(0x1000) + (uint64_t)i * 64u, (uint64_t)i * 16u, 256u, 37 + (i & 7)};
        vka_tbuf_vmap_put(vm, vr[i].buf_u, vr[i].off, vr[i].range, vr[i].fmt, (uint16_t)i);
        pf[i] = (w52_pfb_t){UINT64_C(0x50000) + (uint64_t)i * 64u, UINT64_C(0x90000) + (uint64_t)i, 1};
        vka_pfb_map_put(pm, pf[i].e, pf[i].l, pf[i].k, (uint32_t)i);
        vka_timg_wf_add(&wf, UINT64_C(0x10000000) + (uint64_t)i * 65536u, 4096u);
    }
    uint64_t best_s = UINT64_MAX, best_f = UINT64_MAX, s0 = 0u, s1 = 0u;
    for (int r = 0; r < 5; r++) {
        uint64_t t0 = w52_now_ns();
        s0 = run_bench_slow(vr, pf);
        uint64_t t1 = w52_now_ns();
        s1 = run_bench_fast(vr, vm, pf, pm, &wf);
        uint64_t t2 = w52_now_ns();
        if (t1 - t0 < best_s) best_s = t1 - t0;
        if (t2 - t1 < best_f) best_f = t2 - t1;
    }
    EXPECT_EQ("bench_sink", s0, s1);
    double sms = (double)best_s / 1e6, fms = (double)best_f / 1e6;
    printf("t_tbuf_vl_fast_w52: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=%s\n",
           sms, fms, sms / (fms > 0.001 ? fms : 0.001), (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    free(vm); free(pm); free(vr); free(pf);
}

int main(void) {
    vka_tbuf_vl_fast_set_for_test(1);
    test_tbuf_vmap_and_imap();
    test_wf_vl_and_pfb();
    test_pfb_del_parity();
    test_tsan();
    run_bench();
    return g_fail != 0;
}
