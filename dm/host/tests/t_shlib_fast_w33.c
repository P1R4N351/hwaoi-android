// Unit, parity, ASan+UBSan, TSan, fault-injection & benchmark suite for DM_VK_SHLIB_FAST (Wave 33 / Opt 35).
#define _POSIX_C_SOURCE 200809L
#include "../../gfx/vk_android/vka_shlib.h"
#include "../../gfx/vk_android/vka_spvpatch.h"
#include "../../gfx/vk_android/vka_shlib_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_vka_shlib_fast_state;
_Atomic int g_vka_w33_fault;

static uint64_t w33_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static uint64_t w33_rng(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545f4914f6cdd1d);
}

enum { W33_ENT_N = 101860u };

static void w33_build_lib(vka_shlib *lib, dmsh_vk_header *hdr, dmsh_vk_entry **out_ent) {
    dmsh_vk_entry *ent = calloc(W33_ENT_N, sizeof *ent);
    assert(ent != NULL);
    for (uint32_t i = 0; i < W33_ENT_N; i++) {
        ent[i].key = UINT64_C(0x1000000000000001) + (uint64_t)i * UINT64_C(0x9e3779b1);
        ent[i].vin_mask = (uint32_t)(i & 15u);
        ent[i].fout_mask = (uint8_t)(i & 7u);
    }
    memset(lib, 0, sizeof *lib);
    memset(hdr, 0, sizeof *hdr);
    hdr->entry_count = W33_ENT_N;
    lib->h = hdr;
    lib->ent = ent;
    *out_ent = ent;
}

/* Depth-only fragment shader (no Output vars, bound = 16). */
static const uint32_t k_spv_depth[] = {
    0x07230203u, 0x00010300u, 0u, 16u, 0u,
    (2u << 16) | 17u, 1u,
    (3u << 16) | 14u, 0u, 1u,
    (5u << 16) | 15u, 4u, 4u, 0x6e69616du, 0u,
    (3u << 16) | 16u, 4u, 7u,
    (2u << 16) | 19u, 2u,
    (3u << 16) | 33u, 3u, 2u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u,
    (2u << 16) | 248u, 5u,
    (1u << 16) | 253u,
    (1u << 16) | 56u,
};

/* Fragment shader with 2 outputs: loc 0 vec4 written via full OpStore (bound = 32), loc 1 component 2 only. */
static const uint32_t k_spv_fout[] = {
    0x07230203u, 0x00010300u, 0u, 32u, 0u,
    (2u << 16) | 17u, 1u,
    (3u << 16) | 14u, 0u, 1u,
    (7u << 16) | 15u, 4u, 4u, 0x6e69616du, 0u, 12u, 13u,
    (4u << 16) | 71u, 12u, 30u, 0u,
    (4u << 16) | 71u, 13u, 30u, 1u,
    (2u << 16) | 19u, 2u,
    (3u << 16) | 33u, 3u, 2u,
    (3u << 16) | 22u, 6u, 32u,
    (4u << 16) | 23u, 7u, 6u, 4u,
    (4u << 16) | 32u, 8u, 3u, 7u,
    (4u << 16) | 32u, 9u, 3u, 6u,
    (4u << 16) | 21u, 10u, 32u, 1u,
    (4u << 16) | 43u, 10u, 11u, 2u,
    (4u << 16) | 59u, 8u, 12u, 3u,
    (4u << 16) | 59u, 8u, 13u, 3u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u,
    (2u << 16) | 248u, 5u,
    (3u << 16) | 62u, 12u, 14u,
    (5u << 16) | 65u, 9u, 15u, 13u, 11u,
    (3u << 16) | 62u, 15u, 16u,
    (1u << 16) | 253u,
    (1u << 16) | 56u,
};

