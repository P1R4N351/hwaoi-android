// Unit, parity, ASan+UBSan, TSan, fault-injection & benchmark suite for DM_VK_SPVPATCH_FAST (Wave 39 / Opt 41).
#define _POSIX_C_SOURCE 200809L
#define T_W39_TEST_HOOKS 1
#include "../../gfx/vk_android/vka_spvpatch.h"
#include "../../gfx/vk_android/vka_spvpatch_fast.h"
#include "../../gfx/vk_android/vka_memu_spv.h"
#include "../../gfx/vk_android/vka_selftest_spv.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_vka_spvpatch_fast_state;
_Atomic int g_vka_w39_fault;

static uint64_t w39_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static const uint32_t k_spec_sg[] = {
    0x07230203u, 0x00010300u, 0u, 24u, 0u,
    (2u << 16) | 17u, 1u, (3u << 16) | 14u, 0u, 1u,
    (5u << 16) | 15u, 5u, 4u, 0x6e69616du, 0u,
    (4u << 16) | 71u, 10u, 1u, 3u,
    (4u << 16) | 71u, 11u, 11u, 41u,
    (4u << 16) | 71u, 12u, 11u, 4416u,
    (2u << 16) | 19u, 2u, (3u << 16) | 33u, 3u, 2u,
    (4u << 16) | 21u, 6u, 32u, 0u, (4u << 16) | 23u, 7u, 6u, 4u,
    (4u << 16) | 32u, 8u, 1u, 6u, (4u << 16) | 32u, 9u, 1u, 7u,
    (4u << 16) | 50u, 6u, 10u, 0u,
    (4u << 16) | 59u, 8u, 11u, 1u, (4u << 16) | 59u, 9u, 12u, 1u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u, (2u << 16) | 248u, 5u,
    (4u << 16) | 61u, 6u, 13u, 11u, (4u << 16) | 61u, 7u, 14u, 12u,
    (1u << 16) | 253u, (1u << 16) | 56u,
};

static const uint32_t k_spec_miss[] = {
    0x07230203u, 0x00010300u, 0u, 16u, 0u,
    (2u << 16) | 17u, 1u, (3u << 16) | 14u, 0u, 1u,
    (5u << 16) | 15u, 4u, 4u, 0x6e69616du, 0u,
    (2u << 16) | 19u, 2u, (3u << 16) | 33u, 3u, 2u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u, (2u << 16) | 248u, 5u,
    (1u << 16) | 253u, (1u << 16) | 56u,
};

static const uint32_t k_fs_in[] = {
    0x07230203u, 0x00010300u, 0u, 24u, 0u,
    (2u << 16) | 17u, 1u, (3u << 16) | 14u, 0u, 1u,
    (7u << 16) | 15u, 4u, 4u, 0x6e69616du, 0u, 11u, 12u,
    (4u << 16) | 71u, 11u, 30u, 2u,
    (4u << 16) | 71u, 12u, 30u, 5u,
    (3u << 16) | 22u, 6u, 32u, (4u << 16) | 23u, 7u, 6u, 4u,
    (4u << 16) | 21u, 8u, 32u, 0u, (4u << 16) | 43u, 8u, 9u, 3u,
    (4u << 16) | 28u, 10u, 7u, 9u,
    (4u << 16) | 32u, 13u, 1u, 7u, (4u << 16) | 32u, 14u, 1u, 10u,
    (4u << 16) | 59u, 13u, 11u, 1u, (4u << 16) | 59u, 14u, 12u, 1u,
    (2u << 16) | 19u, 2u, (3u << 16) | 33u, 3u, 2u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u, (2u << 16) | 248u, 5u,
    (1u << 16) | 253u, (1u << 16) | 56u,
};

