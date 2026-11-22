// Wave 43 (Optimization 45): parity, sliding fence window, evpeak skip, park mask, gw hash, granule bitmask, TSan, and fault tests.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/vk_android/vka_qsched.h"

_Atomic int g_vka_qsched_fast_override = -1;
_Atomic int g_aoi_w43_fault = -1;
static int g_fail = 0;

#define EXPECT_EQ_U64(msg, a, b) do { \
    uint64_t va_ = (uint64_t)(a), vb_ = (uint64_t)(b); \
    if (va_ != vb_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s: got %llu vs %llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)vb_, (unsigned long long)va_); \
    } \
} while (0)

static uint64_t w43_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct {
    qs *sc;
    volatile uint32_t *wait_w;
    uint64_t now;
    uint64_t done;
    uint32_t ran;
    int park_once;
} w43_sim_ctx;

static uint64_t w43_done_cb(void *ctx) { return ((const w43_sim_ctx *)ctx)->done; }
static uint64_t w43_now_cb(void *ctx) { return ((const w43_sim_ctx *)ctx)->now; }
static int w43_gate_cb(void *ctx, qs_sub *s, volatile uint32_t *w, uint32_t m, uint32_t v) {
    (void)ctx; (void)s; (void)w; (void)m; (void)v;
    return 1;
}
static int w43_run_cb(void *ctx, qs_sub *s) {
    w43_sim_ctx *c = (w43_sim_ctx *)ctx;
    if (c->park_once && !s->resumed && c->wait_w) {
        if (!qs_wait_event(c->sc, c->wait_w, 0u, 1u)) { s->resumed = 1; qs_commit(c->sc, s->fence, 0); return 1; }
    }
    c->ran++;
    qs_commit(c->sc, s->fence, 1);
    return 0;
}

static void w43_init_qs(qs *sc, w43_sim_ctx *ctx, int with_gate) {
    memset(sc, 0, sizeof *sc);
    ctx->sc = sc;
    sc->run = w43_run_cb; sc->done = w43_done_cb; sc->now = w43_now_cb;
    sc->gate = with_gate ? w43_gate_cb : NULL;
    sc->ctx = ctx; sc->park_ns = 100000000u;
}

static void w43_submit(qs *sc, uint64_t serial, uint64_t fence, volatile uint32_t *w0w) {
    qs_sub *s = calloc(1u, sizeof *s);
    s->serial = serial; s->fence = fence; s->frame = 1u;
    if (w0w) { s->w0kind = 1; s->w0word = w0w; s->w0value = 1u; }
    qs_submit(sc, s);
}

static void test_fence_window_parity(void) {
    static qs s0, s1;
    static w43_sim_ctx c0, c1;
    w43_init_qs(&s0, &c0, 0); w43_init_qs(&s1, &c1, 0);
    for (uint64_t step = 0u; step < 320u; step += 2u) {
        uint64_t hi = step + 260u;
        vka_qsched_fast_set_for_test(0);
        uint64_t r0a = qs_commit(&s0, hi, 1), r0b = qs_commit(&s0, step + 2u, 1), r0c = qs_commit(&s0, step + 1u, 1);
        int w0_hi = qs_whole(&s0, hi), w0_p = qs_whole(&s0, step + 3u);
        vka_qsched_fast_set_for_test(1);
        uint64_t r1a = qs_commit(&s1, hi, 1), r1b = qs_commit(&s1, step + 2u, 1), r1c = qs_commit(&s1, step + 1u, 1);
        int w1_hi = qs_whole(&s1, hi), w1_p = qs_whole(&s1, step + 3u);
        EXPECT_EQ_U64("fence sig hi", r0a, r1a);
        EXPECT_EQ_U64("fence sig mid", r0b, r1b);
        EXPECT_EQ_U64("fence sig fill", r0c, r1c);
        EXPECT_EQ_U64("fence lo", s0.lo, s1.lo);
        EXPECT_EQ_U64("fence sig", s0.sig, s1.sig);
        EXPECT_EQ_U64("whole hi", (uint64_t)w0_hi, (uint64_t)w1_hi);
        EXPECT_EQ_U64("whole pending", (uint64_t)w0_p, (uint64_t)w1_p);
    }
    qs_ev_free(&s0); qs_ev_free(&s1);
}

