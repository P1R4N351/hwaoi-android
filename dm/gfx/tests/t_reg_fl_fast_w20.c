/* Host unit test, benchmark & negative control for Wave 20 (Opt 22: DM_VK_REG_FAST=1). */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w20_fault = 0;
#include "../nvn/nvn_reg_fast.h"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

typedef struct {
    uint64_t serial;
    uint32_t fmt, w, h, flags;
    uint64_t pool_ptr;
} mock_tex_t;

typedef struct {
    uint8_t flags, target, base_lv, nlv;
    uint16_t fmt, min_ly, nly;
} mock_view_t;

typedef struct {
    uint8_t minf, magf, ws, wt, wr, cmp_m, cmp_f, red;
    float lmin, lmax, lbias, aniso, border[4];
} mock_samp_t;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static uint64_t fl_ref_find_locked(pthread_mutex_t *mu, const uintptr_t *lo, const uintptr_t *hi, size_t n, uintptr_t a, uintptr_t b) {
    pthread_mutex_lock(mu);
    size_t l = 0, h = n;
    while (l < h) {
        size_t m = (l + h) / 2;
        if (hi[m] <= a) l = m + 1;
        else h = m;
    }
    uint64_t r = (l < n && lo[l] < b) ? 1u : 0u;
    pthread_mutex_unlock(mu);
    return r;
}

static int test_reg_dedup(void) {
    mock_tex_t t0 = {101u, 0x25u, 1280u, 720u, 0u, 0x100000u};
    mock_tex_t t1 = {101u, 0x25u, 1280u, 720u, 0u, 0x100000u};
    mock_view_t v0 = {3u, 1u, 0u, 4u, 0x25u, 0u, 1u};
    mock_view_t v1 = {3u, 1u, 0u, 4u, 0x25u, 0u, 1u};
    CHECK(nvn_treg_same(t0.serial, t1.serial, 7u, 7u, 0u, 0u, 1u, 1u, &t0, &t1, sizeof t0, &v0, &v1, sizeof v0),
          "identical tex+view+pgen should match");
    CHECK(!nvn_treg_same(t0.serial, 0u, 7u, 7u, 0u, 0u, 1u, 1u, &t0, &t1, sizeof t0, &v0, &v1, sizeof v0),
          "zero serial must never skip");
    t1.serial = 102u;
    CHECK(!nvn_treg_same(t0.serial, t1.serial, 7u, 7u, 0u, 0u, 1u, 1u, &t0, &t1, sizeof t0, &v0, &v1, sizeof v0),
          "changed serial must re-register");
    t1.serial = 101u;
    CHECK(!nvn_treg_same(t0.serial, t1.serial, 7u, 8u, 0u, 0u, 1u, 1u, &t0, &t1, sizeof t0, &v0, &v1, sizeof v0),
          "changed pgen must re-register");
    v1.nly = 2u;
    CHECK(!nvn_treg_same(t0.serial, t1.serial, 7u, 7u, 0u, 0u, 1u, 1u, &t0, &t1, sizeof t0, &v0, &v1, sizeof v0),
          "changed view must re-register");
    mock_samp_t s0 = {1, 1, 2, 2, 2, 0, 0, 0, 0.f, 10.f, 0.f, 1.f, {0.f, 0.f, 0.f, 1.f}};
    mock_samp_t s1 = s0;
    CHECK(nvn_sreg_same(1u, &s0, &s1, sizeof s0), "identical sampler must match");
    s1.magf = 0u;
    CHECK(!nvn_sreg_same(1u, &s0, &s1, sizeof s0), "changed sampler filter must miss");
    CHECK(!nvn_sreg_same(0u, &s0, &s0, sizeof s0), "unregistered sampler slot must miss");
    return 0;
}