static uint32_t *make_padded_vert(size_t *out_n) {
    size_t n = sizeof SPV_TRI_VERT / 4u, fn_at = n;
    for (size_t i = 5u; i < n; i += SPV_TRI_VERT[i] >> 16) {
        if ((SPV_TRI_VERT[i] & 0xffffu) == 54u) { fn_at = i; break; }
    }
    size_t nop_n = 120u, tot = n + nop_n;
    uint32_t *m = (uint32_t *)malloc(tot * 4u);
    assert(m != NULL);
    memcpy(m, SPV_TRI_VERT, fn_at * 4u);
    memcpy(m + fn_at, SPV_TRI_VERT + fn_at, (n - fn_at - 1u) * 4u);
    size_t k = n - 1u;
    for (size_t i = 0; i < nop_n; i++) m[k++] = (1u << 16) | 0u;
    m[k++] = SPV_TRI_VERT[n - 1u];
    m[3] = 600u;
    *out_n = tot;
    return m;
}

static void check_eq_buf(const uint32_t *a, size_t na, const uint32_t *b, size_t nb) {
    assert(a != NULL && b != NULL);
    assert(na == nb && memcmp(a, b, na * 4u) == 0);
}

static void check_parity_all(int fmode) {
    char e0[256] = {0}, e1[256] = {0};
    size_t vn = sizeof SPV_TRI_VERT / 4u, fn = sizeof SPV_TRI_FRAG / 4u;
    size_t v0_n = 0, v1_n = 0, f0_n = 0, f1_n = 0, sg0_n = 0, sg1_n = 0, sp0_n = 0, sp1_n = 0;
    int ch0 = 0, ch1 = 0; uint64_t l0 = 0u, l1 = 0u;
    vka_spvpatch_fast_reset_for_test(0); vka_w39_set_fault(0);
    uint32_t *v0 = vka_spv_vin_retype(SPV_TRI_VERT, vn, 1u, 2u, &v0_n, e0, sizeof e0);
    uint32_t *f0 = vka_spv_fout_retype(SPV_TRI_FRAG, fn, 1u, 0u, &f0_n, e0, sizeof e0);
    uint32_t *sg0 = vka_spv_subgroup_single(k_spec_sg, sizeof k_spec_sg / 4u, &sg0_n, &ch0, e0, sizeof e0);
    uint32_t *sp0 = vka_spv_spec_freeze(k_spec_sg, sizeof k_spec_sg / 4u, 3u, 7u, &sp0_n);
    int r0 = vka_memu_fs_input_locs(k_fs_in, sizeof k_fs_in / 4u, &l0);
    vka_spvpatch_fast_reset_for_test(1); vka_w39_set_fault(fmode);
    uint32_t *v1 = vka_spv_vin_retype(SPV_TRI_VERT, vn, 1u, 2u, &v1_n, e1, sizeof e1);
    uint32_t *f1 = vka_spv_fout_retype(SPV_TRI_FRAG, fn, 1u, 0u, &f1_n, e1, sizeof e1);
    uint32_t *sg1 = vka_spv_subgroup_single(k_spec_sg, sizeof k_spec_sg / 4u, &sg1_n, &ch1, e1, sizeof e1);
    uint32_t *sp1 = vka_spv_spec_freeze(k_spec_sg, sizeof k_spec_sg / 4u, 3u, 7u, &sp1_n);
    int r1 = vka_memu_fs_input_locs(k_fs_in, sizeof k_fs_in / 4u, &l1);
    check_eq_buf(v0, v0_n, v1, v1_n); check_eq_buf(f0, f0_n, f1, f1_n);
    check_eq_buf(sg0, sg0_n, sg1, sg1_n); check_eq_buf(sp0, sp0_n, sp1, sp1_n);
    assert(ch0 == ch1 && ch0 == 1);
    assert(r0 == 1 && r1 == 1 && l0 == l1 && l0 == ((UINT64_C(1) << 2) | (UINT64_C(7) << 5)));
    free(v0); free(v1); free(f0); free(f1); free(sg0); free(sg1); free(sp0); free(sp1);
    if (!fmode) {
        size_t big_n = 0u, b0_n = 0u, b1_n = 0u, pn = 0u, sn = 0u; int ch = 0;
        uint32_t *big = make_padded_vert(&big_n);
        vka_spvpatch_fast_reset_for_test(0);
        uint32_t *b0 = vka_spv_vin_retype(big, big_n, 1u, 2u, &b0_n, e0, sizeof e0);
        vka_spvpatch_fast_reset_for_test(1);
        uint32_t *b1 = vka_spv_vin_retype(big, big_n, 1u, 2u, &b1_n, e1, sizeof e1);
        check_eq_buf(b0, b0_n, b1, b1_n);
        assert(vka_spv_spec_freeze(k_spec_miss, sizeof k_spec_miss / 4u, 3u, 7u, &pn) == NULL);
        assert(vka_spv_subgroup_single(k_spec_miss, sizeof k_spec_miss / 4u, &sn, &ch, e1, sizeof e1) == NULL && ch == 0);
        free(big); free(b0); free(b1);
    }
}

