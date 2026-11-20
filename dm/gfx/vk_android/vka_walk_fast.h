#ifndef VKA_WALK_FAST_H
#define VKA_WALK_FAST_H
/* Wave 10 (Opt 12): 64x4 multi-material stage binding walk front-memo & O(1) resource summary flags (DM_VK_WALK_FAST=1). */
#include "vka_bind.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W10_TEST_HOOKS
extern int g_w10_fault;
#define VKA_WALK_FAULT(n) (g_w10_fault == (n))
#else
#define VKA_WALK_FAULT(n) 0
#endif

enum {
    VKA_WALK_SETS = 64,
    VKA_WALK_WAYS = 4,
    VKA_WALK_SLOTS = 256,
    VKA_WALK_MAX_RES = 48,
    VKA_WALK_SH_SETS = 256
};

enum {
    VKA_WF_TEX_IMG = 1u << 0,
    VKA_WF_TBUF = 1u << 1,
    VKA_WF_FLOAT_TEX = 1u << 2,
    VKA_WF_WRITES_MEM = 1u << 3,
    VKA_WF_GMEM_MAP = 1u << 4,
    VKA_WF_BUF = 1u << 5,
    VKA_WF_FALLBACK = 1u << 7
};

typedef struct {
    uintptr_t e_u, bs_u;
    uint16_t ubo_mask;
    uint8_t cacheable, valid;
} vka_walk_sh_ent;

typedef struct {
    vka_walk_sh_ent e[VKA_WALK_WAYS];
    uint32_t rr, pad;
} vka_walk_sh_set;

typedef struct {
    vka_walk_sh_set s[VKA_WALK_SH_SETS];
} vka_walk_sh_memo4;

typedef struct {
    uintptr_t sh_u, bs_u;
    uint64_t sh_key, s_ssbo, s_tex, ubo_hash, dev_align;
    uint32_t ssbo_align;
    uint16_t ubo_mask;
    uint8_t st, n, pool, fl, valid, texu_elem;
} vka_walk_meta_ent;

typedef struct {
    vka_walk_meta_ent e[VKA_WALK_WAYS];
    uint32_t rr, pad;
} vka_walk_meta_set;

typedef struct {
    vka_walk_meta_set s[VKA_WALK_SETS];
} vka_walk_meta4;

typedef struct {
    vka_range ubo[16];
    vka_res r[VKA_WALK_MAX_RES];
} vka_walk_slot;

static inline int vka_walk_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_WALK_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline uint8_t vka_walk_summarize(const vka_res *r, int n) {
    if (n < 0) return VKA_WF_FALLBACK;
    assert(n == 0 || r != NULL);
    assert(n <= 512);
    uint8_t fl = 0;
    for (int i = 0; i < n; i++) {
        int tex_img = (r[i].what == VKA_R_TEX || r[i].what == VKA_R_IMG) && r[i].tid != 0;
        if (tex_img) {
            fl |= VKA_WF_TEX_IMG;
            if (!(r[i].dim & (DMSH_VK_DIM_SINT | DMSH_VK_DIM_UINT))) fl |= VKA_WF_FLOAT_TEX;
        }
        if (r[i].what == VKA_R_TBUF && r[i].tid != 0) fl |= VKA_WF_TBUF;
        if ((r[i].flags & DMSH_VK_BF_RW) || r[i].what == VKA_R_IMG || r[i].what == VKA_R_GMEM_LOG) fl |= VKA_WF_WRITES_MEM;
        if (r[i].what == VKA_R_GMEM_MAP) fl |= VKA_WF_GMEM_MAP;
        if (r[i].what == VKA_R_BUF) fl |= VKA_WF_BUF;
    }
    if (VKA_WALK_FAULT(3)) fl &= (uint8_t)~(VKA_WF_WRITES_MEM | VKA_WF_FLOAT_TEX);
    return fl;
}

static inline int vka_walk_bind_uncacheable(const dmsh_vk_bind *b) {
    uint8_t k = b->kind, sk = b->skind;
    return k == DMSH_VK_KIND_UNKNOWN || k == DMSH_VK_KIND_SBUF_UBO || k == DMSH_VK_KIND_TEX_UBO ||
           k == DMSH_VK_KIND_IMG_UBO || k == DMSH_VK_KIND_SAMP_UBO || k == DMSH_VK_KIND_TEX_C2A ||
           k == DMSH_VK_KIND_SAMP_C2A || sk == DMSH_VK_KIND_SAMP_UBO || sk == DMSH_VK_KIND_SAMP_C2A;
}