static void test_event_and_gw_parity(void) {
    static qs s0, s1;
    static w43_sim_ctx c0, c1;
    static volatile uint32_t ev[600], gw[48];
    memset((void *)ev, 0, sizeof ev); memset((void *)gw, 0, sizeof gw);
    w43_init_qs(&s0, &c0, 1); w43_init_qs(&s1, &c1, 1);
    qs_sub prod = {.fence = 10u, .frame = 1u};
    uint32_t ev55[2] = {0u, 0u};
    int gw35[2] = {0, 0}, gw36[2] = {0, 0}, gw47[2] = {0, 0};
    for (int mode = 0; mode < 2; mode++) {
        vka_qsched_fast_set_for_test(mode);
        qs *s = mode ? &s1 : &s0;
        w43_sim_ctx *c = mode ? &c1 : &c0;
        s->cur = &prod;
        for (uint32_t i = 0u; i < 400u; i++) qs_event_replayed(s, &ev[i], i & 1u, i + 1u);
        s->cur = NULL;
        qs_commit(s, 10u, 1);
        c->done = 10u;
        qs_sub p2 = {.fence = 11u, .frame = 2u};
        s->cur = &p2;
        for (uint32_t i = 0u; i < 120u; i++) qs_event_replayed(s, &ev[i], 0u, 99u + i);
        s->cur = NULL;
        qs_commit(s, 11u, 1);
        for (uint32_t i = 0u; i < 48u; i++) {
            qs_gw_note(s, &gw[i], 1); qs_gw_note(s, &gw[i], 1);
            if (i == 40u) qs_gate_ban(s, &gw[36]);
        }
        ev55[mode] = qs_ev_value(s, &ev[55]);
        gw35[mode] = qs_gw_cpu(s, &gw[35]);
        gw36[mode] = qs_gw_cpu(s, &gw[36]);
        gw47[mode] = qs_gw_cpu(s, &gw[47]);
    }
    vka_qsched_fast_set_for_test(-1);
    EXPECT_EQ_U64("evpeak", s0.evpeak, s1.evpeak);
    EXPECT_EQ_U64("ev[55]", ev55[0], ev55[1]);
    EXPECT_EQ_U64("gw cpu 35", (uint64_t)gw35[0], (uint64_t)gw35[1]);
    EXPECT_EQ_U64("gw ban 36", (uint64_t)gw36[0], (uint64_t)gw36[1]);
    EXPECT_EQ_U64("gw cpu 47", (uint64_t)gw47[0], (uint64_t)gw47[1]);
    qs_ev_free(&s0); qs_ev_free(&s1);
}

static void test_park_poll_parity(void) {
    static qs s0, s1;
    static w43_sim_ctx c0, c1;
    volatile uint32_t w[4] = {0u, 0u, 0u, 0u};
    w43_init_qs(&s0, &c0, 0); w43_init_qs(&s1, &c1, 0);
    for (int mode = 0; mode < 2; mode++) {
        vka_qsched_fast_set_for_test(mode);
        qs *s = mode ? &s1 : &s0;
        for (uint64_t i = 0u; i < 4u; i++) w43_submit(s, 10u + i, i + 1u, &w[i]);
    }
    EXPECT_EQ_U64("parked before", (uint64_t)qs_parked(&s0), (uint64_t)qs_parked(&s1));
    EXPECT_EQ_U64("park_min before", qs_park_min(&s0), qs_park_min(&s1));
    w[2] = 1u; w[0] = 1u;
    vka_qsched_fast_set_for_test(0); int m0 = qs_poll(&s0);
    vka_qsched_fast_set_for_test(1); int m1 = qs_poll(&s1);
    EXPECT_EQ_U64("poll moved", (uint64_t)m0, (uint64_t)m1);
    EXPECT_EQ_U64("park_min mid", qs_park_min(&s0), qs_park_min(&s1));
    vka_qsched_fast_set_for_test(0); qs_force_all(&s0, QS_WHY_PRESENT);
    vka_qsched_fast_set_for_test(1); qs_force_all(&s1, QS_WHY_PRESENT);
    EXPECT_EQ_U64("parked after", (uint64_t)qs_parked(&s0), (uint64_t)qs_parked(&s1));
    EXPECT_EQ_U64("ran total", c0.ran, c1.ran);
    qs_ev_free(&s0); qs_ev_free(&s1);
}

static void test_gmap_and_page_parity(void) {
    uint64_t bm0[10], bm1[10], state = UINT64_C(0x9e3779b97f4a7c15);
    memset(bm0, 0, sizeof bm0); memset(bm1, 0, sizeof bm1);
    for (int iter = 0; iter < 500; iter++) {
        state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        uint64_t a = (state >> 20) % 512u, b = ((state >> 36) % 512u);
        uint64_t k0 = a < b ? a : b, k1 = a < b ? b : a;
        if ((iter & 3) != 3) {
            for (uint64_t k = k0; k <= k1; k++) bm0[k >> 6] |= UINT64_C(1) << (k & 63u);
            vka_gmap_set_range(bm1, k0, k1);
        }
        int r0 = 1;
        for (uint64_t k = k0; k <= k1; k++) if (!((bm0[k >> 6] >> (k & 63u)) & 1u)) { r0 = 0; break; }
        int r1 = vka_gmap_all_set(bm1, k0, k1);
        EXPECT_EQ_U64("gmap all_set", (uint64_t)r0, (uint64_t)r1);
        EXPECT_EQ_U64("gmap memcmp", (uint64_t)memcmp(bm0, bm1, sizeof bm0), 0u);
    }
    memset(bm1, 0, sizeof bm1);
    vka_gmap_set_range(bm1, 0u, 130u);
    EXPECT_EQ_U64("gmap tail exact", (uint64_t)vka_gmap_all_set(bm1, 0u, 130u), 1u);
    EXPECT_EQ_U64("gmap tail partial", (uint64_t)vka_gmap_all_set(bm1, 0u, 131u), 0u);
    uint8_t page[4096];
    memset(page, 0, sizeof page);
    EXPECT_EQ_U64("zero page", (uint64_t)vka_page_nonzero_4k(page), 0u);
    page[4095] = 1u;
    EXPECT_EQ_U64("tail nonzero page", (uint64_t)vka_page_nonzero_4k(page), 1u);
}

