#define _POSIX_C_SOURCE 200809L
#define W15_TEST_HOOKS 1
#include "../vk_android/vka_tbuf_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w15_fault = 0;
static volatile uint64_t g_w15_sink;

typedef struct { uint64_t addr, elems, fmt, bv, last; int is_2d, dirty; uint64_t cframe, iv; } sim_tbuf;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static void test_view_faults(void) {
    vka_tbuf_view_memo4 vm;
    memset(&vm, 0, sizeof vm);
    uint64_t ep0 = vka_tbuf_epoch_mix(1ull, 1u, 0ull, 1u, 0u);
    uint64_t ep1 = vka_tbuf_epoch_mix(1ull, 1u, 0ull, 1u, 1u);
    assert(ep0 != ep1);
    vka_tbuf_view_memo4_put(&vm, 7u, 1, 1u, ep0, 0xaaaa1000ull, 0x200000ull, 4096ull, 2u);

    uint64_t bv = 0, addr = 0, range = 0;
    uint16_t slot = 0;
    assert(vka_tbuf_view_memo4_find(&vm, 7u, 1, 1u, ep0, &bv, &addr, &range, &slot) == 1);
    assert(bv == 0xaaaa1000ull && slot == 2u);

    /* Fault 1: stale epoch (eviction / seg_gen / pool_fin) must miss. */
    if (vka_tbuf_view_memo4_find(&vm, 7u, 1, 1u, ep1, &bv, &addr, &range, &slot) != 0) {
        fprintf(stderr, "FAIL: stale epoch returned retired buffer view\n");
        exit(1);
    }
    /* Fault 2: stale tver after texture ID re-registration must miss. */
    if (vka_tbuf_view_memo4_find(&vm, 7u, 1, 2u, ep0, &bv, &addr, &range, &slot) != 0) {
        fprintf(stderr, "FAIL: stale tver returned prior registration view\n");
        exit(1);
    }
}

static void test_img_and_fmt_faults(void) {
    vka_tbuf_img_memo4 im;
    memset(&im, 0, sizeof im);
    uint64_t ep = vka_tbuf_epoch_mix(2ull, 3u, 1ull, 1u, 0u);
    vka_tbuf_img_memo4_put(&im, 19u, 1, 5u, 100ull, 10ull, ep, 0xbbbb2000ull, 3u);

    uint64_t iv = 0;
    uint16_t slot = 0;
    assert(vka_tbuf_img_memo4_find(&im, 19u, 1, 5u, 100ull, 10ull, ep, &iv, &slot) == 1);
    assert(iv == 0xbbbb2000ull && slot == 3u);

    /* Fault 3: stale write_gen (vka_tbuf_wrote) or frame must miss. */
    if (vka_tbuf_img_memo4_find(&im, 19u, 1, 5u, 100ull, 11ull, ep, &iv, &slot) != 0) {
        fprintf(stderr, "FAIL: stale write_gen returned unrefreshed buffer image\n");
        exit(1);
    }

    vka_tbuf_fmt_memo fm;
    memset(&fm, 0, sizeof fm);
    uint32_t feat = 0;
    vka_tbuf_fmt_memo_put(&fm, 0x1000ull, 44u, 0x30u);
    assert(vka_tbuf_fmt_memo_get(&fm, 0x1000ull, 44u, &feat) == 1 && feat == 0x30u);
    assert(vka_tbuf_fmt_memo_get(&fm, 0x2000ull, 44u, &feat) == 0);
}

__attribute__((noinline)) static uint64_t sim_view_baseline(
    pthread_mutex_t *m_treg, pthread_mutex_t *m_pool, pthread_mutex_t *m_view,
    sim_tbuf *tab, int n, uint64_t addr, uint64_t elems, uint64_t fmt, uint64_t fr) {
    uint64_t res = 0;
    pthread_mutex_lock(m_treg);
    uint64_t a = addr + (elems & 1u);
    pthread_mutex_unlock(m_treg);
    pthread_mutex_lock(m_pool);
    res ^= a;
    pthread_mutex_unlock(m_pool);
    pthread_mutex_lock(m_view);
    for (int i = 0; i < n; i++) {
        if (tab[i].addr == a && tab[i].elems == elems && tab[i].fmt == fmt) {
            tab[i].last = fr;
            res = tab[i].bv;
            break;
        }
    }
    pthread_mutex_unlock(m_view);
    return res;
}

__attribute__((noinline)) static uint64_t sim_view_fast(
    const vka_tbuf_view_memo4 *vm, _Atomic uint64_t *last_arr, uint32_t id,
    const _Atomic uint32_t *tver_arr, uint64_t ep, uint64_t fr) {
    uint32_t tv = atomic_load_explicit(&tver_arr[id], memory_order_acquire);
    uint64_t bv = 0, a = 0, rg = 0;
    uint16_t slot = 0;
    if (vka_tbuf_view_memo4_find(vm, id, 0, tv, ep, &bv, &a, &rg, &slot)) {
        if (atomic_load_explicit(&last_arr[slot], memory_order_relaxed) != fr) {
            atomic_store_explicit(&last_arr[slot], fr, memory_order_relaxed);
        }
        return bv;
    }
    return 0;
}

