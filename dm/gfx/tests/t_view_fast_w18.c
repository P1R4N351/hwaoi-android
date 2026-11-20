/* Wave 18 (Opt 20) unit test, fault injection & benchmark for vka_view_fast.h. */
#define _POSIX_C_SOURCE 200809L
#define W18_TEST_HOOKS 1
#include "../vk_android/vka_view_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w18_fault = 0;
static volatile uint64_t w18_sink = 0;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct { uint32_t cf[8]; uint32_t n, ds, d, s, samples, clear; } sim_rpkey;
typedef struct { uint32_t fl, u, w, h, nl, nvf; uint32_t vf[6]; } sim_fbatt;
typedef struct { uint64_t rp; uint32_t w, h, layers, na; sim_fbatt a[9]; } sim_fbkey;
typedef struct sim_vent { struct sim_vent *next; uint64_t v, k0, k1; } sim_vent;

_Static_assert(sizeof(sim_rpkey) == 56, "sim_rpkey 56B");
_Static_assert(offsetof(sim_fbkey, a) == 24 && sizeof(sim_fbatt) == 48 && sizeof(sim_fbkey) == 456, "sim_fbkey 456B");

static int test_view_memo_and_fault1(void) {
    static vka_view_memo4 vm;
    memset(&vm, 0, sizeof vm);
    const uint32_t swz[4] = {0u, 1u, 2u, 3u};
    uint64_t k0a = 0, k1a = 0, k0b = 0, k1b = 0, out = 0;
    if (!vka_view_key_pack(37, 1u, 1u, 0u, 1u, 0u, 1u, swz, 0, &k0a, &k1a)) return 1;
    if (!vka_view_key_pack(37, 1u, 1u, 2u, 1u, 0u, 1u, swz, 0, &k0b, &k1b)) return 1;
    if (k1a == k1b) return 1;
    vka_view_memo4_put(&vm, 0x1000u, k0a, k1a, 10u, 0xaaaa0001u);
    if (!vka_view_memo4_get(&vm, 0x1000u, k0a, k1a, 10u, &out) || out != 0xaaaa0001u) return 1;
    if (vka_view_memo4_get(&vm, 0x1000u, k0b, k1b, 10u, &out)) return 1;
    if (vka_view_memo4_get(&vm, 0x1000u, k0a, k1a, 11u, &out)) return 1;
    return 0;
}

static int test_rp_and_fb_faults(void) {
    static vka_rp_memo4 rm;
    static vka_fb_memo4 fm;
    memset(&rm, 0, sizeof rm);
    memset(&fm, 0, sizeof fm);
    sim_rpkey r0 = {.cf = {37u, 0u}, .n = 1u, .samples = 1u, .clear = 0u};
    sim_rpkey r1 = r0;
    r1.clear = 1u;
    vka_rp_memo4_put(&rm, &r0, 0xbbb0001u);
    uint64_t rp_out = 0;
    if (!vka_rp_memo4_get(&rm, &r0, &rp_out) || rp_out != 0xbbb0001u) return 1;
    if (vka_rp_memo4_get(&rm, &r1, &rp_out)) return 1;

    sim_fbkey f0 = {.rp = 0xbbb0001u, .w = 1280u, .h = 720u, .layers = 1u, .na = 2u};
    f0.a[0] = (sim_fbatt){0u, 0x10u, 1280u, 720u, 1u, 1u, {37u}};
    f0.a[1] = (sim_fbatt){0u, 0x20u, 1280u, 720u, 1u, 1u, {126u}};
    sim_fbkey f1 = f0;
    f1.a[1].vf[0] = 129u;
    uint64_t h0 = vka_fbkey_hash_fast(&f0, f0.na, sizeof f0, offsetof(sim_fbkey, a), sizeof(sim_fbatt));
    uint64_t h1 = vka_fbkey_hash_fast(&f1, f1.na, sizeof f1, offsetof(sim_fbkey, a), sizeof(sim_fbatt));
    if (h0 == h1) return 1;
    if (vka_fbkey_eq_fast(&f0, &f1, f0.na, f1.na, sizeof f0, offsetof(sim_fbkey, a), sizeof(sim_fbatt))) return 1;
    vka_fb_memo4_put(&fm, h0, f0.rp, 3u, 0xccc0003u);
    uint32_t sl = 99u; uint64_t fb_out = 0;
    if (!vka_fb_memo4_get(&fm, h0, f0.rp, &sl, &fb_out) || sl != 3u || fb_out != 0xccc0003u) return 1;
    vka_fb_memo4_clear(&fm);
    if (vka_fb_memo4_get(&fm, h0, f0.rp, &sl, &fb_out)) return 1;
    return 0;
}