/* Fragment shader reading pool[idx] at set=1 binding=0 (bound = 32), triggering vka_spv_pool_fb rewrite. */
static const uint32_t k_spv_pool[] = {
    0x07230203u, 0x00010300u, 0u, 32u, 0u,
    (2u << 16) | 17u, 1u,
    (3u << 16) | 14u, 0u, 1u,
    (6u << 16) | 15u, 4u, 4u, 0x6e69616du, 0u, 20u,
    (4u << 16) | 71u, 20u, 34u, 1u,
    (4u << 16) | 71u, 20u, 33u, 0u,
    (2u << 16) | 19u, 2u,
    (3u << 16) | 33u, 3u, 2u,
    (3u << 16) | 22u, 6u, 32u,
    (4u << 16) | 21u, 7u, 32u, 0u,
    (4u << 16) | 43u, 7u, 8u, 3u,
    (8u << 16) | 25u, 9u, 6u, 1u, 0u, 0u, 0u, 1u,
    (3u << 16) | 26u, 10u, 9u,
    (4u << 16) | 28u, 11u, 10u, 1024u,
    (4u << 16) | 32u, 12u, 0u, 11u,
    (4u << 16) | 32u, 13u, 0u, 10u,
    (4u << 16) | 59u, 12u, 20u, 0u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u,
    (2u << 16) | 248u, 5u,
    (5u << 16) | 65u, 13u, 21u, 20u, 8u,
    (1u << 16) | 253u,
    (1u << 16) | 56u,
};

/* Minimal FS with DecComponent (2) that must return -1. */
static const uint32_t k_spv_comp[] = {
    0x07230203u, 0x00010300u, 0u, 16u, 0u,
    (4u << 16) | 71u, 12u, 2u, 1u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u,
    (1u << 16) | 56u,
};

/* Minimal VS with Input loc 0 vec4 float for vka_spv_vin_retype put_n parity. */
static const uint32_t k_spv_vin[] = {
    0x07230203u, 0x00010300u, 0u, 20u, 0u,
    (2u << 16) | 17u, 1u,
    (3u << 16) | 14u, 0u, 1u,
    (6u << 16) | 15u, 0u, 4u, 0x6e69616du, 0u, 12u,
    (4u << 16) | 71u, 12u, 30u, 0u,
    (2u << 16) | 19u, 2u,
    (3u << 16) | 33u, 3u, 2u,
    (3u << 16) | 22u, 6u, 32u,
    (4u << 16) | 23u, 7u, 6u, 4u,
    (4u << 16) | 32u, 8u, 1u, 7u,
    (4u << 16) | 59u, 8u, 12u, 1u,
    (5u << 16) | 54u, 2u, 4u, 0u, 3u,
    (2u << 16) | 248u, 5u,
    (4u << 16) | 61u, 7u, 14u, 12u,
    (1u << 16) | 253u,
    (1u << 16) | 56u,
};

static uint64_t w33_run_shlib_spv(const vka_shlib *lib, const dmsh_vk_entry *ent,
                                  int fast_on, int ops) {
    vka_shlib_fast_reset_for_test(fast_on);
    uint64_t rng = UINT64_C(0x5733333300000001), sink = 0;
    for (int i = 0; i < ops; i++) {
        int idx = (i & 3) ? (int)(w33_rng(&rng) % W33_ENT_N) : -1;
        uint64_t key = idx >= 0 ? ent[idx].key : (w33_rng(&rng) | UINT64_C(0x8000000000000000));
        const dmsh_vk_entry *e = vka_shlib_find(lib, key);
        sink += e ? (uint64_t)(e - ent) + 1u + e->vin_mask : 0u;
    }
    uint8_t c_d[8], c_f[8];
    for (int i = 0; i < ops / 8; i++) {
        assert(vka_spv_fout_comps(k_spv_depth, sizeof k_spv_depth / 4u, c_d) == 0);
        assert(vka_spv_fout_comps(k_spv_fout, sizeof k_spv_fout / 4u, c_f) == 0);
        sink += (uint64_t)c_d[0] + (uint64_t)c_f[0] + ((uint64_t)c_f[1] << 8);
        size_t out_nw = 99u;
        int sites = 99, early = 99;
        char err[128];
        uint32_t *no = vka_spv_pool_fb(k_spv_fout, sizeof k_spv_fout / 4u, 1u, 0u,
                                       0u, 3u, 0u, 1u, &out_nw, &sites, &early, err, sizeof err);
        assert(no == NULL && sites == 0 && err[0] == 0);
    }
    return sink;
}