static int test_fl_and_ft(void) {
    nvn_fl_cov_memo4 memo;
    memset(&memo, 0, sizeof memo);
    _Atomic uint64_t dseq = 2u;
    nvn_fl_summary fsum = {0, 0, UINTPTR_MAX, 0, 0, {0}};
    uintptr_t b0 = 0x10000000u, b1 = 0x10400000u;
    CHECK(nvn_fl_sum_disjoint(&fsum, b0, b0 + 0x1000u), "empty fl_summary is disjoint");
    nvn_fl_sum_note_add(&fsum, b0, b0 + 0x40000u, 1u);
    nvn_fl_cov_put(&memo, atomic_load(&dseq), b0, b0, b0 + 0x40000u);
    CHECK(nvn_fl_cov_contains(&memo, atomic_load(&dseq), b0 + 0x1000u, b0 + 0x2000u), "subspan contained");
    CHECK(nvn_fl_cov_overlaps(&memo, atomic_load(&dseq), b0 - 0x100u, b0 + 0x100u), "partial overlap hits");
    CHECK(nvn_fl_sum_disjoint(&fsum, b1, b1 + 0x4000u), "disjoint range rejected lock-free");
    CHECK(!nvn_fl_sum_disjoint(&fsum, b0 + 0x2000u, b0 + 0x3000u), "overlapping range passes bloom");
    atomic_fetch_add(&dseq, 1u);
    nvn_fl_sum_wbegin(&fsum);
    nvn_fl_sum_wadd(&fsum, b0, b0 + 0x1000u);
    nvn_fl_sum_wadd(&fsum, b0 + 0x2000u, b0 + 0x40000u);
    nvn_fl_sum_wend(&fsum, 2u);
    uint64_t post = atomic_fetch_add(&dseq, 1u) + 1u;
    CHECK(!nvn_fl_cov_overlaps(&memo, post, b0 + 0x1200u, b0 + 0x1800u), "fl_drop middle split invalidated memo");

    nvn_ft_summary tsum = {0, 0, UINTPTR_MAX, 0, 0, {0}, {0}};
    CHECK(nvn_ft_sum_empty(&tsum), "empty ft_summary");
    CHECK(nvn_ft_sum_no_serial(&tsum, 9001u), "empty ft_summary rejects serial");
    nvn_ft_sum_add(&tsum, b0 + 0x10000u, b0 + 0x20000u, 9001u, 1u);
    CHECK(!nvn_ft_sum_empty(&tsum), "non-empty ft_summary");
    CHECK(!nvn_ft_sum_no_serial(&tsum, 9001u), "tracked serial present in bloom");
    CHECK(nvn_ft_sum_disjoint(&tsum, b1, b1 + 0x1000u), "ft disjoint range rejected");
    nvn_ft_sum_wbegin(&tsum);
    nvn_ft_sum_wend(&tsum, 0u);
    CHECK(nvn_ft_sum_empty(&tsum) && nvn_ft_sum_no_serial(&tsum, 9001u), "cleared ft_summary rejects all");
    return 0;
}

static int bench_reg_dedup(void) {
    mock_tex_t t[16], cur[16];
    mock_view_t v[16], cv[16];
    _Atomic uint32_t tver[16];
    uint8_t dirty_f[16];
    for (int k = 0; k < 16; k++) {
        t[k] = (mock_tex_t){555u + (uint64_t)k, 0x25u, 512u, 512u, 0u, 0x20000000u + ((uint64_t)k << 16)};
        cur[k] = t[k];
        v[k] = (mock_view_t){1u, 1u, 0u, 1u, 0x25u, 0u, 1u};
        cv[k] = v[k];
        atomic_init(&tver[k], 1u);
        dirty_f[k] = 0u;
    }
    uint64_t acc_leg = 0u, acc_fast = 0u;
    double t0 = now_ms();
    for (int i = 0; i < 400000; i++) {
        int k = i & 15;
        cur[k] = t[k];
        cv[k] = v[k];
        if (!dirty_f[k]) dirty_f[k] = 1u;
        atomic_fetch_add_explicit(&tver[k], 1u, memory_order_release);
        acc_leg += cur[k].serial ^ cv[k].fmt;
        dirty_f[k] = 0u;
    }
    double leg_ms = now_ms() - t0;
    t0 = now_ms();
    for (int i = 0; i < 400000; i++) {
        int k = i & 15;
        if (!nvn_treg_same(cur[k].serial, t[k].serial, 1u, 1u, 0u, 0u, 1u, 1u,
                           (const uint8_t *)&cur[k], (const uint8_t *)&t[k], sizeof t[k],
                           (const uint8_t *)&cv[k], (const uint8_t *)&v[k], sizeof v[k])) {
            cur[k] = t[k];
            cv[k] = v[k];
            atomic_fetch_add_explicit(&tver[k], 1u, memory_order_release);
        }
        acc_fast += cur[k].serial ^ cv[k].fmt;
    }
    double fast_ms = now_ms() - t0;
    printf("aoi_w20_reg_dedup: iters=400000 leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           leg_ms, fast_ms, fast_ms > 0.0 ? leg_ms / fast_ms : 0.0,
           (unsigned long long)(acc_leg ^ acc_fast));
    CHECK(acc_leg == acc_fast, "reg_dedup diff=0");
    return 0;
}