static void *worker_fn(void *arg) {
    (void)arg;
    char err[128];
    for (int i = 0; i < 120; i++) {
        size_t on = 0u;
        uint32_t *v = vka_spv_vin_retype(SPV_TRI_VERT, sizeof SPV_TRI_VERT / 4u, 1u, 2u, &on, err, sizeof err);
        assert(v != NULL && on > 0u);
        free(v);
        uint64_t mask = 0u;
        int rc = vka_memu_fs_input_locs(k_fs_in, sizeof k_fs_in / 4u, &mask);
        assert(rc == 1 && mask == ((UINT64_C(1) << 2) | (UINT64_C(7) << 5)));
    }
    return NULL;
}

static double bench_mode(int fast_on, uint64_t *sink) {
    vka_spvpatch_fast_reset_for_test(fast_on);
    vka_w39_set_fault(0);
    size_t vn = sizeof SPV_TRI_VERT / 4u, fn = sizeof SPV_TRI_FRAG / 4u, pad_n = 0u;
    uint32_t *pad_miss = make_padded_vert(&pad_n);
    uint64_t t0 = w39_ns(), acc = 0u;
    char err[64];
    for (int it = 0; it < 10000; it++) {
        size_t on = 0u;
        uint32_t *v = vka_spv_vin_retype(pad_miss, pad_n, 1u, 2u, &on, err, sizeof err);
        uint32_t *f = vka_spv_fout_retype(SPV_TRI_FRAG, fn, 1u, 0u, &on, err, sizeof err);
        int ch = 0;
        uint32_t *sg = vka_spv_subgroup_single(k_spec_sg, sizeof k_spec_sg / 4u, &on, &ch, err, sizeof err);
        uint32_t *ms = vka_spv_spec_freeze(pad_miss, pad_n, 3u, 7u, &on);
        uint32_t *sp = vka_spv_spec_freeze(k_spec_sg, sizeof k_spec_sg / 4u, 3u, 7u, &on);
        uint64_t lmask = 0u;
        vka_memu_fs_input_locs(k_fs_in, sizeof k_fs_in / 4u, &lmask);
        acc += (uint64_t)vn + v[3] + f[3] + sg[3] + sp[3] + lmask + (ms == NULL);
        free(v); free(f); free(sg); free(sp);
    }
    free(pad_miss);
    *sink += acc;
    return (double)(w39_ns() - t0) / 1e6;
}

int main(void) {
    const char *fs = getenv("AOI_W39_FAULT");
    int fmode = fs ? atoi(fs) : 0;
    check_parity_all(fmode);
    vka_spvpatch_fast_reset_for_test(1); vka_w39_set_fault(0);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, worker_fn, NULL) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    const char *env = getenv("DM_VK_SPVPATCH_FAST");
    int req_off = (env && env[0] == '0' && !env[1]);
    uint64_t s0 = 0u, s1 = 0u;
    double ms0 = bench_mode(0, &s0), ms1 = bench_mode(1, &s1);
    assert(s0 == s1);
    printf("t_spvpatch_fast_w39: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0.0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}