static void w33_check_parity(const vka_shlib *lib, const dmsh_vk_entry *ent) {
    uint8_t md[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    vka_shlib_fast_reset_for_test(0);
    uint64_t k0 = vka_shlib_key(md);
    assert(vka_shlib_find(lib, ent[0].key) == &ent[0]);
    assert(vka_shlib_find(lib, ent[W33_ENT_N - 1u].key) == &ent[W33_ENT_N - 1u]);
    assert(vka_shlib_find(lib, 0u) == NULL);

    uint8_t cf0[8], cf1[8], cc0[8], cc1[8], ch0[8], ch1[8];
    assert(vka_spv_fout_comps(k_spv_fout, sizeof k_spv_fout / 4u, cf0) == 0);
    assert(vka_spv_fout_comps(k_spv_comp, sizeof k_spv_comp / 4u, cc0) == -1);
    uint32_t heap_fout[sizeof k_spv_fout / 4u];
    memcpy(heap_fout, k_spv_fout, sizeof k_spv_fout);
    heap_fout[3] = 400u; /* bound > VKA_FOUT_STACK_BOUND exercises single calloc path */
    assert(vka_spv_fout_comps(heap_fout, sizeof heap_fout / 4u, ch0) == 0);

    size_t nw0 = 0, nw1 = 0, vn0 = 0, vn1 = 0;
    int s0 = 0, e0 = 0, s1 = 0, e1 = 0;
    char err0[128], err1[128];
    uint32_t *p0 = vka_spv_pool_fb(k_spv_pool, sizeof k_spv_pool / 4u, 1u, 0u, 0u, 5u,
                                   1u, 1u, &nw0, &s0, &e0, err0, sizeof err0);
    uint32_t *v0 = vka_spv_vin_retype(k_spv_vin, sizeof k_spv_vin / 4u, 1u, 0u, &vn0, err0, sizeof err0);
    assert(p0 != NULL && s0 == 1 && v0 != NULL);

    vka_shlib_fast_reset_for_test(1);
    assert(vka_shlib_key(md) == k0);
    assert(vka_shlib_find(lib, ent[0].key) == &ent[0]);
    assert(vka_shlib_find(lib, ent[W33_ENT_N - 1u].key) == &ent[W33_ENT_N - 1u]);
    assert(vka_shlib_find(lib, 0u) == NULL);
    assert(vka_spv_fout_comps(k_spv_fout, sizeof k_spv_fout / 4u, cf1) == 0);
    assert(cf0[0] == 15u && cf0[1] == 4u && memcmp(cf0, cf1, 8) == 0);
    assert(vka_spv_fout_comps(k_spv_comp, sizeof k_spv_comp / 4u, cc1) == -1);
    assert(memcmp(cc0, cc1, 8) == 0);
    assert(vka_spv_fout_comps(heap_fout, sizeof heap_fout / 4u, ch1) == 0);
    assert(memcmp(ch0, ch1, 8) == 0);

    uint32_t *p1 = vka_spv_pool_fb(k_spv_pool, sizeof k_spv_pool / 4u, 1u, 0u, 0u, 5u,
                                   1u, 1u, &nw1, &s1, &e1, err1, sizeof err1);
    uint32_t *v1 = vka_spv_vin_retype(k_spv_vin, sizeof k_spv_vin / 4u, 1u, 0u, &vn1, err1, sizeof err1);
    assert(p1 != NULL && s1 == s0 && e1 == e0 && nw1 == nw0);
    assert(memcmp(p0, p1, nw0 * 4u) == 0);
    assert(v1 != NULL && vn1 == vn0 && memcmp(v0, v1, vn0 * 4u) == 0);
    free(p0); free(p1); free(v0); free(v1);

    /* Malformed SPIR-V must still fail with non-empty err. */
    uint32_t bad_spv[8];
    memcpy(bad_spv, k_spv_fout, sizeof bad_spv);
    bad_spv[7] = (99u << 16) | 71u;
    assert(vka_spv_pool_fb(bad_spv, 8u, 1u, 0u, 0u, 5u, 0u, 1u, &nw1, &s1, &e1, err1, sizeof err1) == NULL);
    assert(err1[0] != 0);
}

static void w33_check_fault(const vka_shlib *lib, const dmsh_vk_entry *ent, int fm) {
    vka_shlib_fast_reset_for_test(1);
    if (fm == 1) {
        uint8_t md[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
        vka_w33_set_fault(fm);
        assert(vka_shlib_key(md) == UINT64_C(0x0123456789abcdef));
    } else if (fm == 2) {
        vka_w33_set_fault(fm);
        assert(vka_shlib_find(lib, ent[W33_ENT_N - 1u].key) == &ent[W33_ENT_N - 1u]);
    } else if (fm == 3) {
        size_t nw_ref = 0, nw_flt = 0; char err[128];
        vka_w33_set_fault(0);
        uint32_t *ref = vka_spv_vin_retype(k_spv_vin, sizeof k_spv_vin / 4u, 1u, 0u, &nw_ref, err, sizeof err);
        assert(ref != NULL);
        vka_w33_set_fault(fm);
        uint32_t *flt = vka_spv_vin_retype(k_spv_vin, sizeof k_spv_vin / 4u, 1u, 0u, &nw_flt, err, sizeof err);
        assert(flt != NULL && nw_flt == nw_ref && memcmp(flt, ref, nw_ref * 4u) == 0);
        free(ref); free(flt);
    } else if (fm == 4) {
        uint8_t comp[8];
        vka_w33_set_fault(fm);
        assert(vka_spv_fout_comps(k_spv_fout, sizeof k_spv_fout / 4u, comp) == 0);
        assert(comp[0] == 15u);
    } else if (fm == 5) {
        size_t nw = 0; int sites = 0, early = 0; char err[128];
        vka_w33_set_fault(fm);
        uint32_t *p = vka_spv_pool_fb(k_spv_pool, sizeof k_spv_pool / 4u, 1u, 0u, 0u, 5u,
                                      1u, 1u, &nw, &sites, &early, err, sizeof err);
        assert(p != NULL && sites == 1);
        free(p);
    }
    vka_w33_set_fault(0);
}

typedef struct { const vka_shlib *lib; const dmsh_vk_entry *ent; } w33_thr_ctx;
static void *w33_tsan_worker(void *arg) {
    const w33_thr_ctx *tc = (const w33_thr_ctx *)arg;
    uint64_t s = w33_run_shlib_spv(tc->lib, tc->ent, 1, 8000);
    assert(s != 0u);
    return NULL;
}

int main(void) {
    const char *env = getenv("DM_VK_SHLIB_FAST");
    const char *flt = getenv("T_W33_FAULT");
    int fault_mode = flt ? atoi(flt) : 0;
    vka_shlib lib; dmsh_vk_header hdr; dmsh_vk_entry *ent = NULL;
    w33_build_lib(&lib, &hdr, &ent);
    w33_check_parity(&lib, ent);
    if (fault_mode > 0) w33_check_fault(&lib, ent, fault_mode);

    vka_shlib_fast_reset_for_test(1);
    pthread_t th[4];
    w33_thr_ctx tc = {&lib, ent};
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w33_tsan_worker, &tc) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);

    uint64_t t0 = w33_ns(), s0 = w33_run_shlib_spv(&lib, ent, 0, 80000); uint64_t off_ns = w33_ns() - t0;
    uint64_t t1 = w33_ns(), s1 = w33_run_shlib_spv(&lib, ent, 1, 80000); uint64_t on_ns = w33_ns() - t1;
    assert(s0 == s1);
    vka_shlib_fast_reset_for_test((env && *env == '0' && !env[1]) ? 0 : 1);
    printf("BENCH aoi_w33_off shlib_spv=%.3f ms sink=%llu\n", off_ns / 1e6, (unsigned long long)s0);
    printf("BENCH aoi_w33_on shlib_spv=%.3f ms sink=%llu\n", on_ns / 1e6, (unsigned long long)s1);
    free(ent);
    return 0;
}