static int bench_fl_ft(void) {
    uintptr_t fl_lo[32], fl_hi[32], q_lo[64], q_hi[64];
    nvn_fl_summary fsum = {0, 0, UINTPTR_MAX, 0, 0, {0}};
    nvn_fl_cov_memo4 cov;
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&cov, 0, sizeof cov);
    for (size_t i = 0; i < 32u; i++) {
        fl_lo[i] = 0x10000000u + (i << 17);
        fl_hi[i] = fl_lo[i] + 0x10000u;
        nvn_fl_sum_note_add(&fsum, fl_lo[i], fl_hi[i], i + 1u);
    }
    for (size_t i = 0; i < 64u; i++) {
        q_lo[i] = (i & 3u) == 0u ? (fl_lo[(i >> 2) & 15u] + 0x1000u) : (0x50000000u + (i << 17));
        q_hi[i] = q_lo[i] + 0x4000u;
    }
    uint64_t lsum = 0u, fsum_acc = 0u;
    double t0 = now_ms();
    for (int it = 0; it < 200000; it++) {
        size_t k = (size_t)it & 63u;
        lsum += fl_ref_find_locked(&mu, fl_lo, fl_hi, 32u, q_lo[k], q_hi[k]);
    }
    double fl_leg_ms = now_ms() - t0;
    t0 = now_ms();
    for (int it = 0; it < 200000; it++) {
        size_t k = (size_t)it & 63u;
        uint64_t hit = 0u;
        if (!nvn_fl_sum_disjoint(&fsum, q_lo[k], q_hi[k])) {
            if (nvn_fl_cov_overlaps(&cov, 2u, q_lo[k], q_hi[k])) hit = 1u;
            else {
                hit = fl_ref_find_locked(&mu, fl_lo, fl_hi, 32u, q_lo[k], q_hi[k]);
                if (hit) nvn_fl_cov_put(&cov, 2u, q_lo[k], q_lo[k], q_hi[k]);
            }
        }
        fsum_acc += hit;
    }
    double fl_fast_ms = now_ms() - t0;
    printf("aoi_w20_fl_ft_fast: iters=200000 leg_ms=%.3f fast_ms=%.3f speedup=%.2fx diff=%llu\n",
           fl_leg_ms, fl_fast_ms, fl_fast_ms > 0.0 ? fl_leg_ms / fl_fast_ms : 0.0,
           (unsigned long long)(lsum ^ fsum_acc));
    CHECK(lsum == fsum_acc, "fl_ft_fast diff=0");
    return 0;
}

int main(void) {
    const char *e = getenv("T_W20_FAULT");
    g_w20_fault = e ? atoi(e) : 0;
    if (test_reg_dedup() != 0) return 1;
    if (test_fl_and_ft() != 0) return 1;
    if (bench_reg_dedup() != 0 || bench_fl_ft() != 0) return 1;
    printf("t_reg_fl_fast_w20: PASS (fault=%d)\n", g_w20_fault);
    return 0;
}
