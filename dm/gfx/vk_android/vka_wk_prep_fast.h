// Fast O(1) stage resource masks, vbuf mask, FB bloom, region-write index & copy-back wait list (Opt 57).
#ifndef VKA_WK_PREP_FAST_H
#define VKA_WK_PREP_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W55_TEST_HOOKS
extern _Atomic int g_vka_wk_prep_fast_override;
extern _Atomic int g_aoi_w55_fault;
static inline void vka_wk_prep_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_wk_prep_fast_override, v, memory_order_release);
}
static inline void aoi_w55_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w55_fault, f, memory_order_release);
}
static inline int aoi_w55_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w55_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W55_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w55_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w55_fault_mode(void) { return 0; }
#endif

static inline int vka_wk_prep_fast_on(void) {
#ifdef T_W55_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_wk_prep_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_WK_PREP_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_wp_mix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

/* 1. Per-stage 64-bit resource classification masks (0..64 entries). */
typedef struct {
    uint64_t tex_img_m;
    uint64_t tex_only_m;
    uint64_t tex_tbuf_m;
    uint64_t float_tex_m;
    uint64_t buf_m;
    uint8_t ready;
} vka_wk_masks_t;

static inline void vka_wk_masks_add(vka_wk_masks_t *mk, int i, int is_tex, int is_img,
                                    int is_tbuf, int is_buf, int has_tid, int is_float_tex) {
    if (!mk || i < 0 || i >= 64) return;
    uint64_t bit = UINT64_C(1) << (unsigned)i;
    if ((is_tex || is_img) && has_tid) {
        mk->tex_img_m |= bit;
        if (is_float_tex) mk->float_tex_m |= bit;
    }
    if (is_tex && has_tid) mk->tex_only_m |= bit;
    if ((is_tex || is_tbuf) && has_tid) mk->tex_tbuf_m |= bit;
    if (is_buf) mk->buf_m |= bit;
}

static inline void vka_wk_masks_finish(vka_wk_masks_t *mk, int n) {
    if (!mk) return;
    if (aoi_w55_fault_mode() == 1) { mk->tex_tbuf_m = 0u; mk->buf_m = 0u; }
    mk->ready = (n >= 0 && n <= 64) ? 1u : 0u;
}

/* 2. 16-bit non-zero vertex-buffer address mask for ub_hazard. */
static inline uint16_t vka_vbuf_mask_patch(uint16_t cur, int first, int n, uint16_t nz_bits) {
    if (first < 0 || n <= 0 || first + n > 16) return cur;
    uint16_t clr = (uint16_t)(((1u << n) - 1u) << first);
    uint16_t out = (uint16_t)((cur & ~clr) | (nz_bits & clr));
    if (aoi_w55_fault_mode() == 2) out &= 0x5555u;
    return out;
}

/* 3. 64-bit 2-probe render-pass attachment pointer bloom for in_fb / view_of. */
typedef struct {
    uint64_t bloom;
    uint8_t count;
} vka_fb_filter_t;

static inline uint64_t vka_fb_ptr_bits(uintptr_t ptr) {
    uint64_t h = vka_wp_mix64((uint64_t)ptr);
    return (UINT64_C(1) << (h & 63u)) | (UINT64_C(1) << ((h >> 32u) & 63u));
}

static inline void vka_fb_filter_set(vka_fb_filter_t *f, const uintptr_t *ims, int n) {
    if (!f) return;
    uint64_t b = 0u;
    int lim = n > 9 ? 9 : (n < 0 ? 0 : n);
    for (int i = 0; i < lim; i++) if (ims[i]) b |= vka_fb_ptr_bits(ims[i]);
    f->bloom = b;
    f->count = (uint8_t)lim;
}

static inline int vka_fb_may_contain(const vka_fb_filter_t *f, uintptr_t im) {
    if (aoi_w55_fault_mode() == 3) return 0;
    if (!f || !im || !f->count) return 0;
    uint64_t bits = vka_fb_ptr_bits(im);
    return (f->bloom & bits) == bits;
}

/* 4. 256-bucket (im, level, layer) hash table + 8x8 tile masks for tex_reg_writes_flush_locked. */
enum { VKA_REGW_CAP = 1024u, VKA_REGW_BUCKETS = 256u, VKA_REGW_BMASK = 255u };
typedef struct {
    uint64_t im_u, tiles;
    uint32_t level, layer;
    int32_t head;
    uint8_t used;
} vka_regw_grp_t;

typedef struct {
    vka_regw_grp_t grp[VKA_REGW_BUCKETS];
    int32_t prev_same[VKA_REGW_CAP];
    uint64_t wtiles[VKA_REGW_CAP];
} vka_regw_idx_t;

static inline void vka_regw_idx_reset(vka_regw_idx_t *x) {
    if (!x) return;
    memset(x->grp, 0, sizeof x->grp);
}

static inline uint64_t vka_regw_tile_mask(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!w || !h || x < 0 || y < 0) return UINT64_MAX;
    uint64_t x0 = (uint64_t)x >> 5u, x1 = ((uint64_t)x + w - 1u) >> 5u;
    uint64_t y0 = (uint64_t)y >> 5u, y1 = ((uint64_t)y + h - 1u) >> 5u;
    if (x1 - x0 >= 7u) return UINT64_MAX;
    uint64_t xm = 0u, ym = 0u;
    for (uint64_t tx = x0; tx <= x1; tx++) xm |= UINT64_C(1) << (tx & 7u);
    uint64_t yr = y1 - y0 >= 7u ? 7u : (y1 - y0);
    for (uint64_t dy = 0u; dy <= yr; dy++) ym |= xm << (((y0 + dy) & 7u) * 8u);
    return ym ? ym : UINT64_MAX;
}

