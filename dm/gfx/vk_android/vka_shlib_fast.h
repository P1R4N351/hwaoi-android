// Fast shader-library lookup & SPIR-V patching/analysis helpers (DM_VK_SHLIB_FAST=1, default on; =0 disables).
#pragma once
#include "../nvn/dmsh_abi_vk.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { VKA_FOUT_STACK_BOUND = 256u, VKA_PFB_STACK_IDS = 32u };

#ifdef T_W33_TEST_HOOKS
extern _Atomic int g_vka_shlib_fast_state;
extern _Atomic int g_vka_w33_fault;
static inline void vka_shlib_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_shlib_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w33_set_fault(int mode) {
    atomic_store_explicit(&g_vka_w33_fault, mode, memory_order_release);
}
static inline int vka_w33_fault(void) {
    return atomic_load_explicit(&g_vka_w33_fault, memory_order_acquire);
}
#else
static _Atomic int g_vka_shlib_fast_state;
static inline int vka_w33_fault(void) { return 0; }
#endif

static inline int vka_shlib_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_shlib_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_SHLIB_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_shlib_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline uint64_t vka_shlib_key_fast(const uint8_t md[8], int fault_mode) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint64_t k = 0;
    memcpy(&k, md, sizeof k);
    return fault_mode == 1 ? k : __builtin_bswap64(k);
#else
    uint64_t k = 0;
    for (int i = 0; i < 8; i++) k = (k << 8) | md[i];
    return fault_mode == 1 ? __builtin_bswap64(k) : k;
#endif
}

static inline const dmsh_vk_entry *vka_shlib_find_fast(const dmsh_vk_entry *ent, uint32_t count,
                                                       uint64_t key, int fault_mode) {
    if (!ent || !count) return NULL;
    uint32_t n = (fault_mode == 2 && count > 1u) ? (count - 1u) : count;
    const dmsh_vk_entry *base = ent;
    for (unsigned step = 0; step < 32u && n > 1u; step++) {
        uint32_t half = n >> 1;
        __builtin_prefetch(&base[half >> 1], 0, 1);
        __builtin_prefetch(&base[half + (half >> 1)], 0, 1);
        base = (base[half].key <= key) ? (base + half) : base;
        n -= half;
    }
    return (base->key == key) ? base : NULL;
}

static inline int vka_spv_vec_reserve(uint32_t **w, size_t *cap, size_t want) {
    if (!w || !cap) return 0;
    if (want <= *cap) return 1;
    size_t c = *cap ? *cap : 4096u;
    for (unsigned step = 0; step < 32u && c < want; step++) {
        if (c > SIZE_MAX / 2u) { c = want; break; }
        c *= 2u;
    }
    if (c < want) c = want;
    if (c > SIZE_MAX / sizeof(uint32_t)) return 0;
    uint32_t *p = (uint32_t *)realloc(*w, c * sizeof(uint32_t));
    if (!p) return 0;
    *w = p;
    *cap = c;
    return 1;
}

static inline int vka_spv_put_words(uint32_t **w, size_t *n, size_t *cap,
                                    const uint32_t *x, size_t k, int fault_mode) {
    if (!w || !n || !cap) return 0;
    if (!k) return 1;
    if (!x || *n > SIZE_MAX - k) return 0;
    size_t need = *n + k;
    if (need > *cap && !vka_spv_vec_reserve(w, cap, need)) return 0;
    memcpy(*w + *n, x, k * sizeof(uint32_t));
    *n = need;
    if (fault_mode == 3 && k >= 4u) (*w)[*n - 1u] ^= 1u;
    return 1;
}

typedef struct {
    uint32_t *loc, *ty, *ptee, *cval;
    uint8_t *isc, *var;
    uint8_t ncomp[8], active_mask, full_mask;
    size_t first_fn;
    int pre_fn_op;
} vka_fout_ctx;

