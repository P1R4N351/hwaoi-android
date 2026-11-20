/* Host unit, benchmark, and Delta #27 fault-injection test for Wave 6:
 *   1) nvn_deswizzle.c / nvn_deswizzle_fast.h (DM_DESWIZZLE_FAST=1)
 *   2) nvn_dirty_fast.h (dirty_rix augmented interval index + 64-page cmax) */
#define _POSIX_C_SOURCE 200809L
#define W6_TEST_HOOKS 1
#include "../tex/nvn_deswizzle.h"
#include "../nvn/nvn_dirty_fast.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void nvn_deswizzle_w6_set_fast(int on);

static uint64_t w6_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void fill_pattern(uint8_t *buf, size_t n, uint32_t seed) {
    uint32_t x = seed ? seed : 0x12345678u;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        buf[i] = (uint8_t)x;
    }
}

static void check_deswz_case(const nvn_texdesc *d, int sw2, uint32_t y0, uint32_t y1) {
    nvn_texlayout L;
    nvn_gob_switch2 = sw2;
    nvn_tex_layout(d, &L);
    uint8_t *st_ref = calloc(1u, (size_t)L.size + 64u);
    uint8_t *st_fast = calloc(1u, (size_t)L.size + 64u);
    assert(st_ref && st_fast);
    for (unsigned l = 0; l < d->levels; l++) {
        uint32_t rb, rows, sl, bpb; uint64_t base; unsigned th, td;
        assert(nvn_deswizzle_geom(d, l, 0, &rb, &rows, &sl, &base, &th, &td, &bpb) == 0);
        size_t pitch = (size_t)rb + 16u, lin_sz = pitch * rows * sl;
        uint8_t *src = malloc(lin_sz), *out0 = calloc(1u, lin_sz), *out1 = calloc(1u, lin_sz);
        assert(src && out0 && out1);
        fill_pattern(src, lin_sz, (uint32_t)(d->width * 31u + l * 7u + (unsigned)sw2));
        nvn_deswizzle_w6_set_fast(0);
        assert(nvn_swizzle(d, l, 0, src, pitch, st_ref) == 0);
        assert(nvn_deswizzle_rows(d, l, 0, st_ref, out0, pitch, y0, y1) == 0);
        nvn_deswizzle_w6_set_fast(1);
        assert(nvn_swizzle(d, l, 0, src, pitch, st_fast) == 0);
        assert(memcmp(st_ref, st_fast, (size_t)L.size) == 0);
        assert(nvn_deswizzle_rows(d, l, 0, st_fast, out1, pitch, y0, y1) == 0);
        assert(memcmp(out0, out1, lin_sz) == 0);
        free(src); free(out0); free(out1);
    }
    free(st_ref); free(st_fast);
}

static void bench_deswz_one(const char *tag, int fmt, uint32_t w, uint32_t h, int sw2, int iters) {
    nvn_texdesc d = {.target = 1, .format = fmt, .width = w, .height = h, .depth = 1, .levels = 1};
    nvn_texlayout L;
    nvn_gob_switch2 = sw2;
    nvn_tex_layout(&d, &L);
    uint32_t rb, rows, sl, bpb; uint64_t base; unsigned th, td;
    assert(nvn_deswizzle_geom(&d, 0, 0, &rb, &rows, &sl, &base, &th, &td, &bpb) == 0);
    size_t lin_sz = (size_t)rb * rows;
    uint8_t *st = calloc(1u, (size_t)L.size), *lin = malloc(lin_sz);
    assert(st && lin);
    fill_pattern(lin, lin_sz, w ^ (uint32_t)fmt);
    assert(nvn_swizzle(&d, 0, 0, lin, rb, st) == 0);
    volatile uint8_t sink = 0;

    nvn_deswizzle_w6_set_fast(0);
    uint64_t t0 = w6_now_ns();
    for (int i = 0; i < iters; i++) { assert(nvn_deswizzle(&d, 0, 0, st, lin, rb) == 0); sink ^= lin[(size_t)i & (lin_sz - 1u)]; }
    uint64_t dt0 = w6_now_ns() - t0;

    nvn_deswizzle_w6_set_fast(1);
    t0 = w6_now_ns();
    for (int i = 0; i < iters; i++) { assert(nvn_deswizzle(&d, 0, 0, st, lin, rb) == 0); sink ^= lin[(size_t)i & (lin_sz - 1u)]; }
    uint64_t dt1 = w6_now_ns() - t0;
    (void)sink;
    printf("aoi_deswz[%s]: iters=%d bytes=%zu legacy_ms=%.2f fast_ms=%.2f speedup=%.2fx\n",
           tag, iters, lin_sz, dt0 / 1e6, dt1 / 1e6, dt1 ? (double)dt0 / (double)dt1 : 0.0);
    free(st); free(lin);
}

typedef struct { uintptr_t base, end; } mock_pool;