static void bench_view(void) {
    static pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t m3 = PTHREAD_MUTEX_INITIALIZER;
    sim_tbuf tab[32];
    vka_tbuf_view_memo4 vm;
    memset(&vm, 0, sizeof vm);
    _Atomic uint32_t tvers[64];
    _Atomic uint64_t lasts[64];
    uint64_t ep = vka_tbuf_epoch_mix(1ull, 1u, 0ull, 1u, 0u);
    for (uint32_t i = 0; i < 32u; i++) {
        atomic_init(&tvers[i], 1u);
        atomic_init(&lasts[i], 0ull);
        tab[i] = (sim_tbuf){0x400000ull + i * 4096u, 256u, 44u, 0x800000ull + i, 0u, 1, 0, 1u, 0u};
        vka_tbuf_view_memo4_put(&vm, i, 0, 1u, ep, tab[i].bv, tab[i].addr, 4096u, (uint16_t)i);
    }
    uint64_t sb = 0, so = 0;
    double t0 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        uint32_t k = i & 31u;
        sb += sim_view_baseline(&m1, &m2, &m3, tab, 32, tab[k].addr, 256u, 44u, 10ull);
    }
    double ms_b = now_ms() - t0;
    double t1 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        so += sim_view_fast(&vm, lasts, i & 31u, tvers, ep, 10ull);
    }
    double ms_o = now_ms() - t1;
    assert(sb == so);
    g_w15_sink ^= sb;
    printf("aoi_w15_tbuf_view: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
}

__attribute__((noinline)) static uint64_t sim_img_baseline(
    pthread_mutex_t *m_treg, pthread_mutex_t *m_img, sim_tbuf *tab, int n,
    uint64_t addr, uint64_t elems, uint64_t fmt, uint64_t fr) {
    pthread_mutex_lock(m_treg);
    uint64_t a = addr + (elems & 1u);
    pthread_mutex_unlock(m_treg);
    uint64_t pend = 1, iv = 0;
    pthread_mutex_lock(m_img);
    for (int i = 0; i < n; i++) {
        if (tab[i].addr == a && tab[i].elems == elems && tab[i].fmt == fmt && tab[i].is_2d) {
            pend = (!tab[i].cframe || tab[i].cframe != fr || tab[i].dirty);
            if (!pend) { tab[i].last = fr; iv = tab[i].iv; }
            break;
        }
    }
    pthread_mutex_unlock(m_img);
    return iv ^ pend;
}

__attribute__((noinline)) static uint64_t sim_img_fast(
    const vka_tbuf_img_memo4 *im, _Atomic uint64_t *last_arr, uint32_t id,
    const _Atomic uint32_t *tver_arr, uint64_t fr, uint64_t wgen, uint64_t ep) {
    uint32_t tv = atomic_load_explicit(&tver_arr[id], memory_order_acquire);
    uint64_t iv = 0;
    uint16_t slot = 0;
    if (vka_tbuf_img_memo4_find(im, id, 1, tv, fr, wgen, ep, &iv, &slot)) {
        if (atomic_load_explicit(&last_arr[slot], memory_order_relaxed) != fr) {
            atomic_store_explicit(&last_arr[slot], fr, memory_order_relaxed);
        }
        return iv;
    }
    return 1;
}

static void bench_img(void) {
    static pthread_mutex_t m1 = PTHREAD_MUTEX_INITIALIZER;
    static pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
    sim_tbuf tab[32];
    vka_tbuf_img_memo4 im;
    memset(&im, 0, sizeof im);
    _Atomic uint32_t tvers[64];
    _Atomic uint64_t lasts[64];
    uint64_t ep = vka_tbuf_epoch_mix(1ull, 1u, 0ull, 1u, 0u);
    for (uint32_t i = 0; i < 32u; i++) {
        atomic_init(&tvers[i], 1u);
        atomic_init(&lasts[i], 0ull);
        tab[i] = (sim_tbuf){0x500000ull + i * 4096u, 512u, 44u, 0u, 0u, 1, 0, 10ull, 0x900000ull + i};
        vka_tbuf_img_memo4_put(&im, i, 1, 1u, 10ull, 1ull, ep, tab[i].iv, (uint16_t)i);
    }
    uint64_t sb = 0, so = 0;
    double t0 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        uint32_t k = i & 31u;
        sb += sim_img_baseline(&m1, &m2, tab, 32, tab[k].addr, 512u, 44u, 10ull);
    }
    double ms_b = now_ms() - t0;
    double t1 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        so += sim_img_fast(&im, lasts, i & 31u, tvers, 10ull, 1ull, ep);
    }
    double ms_o = now_ms() - t1;
    assert(sb == so);
    g_w15_sink ^= sb;
    printf("aoi_w15_tbuf_img: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
}

int main(void) {
    const char *f = getenv("T_W15_FAULT");
    g_w15_fault = f ? atoi(f) : 0;
    assert(vka_tbuf_fast_on() == 0 || vka_tbuf_fast_on() == 1);
    test_view_faults();
    test_img_and_fmt_faults();
    bench_view();
    bench_img();
    printf("t_tbuf_fast_w15: fault=%d PASS\n", g_w15_fault);
    return 0;
}
