// Fast pre-packed descriptor writes, null tracker, augmented gw-reupload max-end index,
// 256-bit PFB zero-chunk scanner & cached shader interface flags (DM_VK_WSET_VIN_FAST=1).
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W56_TEST_HOOKS
extern _Atomic int g_vka_wset_vin_fast_override;
extern _Atomic int g_aoi_w56_fault;
static inline void vka_wset_vin_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_wset_vin_fast_override, v, memory_order_release);
}
static inline void aoi_w56_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w56_fault, f, memory_order_release);
}
static inline int aoi_w56_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w56_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W56_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w56_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w56_fault_mode(void) { return 0; }
#endif

static inline int vka_wset_vin_fast_on(void) {
#ifdef T_W56_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_wset_vin_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_WSET_VIN_FAST");
        v = (e && *e && atoi(e) == 0) ? 0 : 1;
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

/* 1. Pre-packed 4-word descriptor writes & 512-bit null-descriptor tracker. */
typedef struct {
    uint64_t pk[512 * 4];
    uint64_t null_img_m[8];
    uint64_t null_buf_m[8];
    uint8_t any_null;
    uint8_t ready;
} vka_wset_fast_t;

static inline void vka_wset_reset(vka_wset_fast_t *wf, uint32_t prev_n) {
    uint32_t n = prev_n > 512u ? 512u : prev_n;
    uint32_t words = n ? ((n + 63u) >> 6) : 8u;
    memset(wf->null_img_m, 0, (size_t)words * sizeof(uint64_t));
    memset(wf->null_buf_m, 0, (size_t)words * sizeof(uint64_t));
    wf->any_null = 0u;
    wf->ready = 1u;
}

static inline uint64_t vka_wset_hdr(uint32_t binding, uint32_t elem, uint32_t dtype) {
    return (uint64_t)binding | ((uint64_t)elem << 16) | ((uint64_t)dtype << 32) | (UINT64_C(1) << 48);
}

static inline void vka_wset_put_buf(vka_wset_fast_t *wf, uint32_t k, uint32_t binding,
                                    uint32_t elem, uint32_t dtype,
                                    uint64_t buf_u, uint64_t off, uint64_t range) {
    uint64_t *p = &wf->pk[(size_t)k * 4u];
    p[0] = vka_wset_hdr(binding, elem, dtype);
    p[1] = buf_u ^ ((aoi_w56_fault_mode() == 1) ? UINT64_C(1) : UINT64_C(0));
    p[2] = off;
    p[3] = range;
    if (!buf_u) {
        wf->null_buf_m[k >> 6] |= UINT64_C(1) << (k & 63u);
        wf->any_null = 1u;
    }
}

static inline void vka_wset_put_img(vka_wset_fast_t *wf, uint32_t k, uint32_t binding,
                                    uint32_t elem, uint32_t dtype,
                                    uint64_t samp_u, uint64_t view_u, uint64_t lay,
                                    int is_sampler_only) {
    uint64_t *p = &wf->pk[(size_t)k * 4u];
    p[0] = vka_wset_hdr(binding, elem, dtype);
    p[1] = samp_u;
    p[2] = view_u;
    p[3] = lay;
    if (!is_sampler_only && !view_u && aoi_w56_fault_mode() != 2) {
        wf->null_img_m[k >> 6] |= UINT64_C(1) << (k & 63u);
        wf->any_null = 1u;
    }
}

static inline void vka_wset_put_tbuf(vka_wset_fast_t *wf, uint32_t k, uint32_t binding,
                                     uint32_t elem, uint32_t dtype, uint64_t bv_u) {
    uint64_t *p = &wf->pk[(size_t)k * 4u];
    p[0] = vka_wset_hdr(binding, elem, dtype);
    p[1] = bv_u;
    p[2] = 0u;
    p[3] = 0u;
}

static inline void vka_wset_patch_buf(vka_wset_fast_t *wf, uint32_t k,
                                      uint64_t buf_u, uint64_t off, uint64_t range) {
    uint64_t *p = &wf->pk[(size_t)k * 4u];
    p[1] = buf_u;
    p[2] = off;
    p[3] = range;
}

static inline void vka_wset_patch_img(vka_wset_fast_t *wf, uint32_t k,
                                      uint64_t view_u, uint64_t lay) {
    uint64_t *p = &wf->pk[(size_t)k * 4u];
    p[2] = view_u;
    p[3] = lay;
}

static inline int vka_wset_pd_match_or_copy(const vka_wset_fast_t *wf, uint32_t nw,
                                            uint64_t layout_u, uint64_t *pd_w, size_t *pd_n) {
    size_t need = 2u + 4u * (size_t)nw;
    size_t bytes = 32u * (size_t)nw;
    if (*pd_n == need && pd_w[0] == layout_u && pd_w[1] == (uint64_t)nw &&
        (!bytes || memcmp(pd_w + 2, wf->pk, bytes) == 0)) {
        return 1;
    }
    pd_w[0] = layout_u;
    pd_w[1] = (uint64_t)nw;
    if (bytes) memcpy(pd_w + 2, wf->pk, bytes);
    *pd_n = need;
    return 0;
}

/* 2. Augmented prefix-max-end index for vka_tex_prepare gw re-uploads. */
static inline void vka_gw_build_max_end(uint64_t *max_end, const uint64_t *addrs,
                                        const uint64_t *sizes, int n) {
    uint64_t cur = 0u;
    int bad = aoi_w56_fault_mode() == 3;
    for (int j = 0; j < n; j++) {
        uint64_t e = addrs[j] + sizes[j];
        if (e < addrs[j]) e = UINT64_MAX;
        if (e > cur) cur = e;
        max_end[j] = bad ? 0u : cur;
    }
}

static inline int vka_gw_upper_addr(const uint64_t *addrs, int n, uint64_t hi) {
    int lo = 0, up = n;
    while (lo < up) {
        int m = lo + ((up - lo) >> 1);
        if (addrs[m] < hi) lo = m + 1;
        else up = m;
    }
    return lo;
}

/* 3. PFB ring busy mask & 256-bit zero-chunk scanner for pfb_consume_locked. */
static inline uint32_t vka_pfb_free_slot(uint32_t busy_mask, uint32_t ring_cap) {
    uint32_t full = (ring_cap >= 32u) ? UINT32_MAX : ((1u << ring_cap) - 1u);
    uint32_t free_m = (~busy_mask) & full;
    return free_m ? (uint32_t)__builtin_ctz(free_m) : ring_cap;
}

static inline uint32_t vka_pfb_next_nonzero_word(const uint32_t *wd, uint32_t nw, uint32_t i) {
    int fault4 = aoi_w56_fault_mode() == 4;
    while (i + 8u <= nw) {
        uint64_t q[4];
        memcpy(q, wd + i, sizeof q);
        if ((q[0] | q[1] | q[2] | q[3]) == 0u || fault4) {
            i += 8u;
            continue;
        }
        break;
    }
    while (i < nw && wd[i] == 0u) i++;
    return i;
}

/* 4. Per-shader interface memoization flags (NDSR & vegetation_cut). */
enum {
    VKA_SH_IF_NDSR_DONE = 1u,
    VKA_SH_IF_NDSR_OK = 2u,
    VKA_SH_IF_VEG_DONE = 4u,
    VKA_SH_IF_VEG_OK = 8u
};

static inline int vka_sh_iface_eval_veg(int raw_ok) {
    int v = raw_ok ? 1 : 0;
    return (aoi_w56_fault_mode() == 5) ? !v : v;
}
