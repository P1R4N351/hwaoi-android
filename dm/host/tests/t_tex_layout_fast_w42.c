// Wave 42 (Optimization 44): parity, O(1) math, mip-slice, sparse bitmap/idle lock-free TSan, and fault injection tests.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/nvn/nvn_tex.h"
#include "../../gfx/nvn/nvn_tex_layout_fast.h"
#include "../../gfx/tex/nvn_fmt_mtl.h"

_Atomic int g_dm_tex_layout_fast_override = -1;
_Atomic int g_aoi_w42_fault = -1;
static int g_fail = 0;

#define EXPECT_EQ_U64(msg, a, b) do { \
    uint64_t va_ = (uint64_t)(a), vb_ = (uint64_t)(b); \
    if (va_ != vb_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s: got %llu vs %llu\n", __FILE__, __LINE__, msg, (unsigned long long)vb_, (unsigned long long)va_); \
    } \
} while (0)

static uint64_t w42_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int layout_equal(const nvn_texlayout *a, const nvn_texlayout *b) {
    return a->size == b->size && a->layer_size == b->layer_size &&
           a->align == b->align && a->layers == b->layers &&
           a->bpb == b->bpb && a->tileH == b->tileH && a->tileD == b->tileD;
}

static void check_fmt_and_bpb_parity(void) {
    for (int f = -16; f <= 512; f++) {
        dm_tex_layout_fast_set_for_test(0);
        int bw0 = -1, bh0 = -1, r0 = nvn_fmt_bpb(f, &bw0, &bh0);
        uint32_t m0 = 999u; uint8_t s0[4] = {9, 9, 9, 9};
        int k0 = nvn_fmt_to_mtl(f, &m0, s0);

        dm_tex_layout_fast_set_for_test(1);
        int bw1 = -2, bh1 = -2, r1 = nvn_fmt_bpb(f, &bw1, &bh1);
        uint32_t m1 = 888u; uint8_t s1[4] = {8, 8, 8, 8};
        int k1 = nvn_fmt_to_mtl(f, &m1, s1);

        EXPECT_EQ_U64("fmt_bpb", r0, r1);
        EXPECT_EQ_U64("fmt_bw", bw0, bw1);
        EXPECT_EQ_U64("fmt_bh", bh0, bh1);
        EXPECT_EQ_U64("fmt_mtl_k", k0, k1);
        EXPECT_EQ_U64("fmt_mtl", m0, m1);
        if (memcmp(s0, s1, 4u) != 0) { g_fail++; fprintf(stderr, "FAIL swz mismatch f=%d\n", f); }
    }
}

static void check_one_desc_parity(const nvn_texdesc *d, const char *tag) {
    unsigned nlev = d->levels ? d->levels : 1u;
    dm_tex_layout_fast_set_for_test(0);
    nvn_texlayout l0; nvn_tex_layout(d, &l0);
    uint32_t tw0 = 9, th0 = 9, td0 = 9;
    int st0 = nvn_tex_sparse_tiles(d, &tw0, &th0, &td0);
    dm_tex_layout_fast_set_for_test(1);
    nvn_texlayout l1; nvn_tex_layout(d, &l1);
    uint32_t tw1 = 8, th1 = 8, td1 = 8;
    int st1 = nvn_tex_sparse_tiles(d, &tw1, &th1, &td1);
    if (!layout_equal(&l0, &l1)) { g_fail++; fprintf(stderr, "FAIL layout mismatch on %s\n", tag); }
    EXPECT_EQ_U64("sparse_cnt", st0, st1);
    EXPECT_EQ_U64("sparse_tw", tw0, tw1);
    EXPECT_EQ_U64("sparse_th", th0, th1);
    EXPECT_EQ_U64("sparse_td", td0, td1);
    for (unsigned lv = 0u; lv < nlev && lv < 16u; lv++) {
        dm_tex_layout_fast_set_for_test(0);
        uint64_t o0 = nvn_tex_level_offset(d, lv);
        uint32_t g0 = nvn_tex_level_gobs(d, lv);
        unsigned lh0 = 9, ld0 = 9; nvn_tex_level_tiles(d, lv, &lh0, &ld0);
        dm_tex_layout_fast_set_for_test(1);
        uint64_t o1 = nvn_tex_level_offset(d, lv);
        uint32_t g1 = nvn_tex_level_gobs(d, lv);
        unsigned lh1 = 8, ld1 = 8; nvn_tex_level_tiles(d, lv, &lh1, &ld1);
        EXPECT_EQ_U64("lv_off", o0, o1);
        EXPECT_EQ_U64("lv_gobs", g0, g1);
        EXPECT_EQ_U64("lv_th", lh0, lh1);
        EXPECT_EQ_U64("lv_td", ld0, ld1);
    }
}

static void check_math_and_tbuf_parity(void) {
    static const uint32_t bs[] = {1u, 4u, 5u, 6u, 8u, 10u, 12u};
    for (size_t i = 0u; i < 7u; i++) {
        for (uint32_t s = 0u; s <= 128u; s++) {
            EXPECT_EQ_U64("blk_exact", (s + bs[i] - 1u) / bs[i], nvn_blk_fast(s, bs[i]));
        }
    }
    for (unsigned sh = 0u; sh <= 8u; sh++) {
        for (uint32_t d = 0u; d <= 260u; d++) {
            uint32_t th0 = sh, x0 = 8u << (sh ? sh - 1u : 0u);
            if (!sh) th0 = 0u;
            else if (x0 >= d) { while (--th0) { x0 >>= 1; if (x0 < d) break; } }
            EXPECT_EQ_U64("adj8", th0, nvn_adj_tile_fast(sh, 8u, d));
        }
    }
    for (uint32_t n = 1u; n <= 33000u; n += 137u) {
        for (uint32_t al = 16u; al <= 256u; al += 48u) {
            uint32_t w0 = 0, h0 = 0, r0 = 0, w1 = 0, h1 = 0, r1 = 0;
            dm_tex_layout_fast_set_for_test(0); int ok0 = nvn_tbuf_rows(n, 4u, al, &w0, &h0, &r0);
            dm_tex_layout_fast_set_for_test(1); int ok1 = nvn_tbuf_rows(n, 4u, al, &w1, &h1, &r1);
            EXPECT_EQ_U64("tbuf_ok", ok0, ok1);
            EXPECT_EQ_U64("tbuf_w", w0, w1);
            EXPECT_EQ_U64("tbuf_h", h0, h1);
            EXPECT_EQ_U64("tbuf_row", r0, r1);
        }
    }
}

static void check_tex_layout_matrix(void) {
    static const nvn_texdesc cases[] = {
        {1, 0x25, 0x0, 1, 1920, 1080, 1, 11, 0, 0},
        {1, 0x7d, 0x4, 1, 2052, 1026, 1, 8, 0, 0x404},
        {8, 0x7d, 0x4, 1, 1024, 1024, 6, 8, 0, 0x404},
        {4, 0x7d, 0x4, 1, 2052, 1026, 3, 8, 0, 0x404},
        {9, 0x25, 0x0, 1, 64, 64, 12, 7, 0, 0},
        {2, 0x49, 0x0, 1, 64, 64, 64, 7, 0, 0x41},
        {2, 0x25, 0x0, 1, 32, 32, 32, 1, 0, 0x13},
        {1, 0x25, 0x40, 1, 260, 260, 1, 1, 1152, 0},
        {1, 0x42, 0x10, 1, 64, 64, 1, 1, 128, 0},
        {1, 0x0d, 0x200, 1, 260, 260, 1, 9, 0, 0},
        {1, 0x3c, 0x200, 1, 320, 320, 1, 1, 0, 0},
        {1, 0x25, 0x200, 1, 64, 128, 1, 1, 0, 0},
        {1, 0x7d, 0x20, 1, 1024, 1024, 1, 11, 0, 0},
        {1, 0x48, 0x20, 1, 384, 384, 1, 9, 0, 0},
        {1, 0x25, 0x0, 4, 1280, 720, 1, 1, 0, 0},
        {0, 0x01, 0x0, 1, 256, 1, 1, 5, 0, 0},
        {3, 0x25, 0x0, 1, 64, 8, 1, 4, 0, 0},
        {10, 0x25, 0x0, 1, 4096, 1, 1, 1, 0, 0},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_one_desc_parity(&cases[i], "matrix");
    }
    check_math_and_tbuf_parity();
}

static void check_level_of_cases(void) {
    nvn_texdesc p = {.target = 1, .format = 0x33, .flags = 0xc, .samples = 1, .width = 7168, .height = 7168, .depth = 1, .levels = 4};
    nvn_texdesc c = {.target = 1, .format = 0x33, .flags = 0xc, .samples = 1, .width = 3584, .height = 3584, .depth = 1, .levels = 2};
    nvn_texdesc pw4 = {.target = 1, .format = 0x7d, .flags = 0x4, .samples = 1, .width = 2052, .height = 1026, .depth = 1, .levels = 4, .pkg_layout = 0x404};
    nvn_texdesc cw0 = {.target = 1, .format = 0x7d, .flags = 0x4, .samples = 1, .width = 1026, .height = 513, .depth = 1, .levels = 1, .pkg_layout = 0x004};
    unsigned k0 = 99u, k1 = 88u;
    uint64_t off1 = nvn_tex_level_offset(&p, 1u);
    dm_tex_layout_fast_set_for_test(0); int r0 = nvn_tex_level_of(&p, &c, off1, &k0);
    dm_tex_layout_fast_set_for_test(1); int r1 = nvn_tex_level_of(&p, &c, off1, &k1);
    EXPECT_EQ_U64("lvof_hit", r0, r1);
    EXPECT_EQ_U64("lvof_k", k0, k1);
    EXPECT_EQ_U64("lvof_k1", 1u, k1);
    uint64_t off_pkg = nvn_tex_level_offset(&pw4, 1u);
    dm_tex_layout_fast_set_for_test(0); int mr0 = nvn_tex_level_of(&pw4, &cw0, off_pkg, &k0);
    dm_tex_layout_fast_set_for_test(1); int mr1 = nvn_tex_level_of(&pw4, &cw0, off_pkg, &k1);
    EXPECT_EQ_U64("lvof_pkg_mismatch", 0, mr0);
    EXPECT_EQ_U64("lvof_pkg_parity", mr0, mr1);
}

typedef struct {
    nvn_sparse_fast_state_t st;
    _Atomic int stop;
    _Atomic int err;
} sparse_tsan_ctx_t;

static void *sparse_tsan_reader(void *arg) {
    sparse_tsan_ctx_t *ctx = (sparse_tsan_ctx_t *)arg;
    for (int i = 0; i < 10000 && !atomic_load_explicit(&ctx->stop, memory_order_acquire); i++) {
        uintptr_t s = 0; uint64_t v = 0;
        if (nvn_sparse_try_idle(&ctx->st, 0, &s, &v) && s && (v == 0u || v > 2000u || s != (uintptr_t)0x5555u)) {
            atomic_store_explicit(&ctx->err, 1, memory_order_release);
        }
    }
    return NULL;
}

static void check_sparse_helpers_and_tsan(void) {
    uint64_t bits[16] = {0}; uint8_t cur = 0u;
    for (int i = 0; i < 1024; i++) EXPECT_EQ_U64("bmap_take", i, nvn_sparse_bmap_take(bits, &cur));
    EXPECT_EQ_U64("bmap_full", -1, nvn_sparse_bmap_take(bits, &cur));
    EXPECT_EQ_U64("bmap_give", 1, nvn_sparse_bmap_give(bits, &cur, 317u));
    EXPECT_EQ_U64("bmap_double_give", 0, nvn_sparse_bmap_give(bits, &cur, 317u));
    EXPECT_EQ_U64("bmap_retake", 317, nvn_sparse_bmap_take(bits, &cur));

    nvn_sparse_fast_state_t st = {0}; uintptr_t out_s = 1; uint64_t out_v = 1;
    EXPECT_EQ_U64("idle_empty", 1, nvn_sparse_try_idle(&st, 0, &out_s, &out_v));
    EXPECT_EQ_U64("idle_empty_v", 0u, out_v);
    nvn_sparse_publish(&st, (uintptr_t)0x1234u, 77u, 0u);
    EXPECT_EQ_U64("idle_clean", 1, nvn_sparse_try_idle(&st, 0, &out_s, &out_v));
    EXPECT_EQ_U64("idle_clean_v", 77u, out_v);
    nvn_sparse_publish(&st, (uintptr_t)0x1234u, 77u, 1u);
    EXPECT_EQ_U64("idle_pending", 0, nvn_sparse_try_idle(&st, 0, &out_s, &out_v));

    sparse_tsan_ctx_t ctx = {.st = {0}, .stop = 0, .err = 0};
    pthread_t th[3];
    for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, sparse_tsan_reader, &ctx);
    for (uint64_t step = 1u; step <= 2000u; step++) {
        nvn_sparse_publish(&ctx.st, (uintptr_t)0x5555u, step, 1u);
        nvn_sparse_publish(&ctx.st, (uintptr_t)0x5555u, step, 0u);
    }
    atomic_store_explicit(&ctx.stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
    EXPECT_EQ_U64("sparse_tsan_err", 0u, (uint64_t)atomic_load_explicit(&ctx.err, memory_order_acquire));
}

static uint64_t bench_layout_work(int fast_mode) {
    dm_tex_layout_fast_set_for_test(fast_mode);
    nvn_texdesc p = {.target = 1, .format = 0x7d, .flags = 0x4, .samples = 1, .width = 2048, .height = 2048, .depth = 1, .levels = 8, .pkg_layout = 0x404};
    nvn_texdesc c = {.target = 1, .format = 0x7d, .flags = 0x4, .samples = 1, .width = 512, .height = 512, .depth = 1, .levels = 4, .pkg_layout = 0x404};
    uint64_t off2 = nvn_tex_level_offset(&p, 2u), sink = 0u;
    for (uint32_t i = 0u; i < 10000u; i++) {
        int fm = 0x79 + (int)(i % 28u);
        int bw = 0, bh = 0;
        sink += (uint64_t)nvn_fmt_bpb(fm, &bw, &bh);
        uint32_t mtl = 0u; uint8_t swz[4] = {0};
        sink += (uint64_t)nvn_fmt_to_mtl(fm, &mtl, swz) + mtl + swz[0];
        sink += nvn_tex_level_offset(&p, (i & 7u));
        sink += nvn_tex_level_gobs(&p, (i & 7u));
        unsigned k = 0u;
        if (nvn_tex_level_of(&p, &c, off2, &k)) sink += k;
    }
    dm_tex_layout_fast_set_for_test(-1);
    return sink;
}

int main(void) {
    dm_tex_layout_fast_set_for_test(1);
    check_fmt_and_bpb_parity();
    check_tex_layout_matrix();
    check_level_of_cases();
    check_sparse_helpers_and_tsan();
    uint64_t t0 = w42_now_ns(), s0 = bench_layout_work(0), t1 = w42_now_ns();
    uint64_t t2 = w42_now_ns(), s1 = bench_layout_work(1), t3 = w42_now_ns();
    EXPECT_EQ_U64("bench_sink", s0, s1);
    double slow_ms = (double)(t1 - t0) / 1e6, fast_ms = (double)(t3 - t2) / 1e6;
    printf("t_tex_layout_fast_w42: slow_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
           (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