static inline int vka_regw_find_grp(const vka_regw_idx_t *x, uint64_t im_u, uint32_t lv, uint32_t ly) {
    uint64_t h = vka_wp_mix64(im_u ^ ((uint64_t)lv << 32u) ^ ((uint64_t)ly * UINT64_C(0x9e3779b185ebca87)));
    uint32_t s = (uint32_t)h & VKA_REGW_BMASK;
    for (uint32_t p = 0u; p < VKA_REGW_BUCKETS; p++) {
        uint32_t idx = (s + p) & VKA_REGW_BMASK;
        const vka_regw_grp_t *g = &x->grp[idx];
        if (!g->used) return -1;
        if (g->im_u == im_u && g->level == lv && g->layer == ly) return (int)idx;
    }
    return -1;
}

static inline int vka_regw_first_cand(const vka_regw_idx_t *x, uint64_t im_u,
                                      uint32_t lv, uint32_t ly, uint64_t tmask) {
    int gi = vka_regw_find_grp(x, im_u, lv, ly);
    if (gi < 0 || (x->grp[gi].tiles & tmask) == 0u) return -1;
    return x->grp[gi].head;
}

static inline int vka_regw_next_cand(const vka_regw_idx_t *x, int j, uint64_t tmask) {
    for (int cur = (j >= 0 && j < (int)VKA_REGW_CAP) ? x->prev_same[j] : -1; cur >= 0; cur = x->prev_same[cur]) {
        if ((x->wtiles[cur] & tmask) != 0u) return cur;
    }
    return -1;
}

static inline void vka_regw_insert(vka_regw_idx_t *x, int i, uint64_t im_u,
                                   uint32_t lv, uint32_t ly, uint64_t tmask) {
    if (!x || i < 0 || i >= (int)VKA_REGW_CAP) return;
    uint64_t h = vka_wp_mix64(im_u ^ ((uint64_t)lv << 32u) ^ ((uint64_t)ly * UINT64_C(0x9e3779b185ebca87)));
    uint32_t s = (uint32_t)h & VKA_REGW_BMASK;
    for (uint32_t p = 0u; p < VKA_REGW_BUCKETS; p++) {
        uint32_t idx = (s + p) & VKA_REGW_BMASK;
        vka_regw_grp_t *g = &x->grp[idx];
        if (!g->used) {
            g->im_u = im_u; g->level = lv; g->layer = ly; g->head = i; g->used = 1u;
            g->tiles = (aoi_w55_fault_mode() == 4) ? 0u : tmask;
            x->prev_same[i] = -1; x->wtiles[i] = tmask;
            return;
        }
        if (g->im_u == im_u && g->level == lv && g->layer == ly) {
            x->prev_same[i] = g->head; x->wtiles[i] = tmask; g->head = i;
            if (aoi_w55_fault_mode() != 4) g->tiles |= tmask;
            return;
        }
    }
}

/* 5. Compact 64-slot copy-back wait list for non-full-scan vka_tex_prepare. */
enum { VKA_WBF_WAIT_CAP = 64u };
typedef struct {
    void *ims[VKA_WBF_WAIT_CAP];
    uint8_t *ix_ptrs[VKA_WBF_WAIT_CAP];
    uint32_t n;
    uint8_t overflow;
} vka_wbf_wait_list_t;

static inline void vka_wbf_list_add(vka_wbf_wait_list_t *l, void *im, uint8_t *ix_slot) {
    if (!l || !im || !ix_slot) return;
    if (aoi_w55_fault_mode() == 5) return;
    if (*ix_slot != 0u || l->overflow) return;
    if (l->n >= VKA_WBF_WAIT_CAP) { l->overflow = 1u; return; }
    uint32_t pos = l->n++;
    l->ims[pos] = im;
    l->ix_ptrs[pos] = ix_slot;
    *ix_slot = (uint8_t)(pos + 1u);
}

static inline void vka_wbf_list_del(vka_wbf_wait_list_t *l, uint8_t *ix_slot) {
    if (!l || !ix_slot || *ix_slot == 0u) return;
    uint32_t idx = (uint32_t)*ix_slot - 1u;
    *ix_slot = 0u;
    if (idx >= l->n) return;
    uint32_t last = --l->n;
    if (idx < last) {
        l->ims[idx] = l->ims[last];
        l->ix_ptrs[idx] = l->ix_ptrs[last];
        *l->ix_ptrs[idx] = (uint8_t)(idx + 1u);
    }
    l->ims[last] = NULL;
    l->ix_ptrs[last] = NULL;
}

static inline void vka_wbf_list_clear(vka_wbf_wait_list_t *l) {
    if (!l) return;
    for (uint32_t i = 0u; i < l->n && i < VKA_WBF_WAIT_CAP; i++) {
        if (l->ix_ptrs[i]) *l->ix_ptrs[i] = 0u;
        l->ims[i] = NULL;
        l->ix_ptrs[i] = NULL;
    }
    l->n = 0u;
    l->overflow = 0u;
}

#endif