static void verify_dirty_cmax_unit(void) {
    enum { NPAGES = 4096 };
    _Atomic uint64_t *gen = calloc(NPAGES, sizeof *gen);
    _Atomic uint64_t *cmax = calloc(NPAGES >> 6, sizeof *cmax);
    assert(gen && cmax);
    uint64_t stamp = 10u;
    atomic_store(&gen[0], 11u);
    dirty_max_up(&cmax[0], 11u);
    assert(dirty_since_cmax(gen, cmax, 64u, NPAGES - 1u, stamp) == 0);
    atomic_store(&gen[NPAGES - 5u], 12u);
    if (dirty_w6_fault() != 4) dirty_max_up(&cmax[(NPAGES - 5u) >> 6], 12u);
    assert(dirty_since_cmax(gen, cmax, 64u, NPAGES - 1u, stamp) == 1);
    uintptr_t f = 0, l = 0;
    assert(dirty_bounds_cmax(gen, cmax, 64u, NPAGES - 1u, stamp, &f, &l) == 1);
    assert(f == NPAGES - 5u && l == NPAGES - 5u);
    free((void *)gen); free((void *)cmax);
}

static void verify_and_bench_dirty(void) {
    enum { NP = 5000, Q = 200000 };
    mock_pool *pools = calloc((size_t)NP, sizeof *pools);
    assert(pools != NULL);
    pools[0] = (mock_pool){UINT64_C(0x10000000), UINT64_C(0x10004000)};
    pools[1] = (mock_pool){UINT64_C(0x10000000), UINT64_C(0x1000C000)};
    dirty_rix_insert(0, pools[0].base, pools[0].end);
    dirty_rix_insert(1, pools[1].base, pools[1].end);
    for (int i = 2; i < NP; i++) {
        uintptr_t b = UINT64_C(0x20000000) + (uintptr_t)i * UINT64_C(0x10000);
        pools[i] = (mock_pool){b, b + UINT64_C(0x8000)};
        dirty_rix_insert(i, pools[i].base, pools[i].end);
    }
    int slots[DIRTY_RIX_OUT], trusted = 0;
    int m = dirty_rix_collect(UINT64_C(0x10000000), 0u, UINT64_C(0x1000C000), 1, slots, DIRTY_RIX_OUT, &trusted);
    assert(trusted == 1 && ((m >= 1 && slots[0] == 1) || (m >= 2 && slots[1] == 1)));
    int cov = dirty_rix_collect(UINT64_C(0x10001000), UINT64_C(0x10001001), UINT64_C(0x10001000), 0, slots, DIRTY_RIX_OUT, &trusted);
    assert(trusted == 1 && cov == 2);

    volatile uint64_t sink = 0;
    uint64_t t0 = w6_now_ns();
    for (int q = 0; q < Q; q++) {
        int want = 2 + ((q * 131 + 17) % (NP - 2));
        uintptr_t a = pools[want].base;
        for (int i = 0; i < NP; i++) {
            uintptr_t rb = __atomic_load_n(&pools[i].base, __ATOMIC_RELAXED);
            uintptr_t re = __atomic_load_n(&pools[i].end, __ATOMIC_RELAXED);
            if (a >= rb && a + 0x4000 <= re) { sink += (uint64_t)i; break; }
        }
    }
    uint64_t dt_scan = w6_now_ns() - t0;

    t0 = w6_now_ns();
    for (int q = 0; q < Q; q++) {
        int want = 2 + ((q * 131 + 17) % (NP - 2));
        uintptr_t a = pools[want].base;
        m = dirty_rix_collect(a, 0u, a + 0x4000, 1, slots, DIRTY_RIX_OUT, &trusted);
        assert(m >= 1 && slots[0] == want);
        sink += (uint64_t)slots[0];
    }
    uint64_t dt_rix = w6_now_ns() - t0;
    for (int i = 0; i < NP; i++) dirty_rix_remove(i, NP - 1 - i);
    verify_dirty_cmax_unit();
    (void)sink;
    printf("aoi_dirty_rix: pools=%d queries=%d scan_ms=%.2f rix_ms=%.2f speedup=%.2fx\n",
           NP, Q, dt_scan / 1e6, dt_rix / 1e6, dt_rix ? (double)dt_scan / (double)dt_rix : 0.0);
    free(pools);
}

int main(void) {
    const nvn_texdesc cases[] = {
        {.target = 1, .format = 0x25, .width = 197, .height = 149, .depth = 1, .levels = 4}, /* odd RGBA8 + mips */
        {.target = 1, .format = 0x19, .width = 83,  .height = 37,  .depth = 1, .levels = 2}, /* RGB8 non-16B tail */
        {.target = 1, .format = 0x4D, .width = 259, .height = 131, .depth = 1, .levels = 3}, /* odd BC7 */
        {.target = 2, .format = 0x25, .width = 64,  .height = 33,  .depth = 5, .levels = 2}, /* 3D texture */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_deswz_case(&cases[i], 0, 0u, UINT32_MAX);
        check_deswz_case(&cases[i], 1, 0u, UINT32_MAX);
        check_deswz_case(&cases[i], 0, 3u, 29u);
    }
    bench_deswz_one("1024_rgba8_x1", 0x25, 1024u, 1024u, 0, 200);
    bench_deswz_one("1024_bc7_x1  ", 0x4D, 1024u, 1024u, 0, 600);
    bench_deswz_one("1024_rgba8_s2", 0x25, 1024u, 1024u, 1, 200);
    verify_and_bench_dirty();
    printf("t_dirty_deswz_w6: fault=%d PASS\n", dirty_w6_fault());
    return 0;
}
