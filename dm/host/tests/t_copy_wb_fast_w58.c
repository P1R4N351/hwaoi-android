// Equivalence, fault-injection, TSan & -O2 microbenchmark for DM_VK_COPY_WB_FAST (Wave 58).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "vka_copy_wb_fast.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_vka_copy_wb_fast_override = -1;
_Atomic int g_aoi_w58_fault = -1;

static void req(int ok) {
    if (!ok) abort();
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int oracle_mem_any(const uint8_t *p, uint64_t n) {
    for (uint64_t i = 0u; i < n; i++) if (p[i]) return 1;
    return 0;
}

static void oracle_memcpy_2d(uint8_t *dst, size_t drow, const uint8_t *src, size_t srow,
                             size_t wb, size_t rows) {
    for (size_t y = 0u; y < rows; y++) memcpy(dst + y * drow, src + y * srow, wb);
}

static void check_wb_and_mem_any(void) {
    req(vka_wb_has_live(0) == 0);
    req(vka_wb_has_live(1) == 1);
    req(vka_wb_has_live(7) == 1);

    uint8_t raw[320];
    for (unsigned off = 0u; off < 16u; off++) {
        const uint8_t *p = raw + off;
        for (uint64_t len = 0u; len <= 192u; len += 7u) {
            memset(raw, 0, sizeof raw);
            req(vka_mem_any_fast(p, len) == 0);
            if (len > 0u) {
                uint64_t pos[3] = {0u, len >> 1, len - 1u};
                for (int k = 0; k < 3; k++) {
                    memset(raw, 0, sizeof raw);
                    raw[off + pos[k]] = (uint8_t)(k + 1);
                    req(vka_mem_any_fast(p, len) == oracle_mem_any(p, len));
                }
            }
        }
    }
    memset(raw, 0, 32u);
    raw[17] = 9u;
    req(vka_block16_is_zero(raw) == 1);
    req(vka_block16_is_zero(raw + 16) == 0);
}

static void check_copy3d_and_gates(void) {
    vka_copy3d_plan_t p0 = vka_copy3d_plan(128u, 16u, 4u, 128u, 2048u, 128u, 2048u);
    req(p0.units == 1u && p0.unit_bytes == 8192u);
    vka_copy3d_plan_t p1 = vka_copy3d_plan(128u, 16u, 4u, 128u, 4096u, 128u, 2048u);
    req(p1.units == 4u && p1.unit_bytes == 2048u && p1.src_step == 4096u && p1.dst_step == 2048u);
    vka_copy3d_plan_t p2 = vka_copy3d_plan(120u, 8u, 3u, 128u, 2048u, 120u, 960u);
    req(p2.units == 24u && p2.unit_bytes == 120u);

    uint8_t src[512], dst_ref[512], dst_fast[512];
    for (size_t i = 0u; i < sizeof src; i++) src[i] = (uint8_t)(i * 19u + 3u);
    memset(dst_ref, 0xaa, sizeof dst_ref);
    memset(dst_fast, 0xaa, sizeof dst_fast);
    oracle_memcpy_2d(dst_ref, 32u, src, 32u, 32u, 8u);
    vka_memcpy_2d(dst_fast, 32u, src, 32u, 32u, 8u);
    req(memcmp(dst_ref, dst_fast, sizeof dst_ref) == 0);

    memset(dst_ref, 0x55, sizeof dst_ref);
    memset(dst_fast, 0x55, sizeof dst_fast);
    oracle_memcpy_2d(dst_ref, 40u, src, 48u, 24u, 6u);
    vka_memcpy_2d(dst_fast, 40u, src, 48u, 24u, 6u);
    req(memcmp(dst_ref, dst_fast, sizeof dst_ref) == 0);

    uint64_t mf = UINT64_MAX;
    req(vka_carry_has_due(0, mf, 100u) == 0);
    vka_carry_min_note(&mf, 12u);
    vka_carry_min_note(&mf, 9u);
    req(mf == 9u && vka_carry_has_due(2, mf, 8u) == 0 && vka_carry_has_due(2, mf, 9u) == 1);

    _Atomic uint64_t cgen = 0u;
    req(vka_contig_need_scan(5u, atomic_load_explicit(&cgen, memory_order_acquire)) == 1);
    vka_contig_note_clean(&cgen, 5u);
    req(vka_contig_need_scan(5u, atomic_load_explicit(&cgen, memory_order_acquire)) == 0);
    req(vka_contig_need_scan(6u, atomic_load_explicit(&cgen, memory_order_acquire)) == 1);
}

typedef struct {
    int tid;
    _Atomic uint64_t *sg_gen;
    _Atomic uint64_t *clean_gen;
} w58_thr_arg_t;

static void *w58_thread_fn(void *arg) {
    w58_thr_arg_t *ta = (w58_thr_arg_t *)arg;
    uint8_t blk[64];
    for (int it = 0; it < 200; it++) {
        memset(blk, 0, sizeof blk);
        if (it % 11 == 0) blk[(unsigned)(it + ta->tid) & 63u] = 1u;
        req(vka_mem_any_fast(blk, 64u) == (it % 11 == 0));
        req(vka_block16_is_zero(blk) == !(it % 11 == 0 && (((unsigned)(it + ta->tid) & 63u) < 16u)));
        if (ta->tid == 0) atomic_fetch_add_explicit(ta->sg_gen, 1u, memory_order_release);
        else {
            uint64_t sg = atomic_load_explicit(ta->sg_gen, memory_order_acquire);
            uint64_t cg = atomic_load_explicit(ta->clean_gen, memory_order_acquire);
            if (vka_contig_need_scan(sg, cg)) vka_contig_note_clean(ta->clean_gen, sg);
        }
    }
    return NULL;
}

__attribute__((noinline)) static uint64_t bench_base(const uint8_t *zbuf, uint64_t nbytes,
                                                     uint8_t *dst, const uint8_t *src, uint32_t it) {
    uint64_t acc = 0u, v = 0u;
    int any = 0;
    for (uint64_t i = 0u; i + 8u <= nbytes; i += 8u) {
        memcpy(&v, zbuf + i, 8u);
        if (v) { any = 1; break; }
    }
    acc += (uint64_t)any;
    size_t wb = 64u, rows = 256u;
    for (size_t y = 0u; y < rows; y++) memcpy(dst + y * wb, src + y * wb, wb);
    acc += dst[(it * 13u) & 4095u];
    return acc;
}

__attribute__((noinline)) static uint64_t bench_fast(const uint8_t *zbuf, uint64_t nbytes,
                                                     uint8_t *dst, const uint8_t *src, uint32_t it) {
    uint64_t acc = (uint64_t)vka_mem_any_fast(zbuf, nbytes);
    vka_memcpy_2d(dst, 64u, src, 64u, 64u, 256u);
    acc += dst[(it * 13u) & 4095u];
    return acc;
}

static void run_bench(void) {
    static uint8_t zbuf[16384], src[16384], dst0[16384], dst1[16384];
    memset(zbuf, 0, sizeof zbuf);
    for (size_t i = 0u; i < sizeof src; i++) src[i] = (uint8_t)((i * 7u) & 0xffu);
    enum { ITERS = 4000 };
    uint64_t sink0 = 0u, sink1 = 0u;
    uint64_t t0 = now_ns();
    for (uint32_t it = 0u; it < ITERS; it++) {
        zbuf[16380u] = (uint8_t)(it & 1u);
        src[it & 4095u] ^= 1u;
        sink0 += bench_base(zbuf, sizeof zbuf, dst0, src, it);
    }
    uint64_t dt0 = now_ns() - t0;
    for (size_t i = 0u; i < sizeof src; i++) src[i] = (uint8_t)((i * 7u) & 0xffu);
    uint64_t t1 = now_ns();
    for (uint32_t it = 0u; it < ITERS; it++) {
        zbuf[16380u] = (uint8_t)(it & 1u);
        src[it & 4095u] ^= 1u;
        sink1 += bench_fast(zbuf, sizeof zbuf, dst1, src, it);
    }
    uint64_t dt1 = now_ns() - t1;
    req(sink0 == sink1 && memcmp(dst0, dst1, sizeof dst0) == 0);
    double bms = (double)dt0 / 1e6, fms = (double)dt1 / 1e6;
    printf("t_copy_wb_fast_w58: base=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=PASS\n",
           bms, fms, fms > 0.0 ? bms / fms : 1.0, (unsigned long long)sink1);
}

int main(void) {
    vka_copy_wb_fast_set_for_test(1);
    aoi_w58_set_fault(-1);
    check_wb_and_mem_any();
    check_copy3d_and_gates();
    _Atomic uint64_t sg_gen = 1u, clean_gen = 0u;
    pthread_t th[4];
    w58_thr_arg_t ta[4];
    for (int i = 0; i < 4; i++) {
        ta[i] = (w58_thr_arg_t){i, &sg_gen, &clean_gen};
        req(pthread_create(&th[i], NULL, w58_thread_fn, &ta[i]) == 0);
    }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    req(vka_copy_wb_fast_on() == 1);
    vka_copy_wb_fast_set_for_test(0);
    req(vka_copy_wb_fast_on() == 0);
    vka_copy_wb_fast_set_for_test(1);
    run_bench();
    return 0;
}
