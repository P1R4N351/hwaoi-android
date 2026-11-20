/* Wave 16 (Opt 18): Host tests & benchmarks for vka_gw_fast.h (DM_VK_GW_FAST=1). */
#define _POSIX_C_SOURCE 200809L
#define W16_TEST_HOOKS 1
#include "../vk_android/vka_gw_fast.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

int g_w16_fault = 0;
static volatile uint64_t g_w16_sink;

static double now_ms(void) {
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(rc == 0);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static void fill_scalar_ref(uint8_t *out, uint64_t lo, uint64_t hi, uint64_t w_lo, uint32_t val) {
    for (uint64_t k = lo; k < hi; k++) {
        uint32_t v = val;
        out[k - lo] = ((const uint8_t *)&v)[(k - w_lo) & 3u];
    }
}

static int test_gw_summary_and_bloom(void) {
    vka_gw_summary s = {0};
    assert(vka_gw_may_overlap(&s, 0x10000ull, 0x20000ull) == 0);
    assert(vka_gw_may_hazard(&s, 0x10000ull, 0x20000ull) == 0);
    vka_gw_sum_add(&s, 0x20010000ull, 0x20048000ull, 0);
    assert(vka_gw_may_overlap(&s, 0x20020000ull, 0x20021000ull) == 1);
    assert(vka_gw_may_hazard(&s, 0x20020000ull, 0x20021000ull) == 0);
    assert(vka_gw_sum_hit_locked(&s, 0x20050000ull, 0x20060000ull) == 0);
    vka_gw_sum_add(&s, 0x3000f000ull, 0x30021000ull, 1);
    assert(vka_gw_may_hazard(&s, 0x30010000ull, 0x30010100ull) == 1);
    vka_gw_sum_publish(&s, 1u, 0x3000f000ull, 0x30021000ull,
                       vka_gw_bloom_mask(0x3000f000ull, 0x30021000ull),
                       1u, 0x3000f000ull, 0x30021000ull,
                       vka_gw_bloom_mask(0x3000f000ull, 0x30021000ull));
    assert(vka_gw_may_overlap(&s, 0x20020000ull, 0x20021000ull) == 0);
    assert(vka_gw_may_hazard(&s, 0x30018000ull, 0x30019000ull) == 1);
    if (!vka_gw_may_hazard(&s, 0x30018000ull, 0x30019000ull)) return 1;
    return 0;
}

static int test_gw_fill_bytes(void) {
    uint8_t got[4112], ref[4112];
    const uint32_t vals[3] = {0x11223344u, 0xdeadbeefu, 0x01020304u};
    const size_t lens[6] = {0u, 1u, 3u, 7u, 65u, 4099u};
    for (int vi = 0; vi < 3; vi++) {
        for (uint64_t ph = 0; ph < 4u; ph++) {
            for (int li = 0; li < 6; li++) {
                size_t len = lens[li];
                memset(got, 0xa5, sizeof got);
                memset(ref, 0xa5, sizeof ref);
                uint64_t w_lo = 0x10000000ull;
                uint64_t lo = w_lo + ph;
                vka_gw_fill_bytes(got + 3u, lo, lo + len, w_lo, vals[vi]);
                fill_scalar_ref(ref + 3u, lo, lo + len, w_lo, vals[vi]);
                if (memcmp(got, ref, sizeof got) != 0) return 1;
            }
        }
    }
    return 0;
}

static int test_ub_bloom_and_shadow_memo(void) {
    vka_ub_shadow_memo4 m = {0};
    uint64_t base = 0x50000000ull, addrs[4] = {0, 0, 0, 0};
    int found = 0;
    for (uint64_t k = 0; k < 256u && found < 4; k++) {
        uint64_t cand = base + k * 0x1000ull;
        if (vka_ub_shadow_set_idx(cand, 256u) == vka_ub_shadow_set_idx(base, 256u))
            addrs[found++] = cand;
    }
    assert(found == 4);
    for (int i = 0; i < 4; i++) assert(vka_ub_shadow_set_idx(addrs[i], 256u) == vka_ub_shadow_set_idx(addrs[0], 256u));
    uint64_t bloom = 0;
    for (int i = 0; i < 4; i++) {
        bloom |= vka_ub_bloom_mask(addrs[i], addrs[i] + 256u);
        vka_ub_shadow_insert(&m, 7u, addrs[i], 256u, 0x9000u + (uint64_t)i, 64u * (uint64_t)i);
    }
    assert((bloom & vka_ub_bloom_mask(addrs[0] + 0x40000ull, addrs[0] + 0x40100ull)) == 0u);
    for (int i = 0; i < 4; i++) {
        uint64_t bu = 0, of = 0;
        if (!vka_ub_shadow_lookup(&m, 7u, addrs[i], 256u, &bu, &of)) return 1;
        if (bu != 0x9000u + (uint64_t)i || of != 64u * (uint64_t)i) return 1;
    }
    vka_ub_shadow_reset(&m);
    uint64_t bu = 0, of = 0;
    if (vka_ub_shadow_lookup(&m, 7u, addrs[0], 256u, &bu, &of)) return 1;
    if (vka_ub_shadow_lookup(&m, 8u, addrs[1], 256u, &bu, &of)) return 1;
    return 0;
}

typedef struct { uint64_t lo, hi; uint32_t kind; } mock_gw;

static void run_benchmarks(void) {
    enum { NG = 64, NQ = 200000 };
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    mock_gw gw[NG];
    vka_gw_summary s = {0};
    for (int i = 0; i < NG; i++) {
        gw[i] = (mock_gw){0x80000000ull + (uint64_t)i * 0x10000ull, 0x80001000ull + (uint64_t)i * 0x10000ull, 3u};
        vka_gw_sum_add(&s, gw[i].lo, gw[i].hi, 1);
    }
    double t0 = now_ms();
    uint64_t hits_b = 0;
    for (int q = 0; q < NQ; q++) {
        uint64_t addr = 0x20000000ull + (uint64_t)(q & 15) * 0x1000ull;
        pthread_mutex_lock(&mu);
        for (int i = 0; i < NG; i++) hits_b += (gw[i].hi > addr && gw[i].lo < addr + 256u);
        pthread_mutex_unlock(&mu);
    }
    double ms_b = now_ms() - t0;
    vka_ub_shadow_memo4 sm = {0};
    for (int k = 0; k < 16; k++)
        vka_ub_shadow_insert(&sm, 1u, 0x20000000ull + (uint64_t)k * 0x1000ull, 256u, 0x700u + (uint64_t)k, (uint64_t)k * 256u);
    double t1 = now_ms();
    uint64_t hits_f = 0;
    for (int q = 0; q < NQ; q++) {
        uint64_t addr = 0x20000000ull + (uint64_t)(q & 15) * 0x1000ull, bu = 0, of = 0;
        hits_f += (uint64_t)vka_gw_may_hazard(&s, addr, addr + 256u);
        hits_f += (uint64_t)vka_ub_shadow_lookup(&sm, 1u, addr, 256u, &bu, &of);
    }
    double ms_f = now_ms() - t1;
    g_w16_sink = hits_b + hits_f;
    static uint8_t buf[4096];
    double f0 = now_ms();
    for (int r = 0; r < 10000; r++) fill_scalar_ref(buf, 0x10001ull, 0x10001ull + 4096u, 0x10000ull, 0x12345678u);
    double fs_b = now_ms() - f0;
    double f1 = now_ms();
    for (int r = 0; r < 10000; r++) vka_gw_fill_bytes(buf, 0x10001ull, 0x10001ull + 4096u, 0x10000ull, 0x12345678u);
    double fs_f = now_ms() - f1;
    g_w16_sink += buf[17];
    printf("aoi_w16_gw_probe: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms_b, ms_f, ms_b / (ms_f > 0.001 ? ms_f : 0.001));
    printf("aoi_w16_gw_fill: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", fs_b, fs_f, fs_b / (fs_f > 0.001 ? fs_f : 0.001));
}

int main(void) {
    const char *f = getenv("T_W16_FAULT");
    g_w16_fault = f ? atoi(f) : 0;
    if (test_gw_summary_and_bloom() != 0) { fprintf(stderr, "FAIL test_gw_summary_and_bloom\n"); return 1; }
    if (test_gw_fill_bytes() != 0) { fprintf(stderr, "FAIL test_gw_fill_bytes\n"); return 1; }
    if (test_ub_bloom_and_shadow_memo() != 0) { fprintf(stderr, "FAIL test_ub_bloom_and_shadow_memo\n"); return 1; }
    if (!g_w16_fault) run_benchmarks();
    printf("t_gw_fast_w16: ok (fault=%d)\n", g_w16_fault);
    return 0;
}
