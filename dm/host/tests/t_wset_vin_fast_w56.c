/* Wave 56 (Opt 58 / DM_VK_WSET_VIN_FAST=1) unit test, fault injection & benchmark. */
#ifndef T_W56_TEST_HOOKS
#define T_W56_TEST_HOOKS
#endif
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vka_wset_vin_fast.h"

_Atomic int g_vka_wset_vin_fast_override = -1;
_Atomic int g_aoi_w56_fault = -1;

static void w56_req(int ok) {
    if (!ok) abort();
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int ref_pack_words(const uint64_t (*rows)[4], uint32_t nw,
                          uint64_t layout_u, uint64_t *pd, size_t *pd_n) {
    size_t need = 2u + 4u * (size_t)nw;
    int same = (*pd_n == need && pd[0] == layout_u && pd[1] == nw);
    for (uint32_t i = 0; i < nw && same; i++) {
        for (int k = 0; k < 4; k++) {
            if (pd[2u + 4u * (size_t)i + (size_t)k] != rows[i][k]) {
                same = 0;
                break;
            }
        }
    }
    if (same) return 1;
    pd[0] = layout_u;
    pd[1] = nw;
    for (uint32_t i = 0; i < nw; i++) {
        for (int k = 0; k < 4; k++) {
            pd[2u + 4u * (size_t)i + (size_t)k] = rows[i][k];
        }
    }
    *pd_n = need;
    return 0;
}

static void check_wset_equiv(void) {
    vka_wset_fast_t wf = {0};
    uint64_t rows[36][4] = {{0}};
    uint64_t pd0[512 * 4 + 4] = {0}, pd1[512 * 4 + 4] = {0};
    size_t n0 = 0, n1 = 0;
    vka_wset_reset(&wf, 0u);
    for (uint32_t i = 0; i < 36u; i++) {
        uint32_t dtype = (i % 3u == 0u) ? 7u : (i % 3u == 1u) ? 1u : 4u;
        if (dtype == 7u) {
            uint64_t buf = (i == 9u) ? 0u : (UINT64_C(0x1000) + i * 16u);
            rows[i][0] = vka_wset_hdr(i, 0u, dtype);
            rows[i][1] = buf; rows[i][2] = i * 64u; rows[i][3] = 256u;
            vka_wset_put_buf(&wf, i, i, 0u, dtype, buf, i * 64u, 256u);
        } else if (dtype == 1u) {
            uint64_t vw = (i == 22u) ? 0u : (UINT64_C(0x2000) + i);
            rows[i][0] = vka_wset_hdr(i, 0u, dtype);
            rows[i][1] = 0x5555u + i; rows[i][2] = vw; rows[i][3] = 7u;
            vka_wset_put_img(&wf, i, i, 0u, dtype, 0x5555u + i, vw, 7u, 0);
        } else {
            rows[i][0] = vka_wset_hdr(i, 0u, dtype);
            rows[i][1] = 0x9000u + i; rows[i][2] = 0u; rows[i][3] = 0u;
            vka_wset_put_tbuf(&wf, i, i, 0u, dtype, 0x9000u + i);
        }
    }
    w56_req(wf.any_null == 1u);
    w56_req((wf.null_buf_m[0] >> 9 & 1u) == 1u && (wf.null_img_m[0] >> 22 & 1u) == 1u);
    vka_wset_patch_buf(&wf, 9u, UINT64_C(0xDEAD), 0u, 65536u);
    rows[9][1] = UINT64_C(0xDEAD); rows[9][2] = 0u; rows[9][3] = 65536u;
    vka_wset_patch_img(&wf, 22u, UINT64_C(0xBEEF), 7u);
    rows[22][2] = UINT64_C(0xBEEF); rows[22][3] = 7u;
    int s0 = ref_pack_words(rows, 36u, UINT64_C(0xAA01), pd0, &n0);
    int s1 = vka_wset_pd_match_or_copy(&wf, 36u, UINT64_C(0xAA01), pd1, &n1);
    w56_req(s0 == 0 && s1 == 0 && n0 == n1 && memcmp(pd0, pd1, n0 * sizeof(uint64_t)) == 0);
    s0 = ref_pack_words(rows, 36u, UINT64_C(0xAA01), pd0, &n0);
    s1 = vka_wset_pd_match_or_copy(&wf, 36u, UINT64_C(0xAA01), pd1, &n1);
    w56_req(s0 == 1 && s1 == 1);
}

static void check_gw_and_pfb(void) {
    uint64_t addrs[257], sizes[257], mend[257];
    addrs[0] = 0x10000000u;
    sizes[0] = 0x04000000u; /* 64 MiB atlas spanning early range */
    for (int i = 1; i < 257; i++) {
        addrs[i] = 0x10000000u + (uint64_t)i * 0x10000u;
        sizes[i] = 0x4000u;
    }
    vka_gw_build_max_end(mend, addrs, sizes, 257);
    uint64_t wlo = 0x13800000u, whi = 0x13804000u;
    int upper = vka_gw_upper_addr(addrs, 257, whi);
    uint64_t m_fast = 0u, m_oracle = 0u;
    for (int j = upper - 1; j >= 0 && mend[j] > wlo; j--) {
        if (wlo < addrs[j] + sizes[j]) m_fast += addrs[j] ^ (uint64_t)j;
    }
    for (int j = 0; j < 257; j++) {
        if (addrs[j] < whi && wlo < addrs[j] + sizes[j]) m_oracle += addrs[j] ^ (uint64_t)j;
    }
    w56_req(m_fast == m_oracle && m_fast != 0u);

    uint32_t wd[512] = {0};
    wd[13] = 0x80000002u; wd[257] = 0x00010000u; wd[511] = 1u;
    uint64_t sum = 0u;
    for (uint32_t i = vka_pfb_next_nonzero_word(wd, 512u, 0u); i < 512u;
         i = vka_pfb_next_nonzero_word(wd, 512u, i + 1u)) {
        for (uint32_t x = wd[i]; x; x &= x - 1u) {
            sum += (uint64_t)(i * 32u + (uint32_t)__builtin_ctz(x));
        }
    }
    uint64_t exp = 417u + 447u + 8240u + 16352u;
    w56_req(sum == exp);
    w56_req(vka_pfb_free_slot(0x15u, 8u) == 1u);
    w56_req(vka_pfb_free_slot(0xffu, 8u) == 8u);
    w56_req(vka_sh_iface_eval_veg(1) == 1 && vka_sh_iface_eval_veg(0) == 0);
}

static void *thread_worker(void *arg) {
    int tid = *(int *)arg;
    for (int it = 0; it < 200; it++) {
        vka_wset_vin_fast_set_for_test((tid + it) & 1);
        (void)vka_wset_vin_fast_on();
        check_wset_equiv();
    }
    return NULL;
}

static uint64_t bench_wset_pfb(int fast) {
    static uint32_t wd[32768];
    static vka_wset_fast_t wf;
    static uint64_t rows[32][4], pd[256];
    size_t pd_n = 0;
    uint64_t sink = 0u;
    memset(wd, 0, sizeof wd);
    wd[1234] = 5u; wd[29999] = 0x80u;
    for (uint32_t i = 0; i < 32u; i++) {
        rows[i][0] = vka_wset_hdr(i, 0u, 7u);
        rows[i][1] = 0x1000u + i; rows[i][2] = 0u; rows[i][3] = 256u;
    }
    for (int it = 0; it < 2000; it++) {
        rows[0][2] = (uint64_t)(it >> 2);
        if (fast) {
            if (it == 0) {
                vka_wset_reset(&wf, 32u);
                for (uint32_t i = 0; i < 32u; i++) {
                    vka_wset_put_buf(&wf, i, i, 0u, 7u, rows[i][1], rows[i][2], rows[i][3]);
                }
            } else {
                vka_wset_patch_buf(&wf, 0u, rows[0][1], rows[0][2], rows[0][3]);
            }
            int same = vka_wset_pd_match_or_copy(&wf, 32u, 0x999u, pd, &pd_n);
            sink += (uint64_t)same + pd[4];
            for (uint32_t i = vka_pfb_next_nonzero_word(wd, 32768u, 0u); i < 32768u;
                 i = vka_pfb_next_nonzero_word(wd, 32768u, i + 1u)) {
                for (uint32_t x = wd[i]; x; x &= x - 1u) sink += i * 32u + (uint32_t)__builtin_ctz(x);
            }
        } else {
            int same = ref_pack_words(rows, 32u, 0x999u, pd, &pd_n);
            sink += (uint64_t)same + pd[4];
            for (uint32_t i = 0; i < 32768u; i++) {
                for (uint32_t x = wd[i]; x; x &= x - 1u) sink += i * 32u + (uint32_t)__builtin_ctz(x);
            }
        }
    }
    return sink;
}

int main(void) {
    pthread_t th[4];
    int tids[4] = {0, 1, 2, 3};
    check_wset_equiv();
    check_gw_and_pfb();
    for (int i = 0; i < 4; i++) w56_req(pthread_create(&th[i], NULL, thread_worker, &tids[i]) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    vka_wset_vin_fast_set_for_test(-1);
    uint64_t t0 = now_ns(), s0 = bench_wset_pfb(0), t1 = now_ns(), s1 = bench_wset_pfb(1), t2 = now_ns();
    w56_req(s0 == s1);
    double ms0 = (double)(t1 - t0) / 1e6, ms1 = (double)(t2 - t1) / 1e6;
    printf("t_wset_vin_fast_w56: baseline=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=PASS\n",
           ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001), (unsigned long long)s1);
    return 0;
}