static void bench_view_memo(void) {
    enum { NQ = 200000, NV = 16 };
    static vka_view_memo4 vm;
    static sim_vent nodes[NV];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&vm, 0, sizeof vm);
    const uint32_t swz[4] = {0u, 1u, 2u, 3u};
    uint64_t k0[NV], k1[NV];
    sim_vent *head = NULL;
    for (int i = NV - 1; i >= 0; i--) {
        int ok = vka_view_key_pack(37 + (i & 3), 1u, 1u, (uint32_t)(i >> 2), 1u, 0u, 1u, swz, 0, &k0[i], &k1[i]);
        assert(ok == 1);
        nodes[i] = (sim_vent){head, UINT64_C(0x50000) + (uint64_t)i, k0[i], k1[i]};
        head = &nodes[i];
        vka_view_memo4_put(&vm, 0x1000u, k0[i], k1[i], 1u, nodes[i].v);
    }
    uint64_t t0 = now_ns(), sb = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        pthread_mutex_lock(&mu);
        for (sim_vent **pv = &head, *v; (v = *pv) != NULL; pv = &v->next) {
            if (v->k0 == k0[idx] && v->k1 == k1[idx]) {
                if (pv != &head) { *pv = v->next; v->next = head; head = v; }
                sb += v->v; break;
            }
        }
        pthread_mutex_unlock(&mu);
    }
    uint64_t ns0 = now_ns() - t0;
    uint64_t t1 = now_ns(), sf = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        uint64_t vu = 0;
        if (vka_view_memo4_get(&vm, 0x1000u, k0[idx], k1[idx], 1u, &vu)) sf += vu;
    }
    uint64_t ns1 = now_ns() - t1;
    assert(sb == sf);
    w18_sink += sb;
    double ms0 = (double)ns0 * 1e-6, ms1 = (double)ns1 * 1e-6;
    printf("aoi_w18_view_memo: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001));
}

static uint64_t legacy_fb_hash(const sim_fbkey *k) {
    uint64_t h = UINT64_C(1469598103934665603);
    const uint8_t *p = (const uint8_t *)k;
    for (size_t i = 0; i < sizeof *k; i++) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}

