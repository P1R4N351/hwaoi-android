/* Host unit test, benchmark & negative control for Wave 21 (Opt 23: DM_VK_SERIAL_FAST=1). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w21_fault = 0;
#include "../vk_android/vka_serial_fast.h"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

typedef struct {
    uint64_t serial, vep;
    uint32_t vkfmt;
    uint8_t gpu_owned, valid;
} mock_img_t;

enum { MOCK_N = 1024u, ID_N = 256u };
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static mock_img_t g_tbl[MOCK_N];
static _Atomic uint32_t g_sgen = 1u, g_ogen = 1u, g_tver[ID_N];
static uint32_t g_id_fmt[ID_N];

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void tbl_seed(void) {
    memset(g_tbl, 0, sizeof g_tbl);
    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t idx = (i * 13u) & (MOCK_N - 1u);
        g_tbl[idx] = (mock_img_t){
            .serial = UINT64_C(0x10000) + i * 3u,
            .vep = 1u, .vkfmt = 37u + (i & 7u),
            .gpu_owned = (uint8_t)(i & 1u), .valid = 1u,
        };
    }
    for (uint32_t id = 1u; id < ID_N; id++) {
        g_id_fmt[id] = 40u + (id & 15u);
        atomic_store_explicit(&g_tver[id], 1u, memory_order_relaxed);
    }
}

static uintptr_t locked_smap_get(uint64_t serial) {
    pthread_mutex_lock(&g_mu);
    uintptr_t out = 0;
    for (uint32_t i = 0; i < MOCK_N; i++) {
        if (g_tbl[i].valid && g_tbl[i].serial == serial) {
            out = (uintptr_t)&g_tbl[i]; break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return out;
}

static int locked_gpu_owned(uint64_t serial) {
    pthread_mutex_lock(&g_mu);
    int r = 0;
    for (uint32_t i = 0; i < MOCK_N; i++) {
        if (g_tbl[i].valid && g_tbl[i].serial == serial) {
            r = g_tbl[i].gpu_owned; break;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return r;
}

static uint32_t locked_id_vkfmt(uint32_t id) {
    pthread_mutex_lock(&g_mu);
    uint32_t f = (id > 0 && id < ID_N) ? g_id_fmt[id] : 0u;
    pthread_mutex_unlock(&g_mu);
    return f;
}

static int test_correctness(void) {
    vka_smap_memo4 sm = {0};
    vka_gpu_own_memo4 gm = {0};
    vka_vkfmt_memo4 fm = {0};
    uint64_t ser = UINT64_C(0x10000);
    uintptr_t im = locked_smap_get(ser), got = 0;
    uint32_t s0 = atomic_load_explicit(&g_sgen, memory_order_acquire);
    vka_smap_memo4_put(&sm, ser, s0, 1u, im);
    CHECK(vka_smap_memo4_get(&sm, ser, s0, 1u, &got) && got == im, "smap hit");
    CHECK(!vka_smap_memo4_get(&sm, ser, s0, 2u, &got), "smap vep miss");
    uint32_t s1 = atomic_fetch_add_explicit(&g_sgen, 1u, memory_order_acq_rel) + 1u;
    CHECK(!vka_smap_memo4_get(&sm, ser, s1, 1u, &got), "smap sgen miss");

    uint32_t o0 = atomic_load_explicit(&g_ogen, memory_order_acquire);
    vka_gpu_own_memo4_put(&gm, ser, o0, 0u);
    uint8_t own = 9u;
    CHECK(vka_gpu_own_memo4_get(&gm, ser, o0, &own) && own == 0u, "gpu_own hit");
    uint32_t o1 = atomic_fetch_add_explicit(&g_ogen, 1u, memory_order_acq_rel) + 1u;
    CHECK(!vka_gpu_own_memo4_get(&gm, ser, o1, &own), "gpu_own ogen miss");

    uint32_t v0 = atomic_load_explicit(&g_tver[7], memory_order_acquire), fk = 0u;
    vka_vkfmt_memo4_put(&fm, 7u, v0, 44u);
    CHECK(vka_vkfmt_memo4_get(&fm, 7u, v0, &fk) && fk == 44u, "vkfmt hit");
    uint32_t v1 = atomic_fetch_add_explicit(&g_tver[7], 1u, memory_order_acq_rel) + 1u;
    CHECK(!vka_vkfmt_memo4_get(&fm, 7u, v1, &fk), "vkfmt tver miss");
    return 0;
}

static int bench_smap(void) {
    vka_smap_memo4 sm = {0};
    enum { ITERS = 400000 };
    uint64_t leg = 0, fast = 0, t0 = ns_now();
    for (int i = 0; i < ITERS; i++) {
        uint64_t ser = UINT64_C(0x10000) + (uint64_t)((i * 7) & 31) * 3u;
        leg ^= (uint64_t)locked_smap_get(ser);
    }
    uint64_t leg_ns = ns_now() - t0;
    t0 = ns_now();
    for (int i = 0; i < ITERS; i++) {
        uint64_t ser = UINT64_C(0x10000) + (uint64_t)((i * 7) & 31) * 3u;
        uint32_t sg = atomic_load_explicit(&g_sgen, memory_order_acquire);
        uintptr_t im = 0;
        if (!vka_smap_memo4_get(&sm, ser, sg, 1u, &im)) {
            im = locked_smap_get(ser);
            vka_smap_memo4_put(&sm, ser, sg, 1u, im);
        }
        fast ^= (uint64_t)im;
    }
    uint64_t fast_ns = ns_now() - t0;
    printf("aoi_w21_smap_get: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6,
           fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    CHECK(leg == fast, "smap oracle diff");
    return 0;
}

static int bench_gpu_own_fmt(void) {
    vka_gpu_own_memo4 gm = {0};
    vka_vkfmt_memo4 fm = {0};
    enum { ITERS = 400000 };
    uint64_t leg = 0, fast = 0, t0 = ns_now();
    for (int i = 0; i < ITERS; i++) {
        uint64_t ser = UINT64_C(0x10000) + (uint64_t)((i * 5) & 31) * 3u;
        uint32_t id = (uint32_t)(1 + (i & 31));
        leg += (uint64_t)locked_gpu_owned(ser) + (uint64_t)locked_id_vkfmt(id);
    }
    uint64_t leg_ns = ns_now() - t0;
    t0 = ns_now();
    for (int i = 0; i < ITERS; i++) {
        uint64_t ser = UINT64_C(0x10000) + (uint64_t)((i * 5) & 31) * 3u;
        uint32_t id = (uint32_t)(1 + (i & 31));
        uint32_t og = atomic_load_explicit(&g_ogen, memory_order_acquire);
        uint32_t tv = atomic_load_explicit(&g_tver[id], memory_order_acquire);
        uint8_t own = 0;
        uint32_t fmt = 0;
        if (!vka_gpu_own_memo4_get(&gm, ser, og, &own)) {
            own = (uint8_t)locked_gpu_owned(ser);
            vka_gpu_own_memo4_put(&gm, ser, og, own);
        }
        if (!vka_vkfmt_memo4_get(&fm, id, tv, &fmt)) {
            fmt = locked_id_vkfmt(id);
            vka_vkfmt_memo4_put(&fm, id, tv, fmt);
        }
        fast += (uint64_t)own + (uint64_t)fmt;
    }
    uint64_t fast_ns = ns_now() - t0;
    printf("aoi_w21_gpu_own_fmt: iters=%d leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           ITERS, leg_ns / 1e6, fast_ns / 1e6,
           fast_ns ? (double)leg_ns / (double)fast_ns : 0.0,
           (unsigned long long)(leg ^ fast));
    CHECK(leg == fast, "gpu_own_fmt oracle diff");
    return 0;
}

int main(void) {
    const char *f = getenv("T_W21_FAULT");
    g_w21_fault = f ? atoi(f) : 0;
    tbl_seed();
    if (test_correctness() != 0) return 1;
    if (bench_smap() != 0) return 1;
    if (bench_gpu_own_fmt() != 0) return 1;
    printf("t_serial_fast_w21: PASS (fault=%d)\n", g_w21_fault);
    return 0;
}
