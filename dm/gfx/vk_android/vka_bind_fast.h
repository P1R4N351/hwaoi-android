// Fast paths for c2 slot decoding, driver bank zero-skipping, support template copy, and UBO handle batching (Opt 48).
#ifndef VKA_BIND_FAST_H
#define VKA_BIND_FAST_H
#include "vka_bind.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W46_TEST_HOOKS
extern _Atomic int g_vka_bind_fast_override;
extern _Atomic int g_aoi_w46_fault;
static inline void vka_bind_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_bind_fast_override, v, memory_order_release);
}
static inline void aoi_w46_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w46_fault, f, memory_order_release);
}
static inline int aoi_w46_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w46_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W46_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w46_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w46_fault_mode(void) { return 0; }
#endif

static inline int vka_bind_fast_on(void) {
#ifdef T_W46_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_bind_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_BIND_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_c2_handle_fast(const vka_bstate *s, int st, unsigned b) {
    _Static_assert(DMSH_C2_ROW == 8 && (DMSH_C2_TEX & 7) == 0 && (DMSH_C2_SEPTEX & 7) == 0 && (DMSH_C2_SAMP & 7) == 0, "c2 align");
    if ((b & 7u) != 0u && !(aoi_w46_fault_mode() == 1 && (b & 7u) == 4u)) return 0;
    if (b >= DMSH_C2_TEX && b < DMSH_C2_SEPTEX) return vka_tex_slot(s, st, (b - DMSH_C2_TEX) >> 3);
    if (b >= DMSH_C2_SEPTEX && b < DMSH_C2_SAMP) return s->tex[st][DMSH_SEPTEX_SLOT0 + ((b - DMSH_C2_SEPTEX) >> 3)];
    if (b >= DMSH_C2_SAMP && b < DMSH_C2_SAMP + DMSH_C2_ROW * DMSH_C2_SEP_SLOTS) return s->samp[st][(b - DMSH_C2_SAMP) >> 3];
    return 0;
}

static inline void vka_drv_c0_fast(const vka_bstate *s, int st, uint8_t *d) {
    _Static_assert(DMSH_C0_SAMPLE_TABLE + 16 == DMSH_C0_SAMPLE_INFO, "sample contiguous");
    static const uint8_t k_sample_blk[5][32] = {
        {0x88,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0x00,0,0,0, 0x00,0,0,0, 0,0,0,0, 0,0,0,0},
        {0xCC,0x44,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0x01,0,0,0, 0x01,0,0,0, 0,0,0,0, 0,0,0,0},
        {0x26,0x6E,0xA2,0xEA,0,0,0,0,0,0,0,0,0,0,0,0, 0x03,0,0,0, 0x02,0,0,0, 0,0,0,0, 0,0,0,0},
        {0x59,0xB7,0x9D,0x35,0xD3,0x71,0xFB,0x1F,0,0,0,0,0,0,0,0, 0x07,0,0,0, 0x03,0,0,0, 0,0,0,0, 0,0,0,0},
        {0x99,0x57,0xA5,0x7C,0x63,0xDA,0xBD,0x3B,0xE6,0x18,0x24,0xC2,0x80,0x4F,0xFE,0x01, 0x0F,0,0,0, 0x04,0,0,0, 0,0,0,0, 0,0,0,0},
    };
    int base = st == 0 ? DMSH_C0_SSBO_VS : st == 1 ? DMSH_C0_SSBO_PS : st == 2 ? DMSH_C0_SSBO_GS : st == 5 ? DMSH_C0_SSBO_CS : st == 6 ? DMSH_C0_SSBO_TS : st == 7 ? DMSH_C0_SSBO_MS : -1;
    if (base >= 0) {
        for (int r = 0; r < DMSH_C0_SSBO_SLOTS; r++) {
            uint64_t a = s->ssbo[st][r].addr; uint32_t n = (uint32_t)s->ssbo[st][r].size;
            if ((a | n) && !(aoi_w46_fault_mode() == 2 && r == 15)) {
                memcpy(d + base + DMSH_C0_ROW * r, &a, 8); memcpy(d + base + DMSH_C0_ROW * r + 8, &n, 4);
            }
        }
    }
    if (st != 5) { const float max_point = 2047.0f; memcpy(d + DMSH_C0_POINT_SIZE, &max_point, 4); memcpy(d + DMSH_C0_BASE_INSTANCE, &s->base_instance, 4); }
    if (st == 1) {
        uint32_t smp = s->samples;
        int l = (smp >= 1u && smp <= 16u && !(smp & (smp - 1u))) ? __builtin_ctz(smp) : 0;
        memcpy(d + DMSH_C0_SAMPLE_TABLE, k_sample_blk[l], 32);
    }
    if (st == 5) {
        memcpy(d + DMSH_C0_DISPATCH, s->dispatch, 12);
        if (s->nvn2) memcpy(d + DMSH_C0_DISPATCH_NVN2, s->dispatch, 12);
        for (int r = 0; r < DMSH_C0_CS_UBO_SLOTS; r++) {
            uint64_t a = s->ubo[5][r].addr; uint32_t n = (uint32_t)s->ubo[5][r].size;
            if (a | n) { memcpy(d + DMSH_C0_CS_UBO + DMSH_C0_ROW * r, &a, 8); memcpy(d + DMSH_C0_CS_UBO + DMSH_C0_ROW * r + 8, &n, 4); }
        }
    }
}