static void bench_rp_fb_fast(void) {
    enum { NQ = 200000, NENT = 32, FB_CAP = 1024 };
    typedef struct { sim_fbkey k; uint64_t fb, h; int used; } sim_fbent;
    static vka_rp_memo4 rm;
    static vka_fb_memo4 fm;
    static sim_rpkey rps[NENT];
    static sim_fbkey fbs[NENT];
    static sim_fbent g_leg_fb[FB_CAP];
    static sim_fbent g_opt_fb[FB_CAP];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&rm, 0, sizeof rm);
    memset(&fm, 0, sizeof fm);
    memset(g_leg_fb, 0, sizeof g_leg_fb);
    memset(g_opt_fb, 0, sizeof g_opt_fb);
    for (int i = 0; i < NENT; i++) {
        rps[i] = (sim_rpkey){.cf = {37u + (uint32_t)(i & 3), 0u}, .n = 1u, .ds = 126u, .d = 1u, .samples = 1u, .clear = (uint32_t)i};
        fbs[i] = (sim_fbkey){.rp = UINT64_C(0x70000) + (uint64_t)i, .w = 1280u + (uint32_t)(i * 16), .h = 720u, .layers = 1u, .na = 2u};
        fbs[i].a[0] = (sim_fbatt){0u, 0x10u, fbs[i].w, 720u, 1u, 1u, {37u}};
        fbs[i].a[1] = (sim_fbatt){0u, 0x20u, fbs[i].w, 720u, 1u, 1u, {126u}};
        uint64_t fbv = UINT64_C(0x80000) + (uint64_t)i;
        uint64_t lh = legacy_fb_hash(&fbs[i]);
        uint32_t lj = (uint32_t)lh & (FB_CAP - 1u);
        while (g_leg_fb[lj].used) lj = (lj + 1u) & (FB_CAP - 1u);
        g_leg_fb[lj] = (sim_fbent){fbs[i], fbv, lh, 1};
        vka_rp_memo4_put(&rm, &rps[i], fbs[i].rp);
        uint64_t fh = vka_fbkey_hash_fast(&fbs[i], 2u, sizeof fbs[i], offsetof(sim_fbkey, a), sizeof(sim_fbatt));
        uint32_t oj = (uint32_t)fh & (FB_CAP - 1u);
        while (g_opt_fb[oj].used) oj = (oj + 1u) & (FB_CAP - 1u);
        g_opt_fb[oj] = (sim_fbent){fbs[i], fbv, fh, 1};
        vka_fb_memo4_put(&fm, fh, fbs[i].rp, oj, fbv);
    }
    uint64_t t0 = now_ns(), sb = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        uint64_t rp = 0;
        pthread_mutex_lock(&mu);
        for (int i = 0; i < NENT; i++) {
            if (memcmp(&rps[i], &rps[idx], sizeof(sim_rpkey)) == 0) { rp = UINT64_C(0x70000) + (uint64_t)i; break; }
        }
        pthread_mutex_unlock(&mu);
        sim_fbkey fk = fbs[idx];
        fk.rp = rp;
        uint64_t h = legacy_fb_hash(&fk);
        for (uint32_t j = (uint32_t)h & (FB_CAP - 1u); g_leg_fb[j].used; j = (j + 1u) & (FB_CAP - 1u)) {
            if (g_leg_fb[j].h == h && memcmp(&g_leg_fb[j].k, &fk, sizeof(sim_fbkey)) == 0) {
                sb += rp + g_leg_fb[j].fb;
                break;
            }
        }
    }
    uint64_t ns0 = now_ns() - t0;
    uint64_t t1 = now_ns(), sf = 0;
    for (int q = 0; q < NQ; q++) {
        uint32_t idx = (uint32_t)((q ^ (int)(w18_sink & 0u)) & 7);
        uint64_t rp = 0, fb = 0; uint32_t sl = 0;
        (void)vka_rp_memo4_get(&rm, &rps[idx], &rp);
        sim_fbkey fk = fbs[idx];
        fk.rp = rp;
        uint64_t h = vka_fbkey_hash_fast(&fk, fk.na, sizeof fk, offsetof(sim_fbkey, a), sizeof(sim_fbatt));
        if (vka_fb_memo4_get(&fm, h, rp, &sl, &fb) && sl < FB_CAP && g_opt_fb[sl].used && g_opt_fb[sl].h == h &&
            vka_fbkey_eq_fast(&g_opt_fb[sl].k, &fk, g_opt_fb[sl].k.na, fk.na, sizeof fk, offsetof(sim_fbkey, a), sizeof(sim_fbatt))) {
            sf += rp + fb;
        }
    }
    uint64_t ns1 = now_ns() - t1;
    assert(sb == sf);
    w18_sink += sb;
    double ms0 = (double)ns0 * 1e-6, ms1 = (double)ns1 * 1e-6;
    printf("aoi_w18_rp_fb_fast: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001));
}

int main(void) {
    const char *f = getenv("T_W18_FAULT");
    g_w18_fault = f ? atoi(f) : 0;
    if (test_view_memo_and_fault1() != 0) { fprintf(stderr, "FAIL view_memo fault=%d\n", g_w18_fault); return 1; }
    if (test_rp_and_fb_faults() != 0) { fprintf(stderr, "FAIL rp_fb fault=%d\n", g_w18_fault); return 1; }
    if (!g_w18_fault) {
        bench_view_memo();
        bench_rp_fb_fast();
        printf("PASS t_view_fast_w18\n");
    }
    return 0;
}