static inline uint8_t vka_walk_compute_traits(const dmsh_vk_entry *e, const dmsh_vk_bind *bs, uint16_t *ubo_mask_out) {
    assert(e != NULL && ubo_mask_out != NULL);
    if (e->bind_count > 512u || (e->bind_count > 0 && !bs)) { *ubo_mask_out = 0; return 0; }
    uint32_t total = 0;
    uint16_t um = 0;
    for (unsigned i = 0; i < e->bind_count; i++) {
        const dmsh_vk_bind *b = &bs[i];
        if (b->set == DMSH_VK_SET_POOL || b->kind == DMSH_VK_KIND_TEX_POOL || b->kind == DMSH_VK_KIND_SAMP_POOL) continue;
        total += b->count ? b->count : 1u;
        if (total > VKA_WALK_MAX_RES || vka_walk_bind_uncacheable(b)) { *ubo_mask_out = 0; return 0; }
        if ((b->kind == DMSH_VK_KIND_CBUF || b->kind == DMSH_VK_KIND_UBO_PTR) &&
            !(b->flags & DMSH_VK_BF_NOSLOT) && b->reg >= 0 && b->reg < 16) {
            um |= (uint16_t)(1u << b->reg);
        }
    }
    *ubo_mask_out = um;
    return 1;
}

static inline int vka_walk_shader_traits(vka_walk_sh_memo4 *sm, const dmsh_vk_entry *e, const dmsh_vk_bind *bs, uint16_t *ubo_mask_out) {
    assert(sm != NULL && e != NULL && ubo_mask_out != NULL);
    uintptr_t eu = (uintptr_t)e, bu = (uintptr_t)bs;
    uint32_t idx = (uint32_t)(((eu ^ (bu >> 4)) * 0x9e3779b97f4a7c15ull) >> 56) & (VKA_WALK_SH_SETS - 1u);
    vka_walk_sh_set *set = &sm->s[idx];
    uint32_t repl = VKA_WALK_WAYS;
    for (uint32_t w = 0; w < VKA_WALK_WAYS; w++) {
        const vka_walk_sh_ent *en = &set->e[w];
        if (en->valid && en->e_u == eu && en->bs_u == bu) { *ubo_mask_out = en->ubo_mask; return (int)en->cacheable; }
        if (!en->valid && repl == VKA_WALK_WAYS) repl = w;
    }
    if (repl == VKA_WALK_WAYS) repl = set->rr++ & (VKA_WALK_WAYS - 1u);
    uint16_t um = 0;
    uint8_t ok = vka_walk_compute_traits(e, bs, &um);
    set->e[repl] = (vka_walk_sh_ent){eu, bu, um, ok, 1};
    *ubo_mask_out = um;
    return (int)ok;
}

static inline uint64_t vka_walk_ubo_hash(const vka_range ubo[16], uint16_t ubo_mask) {
    assert(ubo != NULL);
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t m = ubo_mask; m; m &= m - 1u) {
        uint32_t r = (uint32_t)__builtin_ctz(m);
        h = (h ^ ((uint64_t)r << 56) ^ ubo[r].addr) * 0x9e3779b97f4a7c15ull;
        h = (h ^ ubo[r].size) * 0xbf58476d1ce4e5b9ull;
    }
    return h ^ (h >> 31);
}

static inline int vka_walk_ubo_eq(const vka_range a[16], const vka_range b[16], uint16_t ubo_mask) {
    for (uint32_t m = ubo_mask; m; m &= m - 1u) {
        uint32_t r = (uint32_t)__builtin_ctz(m);
        if (a[r].addr != b[r].addr || a[r].size != b[r].size) return 0;
    }
    return 1;
}

static inline uint32_t vka_walk_idx(uint64_t sh_key, uint8_t st, uint64_t s_ssbo, uint64_t s_tex, uint64_t ubo_hash) {
    uint64_t x = (sh_key * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)st * 0xbf58476d1ce4e5b9ull) ^ (s_ssbo * 0x94d049bb133111ebull);
    if (!VKA_WALK_FAULT(2)) x ^= s_tex * 0xc2b2ae3d27d4eb4full;
    if (!VKA_WALK_FAULT(1)) x ^= ubo_hash * 0x100000001b3ull;
    return (uint32_t)(((x ^ (x >> 31)) >> 4) & (VKA_WALK_SETS - 1u));
}