static inline uint32_t vka_c2_local_u32(uint64_t h, int tb, int sb) {
    uint32_t lo = (uint32_t)(h & 0xfffffu), hi = (uint32_t)((h >> 20) & 0xfffu);
    return (lo ? (uint32_t)(lo - (uint32_t)tb) : 0u) | (hi ? ((uint32_t)(hi - (uint32_t)sb) << 20) : 0u);
}

static inline void vka_c2_span128_fast(const uint64_t *src, uint8_t *dst_base, int tb, int sb, int raw) {
    for (int g = 0; g < DMSH_C2_SEP_SLOTS; g += 8) {
        const uint64_t *p = src + g;
        if (!(p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7])) continue;
        for (int k = 0; k < 8; k++) {
            uint64_t h = p[k];
            if (!h) continue;
            uint32_t v = raw ? (uint32_t)h : vka_c2_local_u32(h, tb, sb);
            if (v) memcpy(dst_base + DMSH_C2_ROW * (g + k), &v, 4u);
        }
    }
}

static inline void vka_drv_c2_fast(const vka_bstate *s, int st, int pool, uint8_t *d) {
    int tb = pool ? s->tex_base : 0, sb = pool ? s->samp_base : 0;
    if (aoi_w46_fault_mode() == 3 && pool) tb = 0;
    int raw = (tb == 0 && sb == 0);
    const uint64_t *tx = s->tex[st], *im = s->img[st];
    for (int r = 0; r < DMSH_C2_IMG_SLOTS; r++) {
        uint32_t ih = (uint32_t)im[r];
        if (ih) memcpy(d + DMSH_C2_IMG + DMSH_C2_ROW * r, &ih, 4u);
    }
    for (int r = 0; r < DMSH_C2_TEX_SLOTS; r++) {
        if (!tx[r] || (r >= 32 && r - 32 < DMSH_C2_IMG_SLOTS && im[r - 32])) continue;
        uint32_t v = raw ? (uint32_t)tx[r] : vka_c2_local_u32(tx[r], tb, sb);
        if (v) memcpy(d + DMSH_C2_TEX + DMSH_C2_ROW * r, &v, 4u);
    }
    vka_c2_span128_fast(tx + DMSH_SEPTEX_SLOT0, d + DMSH_C2_SEPTEX, tb, sb, raw);
    vka_c2_span128_fast(s->samp[st], d + DMSH_C2_SAMP, tb, sb, raw);
}

static inline void vka_drv_fill_fast(const vka_bstate *s, int st, int bank, int pool, uint8_t *d) {
    if (bank == DMSH_PUSH_BANK) { memcpy(d, s->push[st == 5], DMSH_PUSH_SIZE); return; }
    memset(d, 0, DMSH_DRV_SIZE);
    if (bank == 0) vka_drv_c0_fast(s, st, d);
    else if (bank == 2) vka_drv_c2_fast(s, st, pool, d);
}

static uint8_t g_vka_sup_tpl[VKA_SUPPORT_SIZE];
static pthread_once_t g_vka_sup_once = PTHREAD_ONCE_INIT;

static void vka_sup_init_tpl(void) {
    memset(g_vka_sup_tpl, 0, sizeof g_vka_sup_tpl);
    const float one = 1.0f;
    for (size_t o = 192u; o + 4u <= VKA_SUPPORT_SIZE && o < 192u + 16u * 73u; o += 16u)
        memcpy(g_vka_sup_tpl + o, &one, 4u);
}

static inline void vka_support_fill_fast(uint8_t *d, size_t n) {
    (void)pthread_once(&g_vka_sup_once, vka_sup_init_tpl);
    size_t c = n < VKA_SUPPORT_SIZE ? n : VKA_SUPPORT_SIZE;
    if (c) memcpy(d, g_vka_sup_tpl, c);
    if (n > c) memset(d + c, 0, n - c);
    if (aoi_w46_fault_mode() == 4 && n >= 208u) memset(d + 192u, 0, 4u);
}

static inline int vka_ubo_handles_prefetch(
    const vka_bstate *s, int st, int reg, unsigned off, unsigned cnt,
    int texu_elem, vka_read_fn rd, void *ctx, uint32_t out_w[128]) {
    if (cnt == 0u || cnt > 128u) return -1;
    if (reg < 0 || reg >= 16 || !s->ubo[st][reg].addr || !rd) {
        memset(out_w, 0, (size_t)cnt * 4u);
        return 1;
    }
    uint64_t addr = s->ubo[st][reg].addr, ubo_sz = s->ubo[st][reg].size;
    if (cnt == 1u || !texu_elem) {
        uint32_t w = 0u;
        if ((!ubo_sz || (uint64_t)off + 4u <= ubo_sz) && !rd(ctx, addr + off, &w, 4u)) w = 0u;
        for (unsigned k = 0; k < cnt; k++) out_w[k] = w;
        return 1;
    }
    uint64_t span = (uint64_t)8u * (cnt - 1u) + 4u;
    if (ubo_sz && (uint64_t)off + span > ubo_sz) return -1;
    uint8_t raw[1024];
    if (!rd(ctx, addr + off, raw, (size_t)span)) return -1;
    for (unsigned k = 0; k < cnt; k++) memcpy(&out_w[k], raw + 8u * k, 4u);
    if (aoi_w46_fault_mode() == 5 && cnt > 1u) out_w[cnt - 1u] = out_w[0];
    return 1;
}
#endif
