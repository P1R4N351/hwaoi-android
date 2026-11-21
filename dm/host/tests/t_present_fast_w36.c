// Unit, parity, TSan & benchmark harness for Wave 36 present fast paths.
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W36_TEST_HOOKS 1
#include "../../gfx/vk_android/vka_present_fast.h"

_Atomic int g_vka_present_fast_state;
_Atomic int g_vka_w36_fault;

static const char *const g_w36_asps[4] = {"16:9", "4:3", "21:9", "16:10"};

static uint64_t w36_ns(void) {
    struct timespec t;
    memset(&t, 0, sizeof t);
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int w36_legacy_aspect(const char *s, int *aw, int *ah) {
    if (!s || !aw || !ah) return 0;
    int a = 0, b = 0;
    if (sscanf(s, "%d:%d", &a, &b) != 2 || a <= 0 || b <= 0 || a > 65536 || b > 65536) return 0;
    *aw = a; *ah = b;
    return 1;
}

static _Atomic uint32_t g_w36_loader_calls;
static void w36_dummy_loader(vka_pres_cfg *c) {
    uint32_t k = atomic_fetch_add_explicit(&g_w36_loader_calls, 1u, memory_order_relaxed) + 1u;
    c->aspect_w = 16; c->aspect_h = 9;
    c->rcas_con0_bits = 0x3f800000u + k;
}

static void w36_check_aspect(int flt) {
    const char *cases[] = {NULL, "", "16:9", "  4:3", "\t21:9 extra", "16: 9", "0:9", "-4:3", "16:0", "9999999:9", "abc:4", "16", "+32:+9"};
    size_t n = sizeof cases / sizeof cases[0];
    for (size_t i = 0; i < n; i++) {
        int aw0 = -1, ah0 = -1, aw1 = -1, ah1 = -1;
        int r0 = w36_legacy_aspect(cases[i], &aw0, &ah0);
        int r1 = vka_parse_aspect_fast(cases[i], &aw1, &ah1, flt);
        if (r0 != r1 || (r0 && (aw0 != aw1 || ah0 != ah1))) abort();
    }
}

static void w36_check_cfg_and_memo(int flt) {
    vka_pres_cfg cfg;
    memset(&cfg, 0, sizeof cfg);
    atomic_store_explicit(&g_w36_loader_calls, 0u, memory_order_relaxed);
    (void)vka_pres_cfg_sync(&cfg, 1u, w36_dummy_loader, flt);
    (void)vka_pres_cfg_sync(&cfg, 1u, w36_dummy_loader, flt);
    if (atomic_load_explicit(&g_w36_loader_calls, memory_order_relaxed) != 1u) abort();
    (void)vka_pres_cfg_sync(&cfg, 2u, w36_dummy_loader, flt);
    if (atomic_load_explicit(&g_w36_loader_calls, memory_order_relaxed) != 2u) abort();

    vka_pres_dset_memo memo;
    memset(&memo, 0, sizeof memo);
    if (!vka_pres_rot_need_write(&memo, 0u, 1u, 100u, 200u, 1, flt)) abort();
    if (vka_pres_rot_need_write(&memo, 0u, 1u, 100u, 200u, 1, flt)) abort();
    if (!vka_pres_rot_need_write(&memo, 0u, 2u, 100u, 200u, 1, flt)) abort();
    if (!vka_pres_fsr_src_need_write(&memo, 1u, 5u, 300u, 400u, flt)) abort();
    if (vka_pres_fsr_src_need_write(&memo, 1u, 5u, 300u, 400u, flt)) abort();
    if (!vka_pres_fsr_mid_need_write(&memo, 1u, 9u, flt)) abort();
    if (vka_pres_fsr_mid_need_write(&memo, 1u, 9u, flt)) abort();
    vka_pres_dset_clear_mid(&memo, 1u);
    if (!vka_pres_fsr_mid_need_write(&memo, 1u, 9u, flt)) abort();
    vka_pres_dset_reset(&memo);
    if (!vka_pres_rot_need_write(&memo, 0u, 2u, 100u, 200u, 1, flt)) abort();
}

static void w36_check_ring_and_pq(int flt) {
    vka_pres_ring512 r;
    memset(&r, 0, sizeof r);
    for (uint64_t n = 1u; n <= 600u; n++) vka_pres_ring_note(&r, n, 1000.0 + (double)n * 0.25);
    for (uint64_t n = 1u; n <= 88u; n++) {
        if (vka_pres_ring_read(&r, n, flt) != 0.0) abort();
    }
    for (uint64_t n = 89u; n <= 600u; n++) {
        if (vka_pres_ring_read(&r, n, flt) != 1000.0 + (double)n * 0.25) abort();
    }
    vka_pq_sync q;
    memset(&q, 0, sizeof q);
    atomic_store_explicit(&q.done_sh, 10u, memory_order_release);
    if (!vka_pq_already_done(&q, 10u, flt) || vka_pq_already_done(&q, 11u, flt)) abort();
    if (vka_pq_should_wake(0u, 1) || !vka_pq_should_wake(0u, 0)) abort();
    if (!vka_pq_should_wake(1u, 1) || !vka_pq_should_wake(1u, 0)) abort();
}

typedef struct {
    vka_pres_ring512 *ring;
    vka_pq_sync *pq;
} w36_tsan_ctx;

static void *w36_tsan_writer(void *arg) {
    w36_tsan_ctx *ctx = (w36_tsan_ctx *)arg;
    for (uint64_t n = 1u; n <= 20000u; n++) {
        vka_pres_ring_note(ctx->ring, n, (double)n * 0.5);
        atomic_store_explicit(&ctx->pq->in_sh, n, memory_order_release);
        atomic_store_explicit(&ctx->pq->done_sh, n, memory_order_release);
    }
    return NULL;
}

static void *w36_tsan_reader(void *arg) {
    w36_tsan_ctx *ctx = (w36_tsan_ctx *)arg;
    uint64_t sum = 0u;
    for (uint64_t n = 1u; n <= 20000u; n++) {
        double t = vka_pres_ring_read(ctx->ring, n, 0);
        if (t > 0.0) sum += (uint64_t)t;
        if (vka_pq_already_done(ctx->pq, n, 0)) sum++;
        sum += (uint64_t)vka_present_fast_on();
    }
    return (void *)(uintptr_t)sum;
}

static uint64_t w36_bench_once(int fast) {
    vka_pres_cfg cfg; memset(&cfg, 0, sizeof cfg);
    vka_pres_dset_memo memo; memset(&memo, 0, sizeof memo);
    vka_pres_ring512 ring; memset(&ring, 0, sizeof ring);
    for (uint64_t i = 1u; i <= 512u; i++) vka_pres_ring_note(&ring, i, (double)i);
    uint64_t bound_rot[3] = {0u, 0u, 0u}, bound_fsr[3] = {0u, 0u, 0u};
    uint64_t sink = 0u;
    for (uint32_t it = 0u; it < 150000u; it++) {
        const char *asp = g_w36_asps[it & 3u];
        int aw = 0, ah = 0;
        int ok = fast ? vka_parse_aspect_fast(asp, &aw, &ah, 0) : w36_legacy_aspect(asp, &aw, &ah);
        sink += ok ? (uint64_t)(aw + ah) : 0u;
        if (fast) sink += vka_pres_cfg_sync(&cfg, 1u, w36_dummy_loader, 0)->rcas_con0_bits ? 1u : 0u;
        else { w36_dummy_loader(&cfg); sink += cfg.rcas_con0_bits ? 1u : 0u; }
        unsigned slot = it % 3u;
        uint64_t serial = 1u + slot, view_r = 200u + slot, view_f = 300u + slot;
        int w_rot = fast ? vka_pres_rot_need_write(&memo, slot, serial, 100u, view_r, 1, 0) : 1;
        int w_fsr = fast ? vka_pres_fsr_src_need_write(&memo, slot, serial, 100u, view_f, 0) : 1;
        if (w_rot) bound_rot[slot] = view_r;
        if (w_fsr) bound_fsr[slot] = view_f;
        sink += bound_rot[slot] + bound_fsr[slot];
    }
    return sink;
}

int main(void) {
    const char *env = getenv("DM_VK_PRESENT_FAST");
    const char *flt_s = getenv("T_W36_FAULT");
    int init_on = vka_present_fast_on();
    if (env && !strcmp(env, "0")) assert(!init_on); else assert(init_on);
    int flt = flt_s ? atoi(flt_s) : 0;
    w36_check_aspect(flt);
    w36_check_cfg_and_memo(flt);
    w36_check_ring_and_pq(flt);

    vka_pres_ring512 ring; memset(&ring, 0, sizeof ring);
    vka_pq_sync pq; memset(&pq, 0, sizeof pq);
    w36_tsan_ctx tctx = {&ring, &pq};
    pthread_t th[3];
    assert(pthread_create(&th[0], NULL, w36_tsan_writer, &tctx) == 0);
    assert(pthread_create(&th[1], NULL, w36_tsan_reader, &tctx) == 0);
    assert(pthread_create(&th[2], NULL, w36_tsan_reader, &tctx) == 0);
    for (int i = 0; i < 3; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); }

    uint64_t t0 = w36_ns(), s0 = w36_bench_once(0), off_ns = w36_ns() - t0;
    uint64_t t1 = w36_ns(), s1 = w36_bench_once(1), on_ns = w36_ns() - t1;
    assert(s0 == s1);
    printf("BENCH aoi_w36_off exec=%.3f ms sink=%llu\n", off_ns / 1e6, (unsigned long long)s0);
    printf("BENCH aoi_w36_on exec=%.3f ms sink=%llu\n", on_ns / 1e6, (unsigned long long)s1);
    return 0;
}