static inline int vka_fout_pass1(const uint32_t *w, size_t nw, uint32_t bound,
                                 uint8_t comp[8], vka_fout_ctx *c) {
    enum { DecComponent = 2, DecLocation = 30, OpTypeInt = 21, OpTypeFloat = 22,
           OpTypeVector = 23, OpTypePointer = 32, OpConstant = 43, OpFunction = 54,
           OpVariable = 59, OpLoad = 61, OpDecorate = 71, OpName = 5,
           OpEntryPoint = 15, SCOutput = 3 };
    c->first_fn = nw;
    for (size_t i = 5; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) return -1;
        if (c->first_fn == nw && op == OpFunction) c->first_fn = i;
        else if (c->first_fn == nw && op > 17u && op != OpVariable && op != OpDecorate &&
                 op != OpName && op != OpEntryPoint && op != OpLoad && op != OpConstant) c->pre_fn_op = 1;
        if (op == OpDecorate && len >= 4u && w[i + 1] < bound) {
            if (w[i + 2] == DecLocation) c->loc[w[i + 1]] = w[i + 3];
            else if (w[i + 2] == DecComponent) return -1;
        } else if (op == OpTypeFloat && len >= 3u && w[i + 2] == 32u && w[i + 1] < bound) {
            c->ty[w[i + 1]] = 1u;
        } else if (op == OpTypeInt && len >= 4u && w[i + 2] == 32u && w[i + 1] < bound) {
            c->ty[w[i + 1]] = 1u;
        } else if (op == OpTypeVector && len >= 4u && w[i + 2] < bound &&
                   c->ty[w[i + 2]] && w[i + 1] < bound) {
            c->ty[w[i + 1]] = w[i + 3];
        } else if (op == OpTypePointer && len >= 4u && w[i + 2] == SCOutput && w[i + 1] < bound) {
            c->ptee[w[i + 1]] = w[i + 3];
        } else if (op == OpConstant && len >= 4u && w[i + 2] < bound) {
            c->isc[w[i + 2]] = 1u; c->cval[w[i + 2]] = w[i + 3];
        } else if (op == OpVariable && len >= 4u && w[i + 3] == SCOutput &&
                   w[i + 1] < bound && w[i + 2] < bound) {
            uint32_t id = w[i + 2], l = c->loc[id], pt = c->ptee[w[i + 1]];
            uint32_t n = pt < bound ? c->ty[pt] : 0u;
            if (l < 8u && n >= 1u && n <= 4u) {
                uint8_t all = (uint8_t)((1u << n) - 1u);
                c->var[id] = 1u; c->ncomp[l] = (uint8_t)n; c->active_mask |= (uint8_t)(1u << l);
                if (len > 4u) { comp[l] = all; c->full_mask |= (uint8_t)(1u << l); }
            }
        }
        i += len;
    }
    return 0;
}

static inline void vka_fout_pass2(const uint32_t *w, size_t nw, uint32_t bound,
                                  uint8_t comp[8], vka_fout_ctx *c) {
    enum { OpName = 5, OpEntryPoint = 15, OpConstant = 43, OpVariable = 59,
           OpLoad = 61, OpAccessChain = 65, OpInBoundsAccessChain = 66, OpDecorate = 71 };
    size_t start = (!c->pre_fn_op && c->first_fn < nw) ? c->first_fn : 5u;
    for (size_t i = start; i < nw && c->full_mask != c->active_mask;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (op != OpVariable && op != OpDecorate && op != OpName && op != OpEntryPoint &&
            op != OpLoad && op != OpConstant && op > 17u) {
            int chain = (op == OpAccessChain || op == OpInBoundsAccessChain) && len >= 5u;
            for (uint32_t k = 1u; k < len; k++) {
                uint32_t id = w[i + k];
                if (id >= bound || !c->var[id]) continue;
                uint32_t l = c->loc[id];
                uint8_t all = (uint8_t)((1u << c->ncomp[l]) - 1u);
                if (chain && k == 3u) {
                    uint32_t x = w[i + 4];
                    comp[l] |= (uint8_t)((x < bound && c->isc[x] && c->cval[x] < c->ncomp[l])
                                         ? (1u << c->cval[x]) : all);
                } else comp[l] |= all;
                if ((comp[l] & all) == all) c->full_mask |= (uint8_t)(1u << l);
            }
        }
        i += len;
    }
}

