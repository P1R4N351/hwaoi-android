/* Wave 22 (Opt 24): Unit, benchmark & fault-injection test for DM_VK_IMG_FAST=1. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "../vk_android/vka_img_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int g_w22_fault = 0;
int g_w22_fast_override = 1;

static uint64_t w22_ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

enum { W22_IDS = 64 };
typedef struct {
    uint64_t pfin, buf_addr;
    uint32_t id, ver, gen, elems;
    int32_t target, fmt;
    uint8_t storage_ready, is_buf, gpu_owned, mz;
    uint8_t edge_ix, edge_unpinned, wb_kind, valid;
    void *im, *v;
} w22_rec_t;

static pthread_mutex_t g_w22_tmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_w22_fmu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t g_w22_fence = 10u;
static w22_rec_t g_w22_tbl[W22_IDS];

static uint64_t w22_fence_last(int fast) {
    if (fast) return atomic_load_explicit(&g_w22_fence, memory_order_acquire);
    pthread_mutex_lock(&g_w22_fmu);
    uint64_t f = atomic_load_explicit(&g_w22_fence, memory_order_relaxed);
    pthread_mutex_unlock(&g_w22_fmu);
    return f;
}

static void w22_seed_table(void) {
    for (uint32_t i = 0; i < W22_IDS; i++) {
        w22_rec_t *r = &g_w22_tbl[i];
        r->id = i + 1u; r->ver = 1u; r->gen = 1u; r->pfin = 1u;
        r->is_buf = (uint8_t)((i & 3u) == 0u);
        r->buf_addr = UINT64_C(0x70000000) + (uint64_t)i * 4096u;
        r->elems = 256u + i; r->fmt = 0x25; r->target = r->is_buf ? 10 : 1;
        r->storage_ready = r->is_buf ? 0u : 1u; r->gpu_owned = 1u; r->valid = 1u;
        r->im = (void *)(uintptr_t)(UINT64_C(0x40000) + i * 64u);
        r->v = (void *)(uintptr_t)(UINT64_C(0x80000) + i * 64u);
    }
}

typedef struct { _Atomic int stop; _Atomic uint64_t max_seen; } w22_farg_t;

static void *w22_fence_reader(void *p) {
    w22_farg_t *a = (w22_farg_t *)p;
    uint64_t prev = 0u;
    while (!atomic_load_explicit(&a->stop, memory_order_acquire)) {
        uint64_t f = w22_fence_last(1);
        if (f < prev) abort();
        prev = f;
    }
    atomic_store_explicit(&a->max_seen, prev, memory_order_release);
    return NULL;
}

static int w22_test_fence_mt(void) {
    w22_farg_t arg;
    atomic_init(&arg.stop, 0); atomic_init(&arg.max_seen, 0);
    pthread_t th[3];
    for (int i = 0; i < 3; i++) assert(pthread_create(&th[i], NULL, w22_fence_reader, &arg) == 0);
    for (int i = 0; i < 4000; i++) {
        pthread_mutex_lock(&g_w22_fmu);
        uint64_t nf = atomic_load_explicit(&g_w22_fence, memory_order_relaxed) + 1u;
        atomic_store_explicit(&g_w22_fence, nf, memory_order_release);
        pthread_mutex_unlock(&g_w22_fmu);
    }
    atomic_store_explicit(&arg.stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
    return w22_fence_last(1) < 4010u;
}

static int w22_test_sview(void) {
    vka_sview_memo4 *sv = calloc(1, sizeof *sv);
    assert(sv != NULL);
    void *im1 = (void *)(uintptr_t)0x1100, *v1 = (void *)(uintptr_t)0x2100;
    vka_sview_memo4_put(sv, 5u, 1u, 0u, 1u, 10u, 3u, im1, v1);
    const vka_sv_ent *e = vka_sview_memo4_find(sv, 5u, 1u, 0u, 1u, 10u, 3u);
    if (!e || e->v != v1) { free(sv); return 1; }
    if (vka_sview_memo4_find(sv, 5u, 1u, 0u, 1u, 11u, 3u) != NULL) { free(sv); return 1; }
    if (vka_sview_memo4_find(sv, 5u, 1u, 0u, 2u, 10u, 3u) != NULL) { free(sv); return 1; }
    if (vka_sview_memo4_find(sv, 5u, 1u, 0u, 1u, 10u, 4u) != NULL) { free(sv); return 1; }
    if (vka_sview_memo4_find(sv, 5u, 1u, 1u, 1u, 10u, 3u) != NULL) { free(sv); return 1; }
    free(sv);
    return 0;
}

static int w22_test_tmeta_and_steady(void) {
    vka_tmeta_memo4 *tm = calloc(1, sizeof *tm);
    assert(tm != NULL);
    vka_tmeta_memo4_put_storage_ready(tm, 7u, 1u, 1u, 1u);
    if (!vka_tmeta_memo4_get_storage_ready(tm, 7u, 1u, 1u, 1u)) { free(tm); return 1; }
    vka_tmeta_memo4_put_target(tm, 7u, 1u, 1u, 1u, 10);
    int tgt = -1;
    if (!vka_tmeta_memo4_get_target(tm, 7u, 1u, 1u, 1u, &tgt) || tgt != 10) { free(tm); return 1; }
    if (vka_tmeta_memo4_get_target(tm, 7u, 2u, 1u, 1u, &tgt)) { free(tm); return 1; }
    vka_tmeta_memo4_put_buf(tm, 7u, 1u, 1u, 1u, 1, 0x8000u, 64u, 0x25);
    int ib = 0, fmt = 0; uint64_t ad = 0; uint32_t el = 0;
    if (!vka_tmeta_memo4_get_buf(tm, 7u, 1u, 1u, 1u, &ib, &ad, &el, &fmt) || !ib) { free(tm); return 1; }
    if (vka_tmeta_memo4_get_buf(tm, 7u, 1u, 1u, 2u, &ib, &ad, &el, &fmt)) { free(tm); return 1; }
    free(tm);
    if (!vka_img_steady_gpu_owned(1, 0, 0, 0, 0)) return 1;
    if (vka_img_steady_gpu_owned(1, 1, 0, 0, 0)) return 1;
    if (vka_img_steady_gpu_owned(1, 0, 0, 0, 1)) return 1;
    return vka_img_steady_gpu_owned(0, 0, 0, 0, 0);
}

static uint64_t w22_legacy_sview_pend(uint32_t id) {
    pthread_mutex_lock(&g_w22_tmu);
    const w22_rec_t *r = &g_w22_tbl[(id - 1u) % W22_IDS];
    uint64_t res = r->storage_ready ? (uint64_t)(uintptr_t)r->v : 0u;
    pthread_mutex_unlock(&g_w22_tmu);
    return res;
}

static uint64_t w22_legacy_written_tbuf(uint32_t id) {
    pthread_mutex_lock(&g_w22_tmu);
    w22_rec_t *r = &g_w22_tbl[(id - 1u) % W22_IDS];
    uint64_t fn = w22_fence_last(0);
    uint64_t res = r->is_buf ? (r->buf_addr ^ (uint64_t)r->elems ^ fn) : fn;
    pthread_mutex_unlock(&g_w22_tmu);
    return res;
}

static int w22_bench_sview_pend(void) {
    vka_sview_memo4 *sv = calloc(1, sizeof *sv);
    vka_tmeta_memo4 *tm = calloc(1, sizeof *tm);
    assert(sv != NULL && tm != NULL);
    enum { ITERS = 300000 };
    uint64_t leg = 0, fast = 0, t0 = w22_ns_now();
    for (int i = 0; i < ITERS; i++) leg ^= w22_legacy_sview_pend(1u + (uint32_t)(i & 15));
    uint64_t leg_ns = w22_ns_now() - t0;
    t0 = w22_ns_now();
    for (int i = 0; i < ITERS; i++) {
        uint32_t id = 1u + (uint32_t)(i & 15);
        int sr = vka_tmeta_memo4_get_storage_ready(tm, id, 1u, 1u, 1u);
        const vka_sv_ent *e = sr ? vka_sview_memo4_find(sv, id, 1u, 0u, 1u, 1u, 1u) : NULL;
        if (!e) {
            uint64_t v = w22_legacy_sview_pend(id);
            if (v) {
                vka_tmeta_memo4_put_storage_ready(tm, id, 1u, 1u, 1u);
                vka_sview_memo4_put(sv, id, 1u, 0u, 1u, 1u, 1u, g_w22_tbl[(id - 1u) % W22_IDS].im, (void *)(uintptr_t)v);
            }
            fast ^= v;
        } else fast ^= (uint64_t)(uintptr_t)e->v;
    }
    uint64_t fast_ns = w22_ns_now() - t0;
    free(sv); free(tm);
    printf("aoi_w22_sview_pend: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6, fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    return leg != fast;
}

static int w22_bench_written_tbuf(void) {
    vka_tmeta_memo4 *tm = calloc(1, sizeof *tm);
    assert(tm != NULL);
    enum { ITERS = 300000 };
    uint64_t leg = 0, fast = 0, t0 = w22_ns_now();
    for (int i = 0; i < ITERS; i++) leg ^= w22_legacy_written_tbuf(1u + (uint32_t)(i & 31));
    uint64_t leg_ns = w22_ns_now() - t0;
    t0 = w22_ns_now();
    for (int i = 0; i < ITERS; i++) {
        uint32_t id = 1u + (uint32_t)(i & 31);
        const w22_rec_t *r = &g_w22_tbl[(id - 1u) % W22_IDS];
        int ib = 0, fmt = 0; uint64_t ad = 0; uint32_t el = 0;
        if (!vka_img_steady_gpu_owned(r->gpu_owned, r->mz, r->edge_ix, r->edge_unpinned, r->wb_kind) ||
            !vka_tmeta_memo4_get_buf(tm, id, 1u, 1u, 1u, &ib, &ad, &el, &fmt)) {
            uint64_t slow = w22_legacy_written_tbuf(id);
            vka_tmeta_memo4_put_buf(tm, id, 1u, 1u, 1u, r->is_buf, r->buf_addr, r->elems, r->fmt);
            fast ^= slow;
        } else {
            uint64_t fn = w22_fence_last(1);
            fast ^= ib ? (ad ^ (uint64_t)el ^ fn) : fn;
        }
    }
    uint64_t fast_ns = w22_ns_now() - t0;
    free(tm);
    printf("aoi_w22_steady_written_tbuf: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6, fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    return leg != fast;
}

int main(void) {
    const char *f = getenv("T_W22_FAULT");
    g_w22_fault = f ? atoi(f) : 0;
    w22_seed_table();
    if (w22_test_fence_mt() != 0) return 1;
    if (w22_test_sview() != 0) return 1;
    if (w22_test_tmeta_and_steady() != 0) return 1;
    if (w22_bench_sview_pend() != 0) return 1;
    if (w22_bench_written_tbuf() != 0) return 1;
    printf("t_img_fast_w22: PASS (fault=%d)\n", g_w22_fault);
    return 0;
}