static inline int vka_walk_memo4_find(const vka_walk_meta4 *m, const vka_walk_slot *slots,
                                      uintptr_t sh_u, uintptr_t bs_u, uint64_t sh_key, uint8_t st,
                                      uint64_t s_ssbo, uint64_t s_tex, const vka_range ubo[16], uint16_t ubo_mask,
                                      uint32_t ssbo_align, uint64_t dev_align, uint8_t texu_elem,
                                      vka_res *out_r, int *n_out, int *pool_out, uint8_t *fl_out) {
    assert(m != NULL && slots != NULL && out_r != NULL && n_out != NULL && pool_out != NULL && fl_out != NULL);
    if (!s_ssbo || !s_tex) return 0;
    uint64_t ubo_hash = vka_walk_ubo_hash(ubo, ubo_mask);
    uint32_t set_idx = vka_walk_idx(sh_key, st, s_ssbo, s_tex, ubo_hash);
    const vka_walk_meta_set *set = &m->s[set_idx];
    for (uint32_t w = 0; w < VKA_WALK_WAYS; w++) {
        const vka_walk_meta_ent *e = &set->e[w];
        uint32_t slot = set_idx * VKA_WALK_WAYS + w;
        if (!e->valid || e->sh_u != sh_u || e->bs_u != bs_u || e->sh_key != sh_key || e->st != st ||
            e->s_ssbo != s_ssbo || e->ubo_mask != ubo_mask || e->ssbo_align != ssbo_align ||
            e->dev_align != dev_align || e->texu_elem != texu_elem) continue;
        if (!VKA_WALK_FAULT(2) && e->s_tex != s_tex) continue;
        if (!VKA_WALK_FAULT(1) && (e->ubo_hash != ubo_hash || !vka_walk_ubo_eq(slots[slot].ubo, ubo, ubo_mask))) continue;
        if (e->n > 0) memcpy(out_r, slots[slot].r, (size_t)e->n * sizeof(vka_res));
        *n_out = (int)e->n; *pool_out = (int)e->pool; *fl_out = e->fl;
        return 1;
    }
    return 0;
}

static inline void vka_walk_memo4_put(vka_walk_meta4 *m, vka_walk_slot *slots,
                                      uintptr_t sh_u, uintptr_t bs_u, uint64_t sh_key, uint8_t st,
                                      uint64_t s_ssbo, uint64_t s_tex, const vka_range ubo[16], uint16_t ubo_mask,
                                      uint32_t ssbo_align, uint64_t dev_align, uint8_t texu_elem,
                                      const vka_res *r, int n, int pool, uint8_t fl) {
    assert(m != NULL && slots != NULL);
    if (!s_ssbo || !s_tex || n < 0 || n > VKA_WALK_MAX_RES) return;
    uint64_t ubo_hash = vka_walk_ubo_hash(ubo, ubo_mask);
    uint32_t set_idx = vka_walk_idx(sh_key, st, s_ssbo, s_tex, ubo_hash);
    vka_walk_meta_set *set = &m->s[set_idx];
    uint32_t way = VKA_WALK_WAYS;
    for (uint32_t w = 0; w < VKA_WALK_WAYS; w++) {
        const vka_walk_meta_ent *e = &set->e[w];
        if (!e->valid || (e->sh_u == sh_u && e->bs_u == bs_u && e->sh_key == sh_key && e->st == st &&
            e->s_ssbo == s_ssbo && e->s_tex == s_tex && e->ubo_hash == ubo_hash && e->ubo_mask == ubo_mask)) { way = w; break; }
    }
    if (way == VKA_WALK_WAYS) way = set->rr++ & (VKA_WALK_WAYS - 1u);
    uint32_t slot = set_idx * VKA_WALK_WAYS + way;
    for (uint32_t um = ubo_mask; um; um &= um - 1u) {
        uint32_t idx = (uint32_t)__builtin_ctz(um);
        slots[slot].ubo[idx] = ubo[idx];
    }
    if (n > 0) memcpy(slots[slot].r, r, (size_t)n * sizeof(vka_res));
    set->e[way] = (vka_walk_meta_ent){
        sh_u, bs_u, sh_key, s_ssbo, s_tex, ubo_hash, dev_align,
        ssbo_align, ubo_mask, st, (uint8_t)n, (uint8_t)(pool != 0), fl, 1, texu_elem
    };
}

#endif