static inline int vka_spv_fout_comps_fast(const uint32_t *w, size_t nw,
                                          uint8_t comp[8], int fault_mode) {
    memset(comp, 0, 8);
    if (!w || nw < 5u || w[0] != 0x07230203u) return -1;
    uint32_t bound = w[3];
    uint32_t s_loc[VKA_FOUT_STACK_BOUND], s_ty[VKA_FOUT_STACK_BOUND];
    uint32_t s_ptee[VKA_FOUT_STACK_BOUND], s_cval[VKA_FOUT_STACK_BOUND];
    uint8_t s_isc[VKA_FOUT_STACK_BOUND], s_var[VKA_FOUT_STACK_BOUND];
    uint8_t *heap = NULL;
    vka_fout_ctx ctx = {0};
    if (bound <= VKA_FOUT_STACK_BOUND) {
        ctx.loc = s_loc; ctx.ty = s_ty; ctx.ptee = s_ptee; ctx.cval = s_cval;
        ctx.isc = s_isc; ctx.var = s_var;
        if (bound) {
            memset(ctx.loc, 0xff, (size_t)bound * 4u); memset(ctx.ty, 0, (size_t)bound * 4u);
            memset(ctx.ptee, 0, (size_t)bound * 4u); memset(ctx.cval, 0, (size_t)bound * 4u);
            memset(ctx.isc, 0, bound); memset(ctx.var, 0, bound);
        }
    } else {
        if (bound > (UINT32_C(1) << 26)) return -1;
        heap = (uint8_t *)calloc(bound, 18u);
        if (!heap) return -1;
        ctx.loc = (uint32_t *)heap; ctx.ty = ctx.loc + bound;
        ctx.ptee = ctx.ty + bound; ctx.cval = ctx.ptee + bound;
        ctx.isc = (uint8_t *)(ctx.cval + bound); ctx.var = ctx.isc + bound;
        memset(ctx.loc, 0xff, (size_t)bound * 4u);
    }
    int rc = vka_fout_pass1(w, nw, bound, comp, &ctx);
    if (rc == 0 && ctx.active_mask && ctx.full_mask != ctx.active_mask) {
        vka_fout_pass2(w, nw, bound, comp, &ctx);
    }
    if (rc == 0 && fault_mode == 4) comp[0] &= (uint8_t)~1u;
    free(heap);
    return rc;
}

static inline int vka_pfb_id_in(const uint32_t *arr, uint32_t n, uint32_t id) {
    for (uint32_t i = 0; i < n; i++) if (arr[i] == id) return 1;
    return 0;
}

static inline int vka_spv_pool_fb_prescan(const uint32_t *w, size_t nw, uint32_t set,
                                          uint32_t tex_binding, int fault_mode) {
    enum { OpEntryPoint = 15, OpVariable = 59, OpDecorate = 71,
           DecBinding = 33, DecDescriptorSet = 34 };
    if (!w || nw < 5u || set == UINT32_MAX || tex_binding == UINT32_MAX) return 1;
    uint32_t bound = w[3], model = UINT32_MAX;
    uint32_t set_ids[VKA_PFB_STACK_IDS], bind_ids[VKA_PFB_STACK_IDS];
    uint32_t dset_seen[VKA_PFB_STACK_IDS], dbind_seen[VKA_PFB_STACK_IDS];
    uint32_t nset = 0, nbind = 0, ns_all = 0, nb_all = 0;
    int cand = 0, ov = 0;
    for (size_t i = 5; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) return 1;
        if (op == OpEntryPoint && len >= 3u) {
            if (model == UINT32_MAX) model = w[i + 1];
            else if (model != w[i + 1]) return 1;
        } else if (op == OpDecorate && len >= 4u && w[i + 1] < bound) {
            uint32_t id = w[i + 1];
            if (w[i + 2] == DecDescriptorSet) {
                if (vka_pfb_id_in(dset_seen, ns_all, id)) ov = 1;
                if (ns_all < VKA_PFB_STACK_IDS) dset_seen[ns_all++] = id; else ov = 1;
                if (w[i + 3] == set) {
                    if (nset < VKA_PFB_STACK_IDS) set_ids[nset++] = id; else ov = 1;
                }
            } else if (w[i + 2] == DecBinding) {
                if (vka_pfb_id_in(dbind_seen, nb_all, id)) ov = 1;
                if (nb_all < VKA_PFB_STACK_IDS) dbind_seen[nb_all++] = id; else ov = 1;
                if (w[i + 3] == tex_binding) {
                    if (nbind < VKA_PFB_STACK_IDS) bind_ids[nbind++] = id; else ov = 1;
                }
            }
        } else if (op == OpVariable && len >= 4u && w[i + 2] < bound) {
            if (ov || (vka_pfb_id_in(set_ids, nset, w[i + 2]) &&
                       vka_pfb_id_in(bind_ids, nbind, w[i + 2]))) cand = 1;
        }
        i += len;
    }
    if (fault_mode == 5) return 0;
    return (cand || ov) ? 1 : 0;
}