typedef struct { int tid; _Atomic int *err; } w43_thr_arg;
static void *w43_tsan_worker(void *arg) {
    w43_thr_arg *a = (w43_thr_arg *)arg;
    qs *sc = calloc(1u, sizeof *sc);
    w43_sim_ctx ctx = {0};
    w43_init_qs(sc, &ctx, 1);
    volatile uint32_t ev[8] = {0};
    for (uint64_t i = 1u; i <= 200u; i++) {
        qs_sub sub = {.fence = i, .frame = 1u};
        sc->cur = &sub;
        qs_event_replayed(sc, &ev[i & 7u], 0u, (uint32_t)i);
        sc->cur = NULL;
        w43_submit(sc, 1u + (i & 3u), i, NULL);
        qs_poll(sc);
        ctx.done = i;
    }
    if (qs_parked(sc) || ctx.ran != 200u) atomic_store(a->err, 1);
    qs_ev_free(sc);
    free(sc);
    return NULL;
}

static void test_tsan_threads(void) {
    _Atomic int err = 0;
    pthread_t th[4];
    w43_thr_arg args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (w43_thr_arg){.tid = i, .err = &err};
        pthread_create(&th[i], NULL, w43_tsan_worker, &args[i]);
    }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    EXPECT_EQ_U64("tsan threads err", (uint64_t)atomic_load(&err), 0u);
}

static int w43_bench_gmap_all(const uint64_t *gmap, uint64_t k0, uint64_t k1, int fast) {
    if (fast) return vka_gmap_all_set(gmap, k0, k1);
    for (uint64_t k = k0; k <= k1; k++) if (!((gmap[k >> 6] >> (k & 63u)) & 1u)) return 0;
    return 1;
}

static uint64_t w43_bench_mode(int mode) {
    static qs sc;
    static w43_sim_ctx ctx;
    static volatile uint32_t ev[128], gw[32];
    static uint64_t gmap[16];
    memset(gmap, 0xff, sizeof gmap);
    w43_init_qs(&sc, &ctx, 1);
    vka_qsched_fast_set_for_test(mode);
    sc.nq = 12;
    for (uint64_t qi = 0u; qi < 12u; qi++) sc.q[qi].serial = 100u + qi;
    for (int k = 0; k < 32; k++) { qs_gw_note(&sc, &gw[k], 1); qs_gw_note(&sc, &gw[k], 1); }
    qs_sub prod = {.frame = 1u, .fence = 1u};
    sc.cur = &prod;
    for (uint32_t k = 0u; k < 64u; k++) qs_event_replayed(&sc, &ev[k], 0u, k + 1u);
    sc.cur = NULL;
    uint64_t sink = qs_commit(&sc, 1u, 1);
    ctx.done = 1u;
    for (uint64_t blk = 0u; blk < 128u; blk++) {
        uint64_t base = 2u + blk * 32u;
        for (int rev = 31; rev >= 0; rev--) {
            uint64_t f = base + (uint64_t)rev;
            prod.fence = f; sc.cur = &prod;
            qs_event_replayed(&sc, &ev[((blk * 32u) + 31u - (uint64_t)rev) & 127u], 0u, (uint32_t)f);
            sc.cur = NULL;
            sink += qs_commit(&sc, f, 1);
            sink += (uint64_t)qs_poll(&sc);
            sink += (uint64_t)qs_whole(&sc, base + 15u);
            sink += qs_park_min(&sc);
            sink += (uint64_t)qs_gw_cpu(&sc, &gw[((unsigned)rev * 7u) & 31u]);
            sink += (uint64_t)w43_bench_gmap_all(gmap, 3u, 511u, mode);
        }
        ctx.done = base + 31u;
        if ((blk & 7u) == 7u) qs_ev_prune(&sc);
    }
    qs_ev_free(&sc);
    vka_qsched_fast_set_for_test(-1);
    return sink;
}

int main(void) {
    const char *fe = getenv("AOI_W43_FAULT");
    if (fe && *fe) aoi_w43_set_fault(atoi(fe));
    test_fence_window_parity();
    test_event_and_gw_parity();
    test_park_poll_parity();
    test_gmap_and_page_parity();
    test_tsan_threads();
    uint64_t t0 = w43_now_ns(), s0 = w43_bench_mode(0), t1 = w43_now_ns();
    uint64_t t2 = w43_now_ns(), s1 = w43_bench_mode(1), t3 = w43_now_ns();
    if (s0 != s1) { g_fail++; fprintf(stderr, "FAIL bench sink %llu != %llu\n", (unsigned long long)s0, (unsigned long long)s1); }
    double slow_ms = (double)(t1 - t0) / 1e6, fast_ms = (double)(t3 - t2) / 1e6;
    printf("t_qsched_fast_w43: slow_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
           (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
