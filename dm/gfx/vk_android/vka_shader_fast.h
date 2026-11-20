// Lock-free, read-clean per-thread specialization & PFB twin memos for vka_shader.c (DM_VK_SHADER_FAST=1).
// Eliminates global mutex contention and linked-list/array scans across per-draw and PSO shader queries.
#ifndef VKA_SHADER_FAST_H
#define VKA_SHADER_FAST_H

#include <assert.h>
#include <stdint.h>

#ifdef W8_TEST_HOOKS
extern int g_w8_fault;
#define VKA_SHADER_FAST_FAULT(n) (g_w8_fault == (n))
#else
#define VKA_SHADER_FAST_FAULT(n) 0
#endif

enum { VKA_SH_SETS = 256, VKA_SH_WAYS = 4, VKA_PFB_SETS = 512 };

typedef struct {
    uint64_t obj, k0, k1, val;
    uint32_t tag;
    uint8_t valid, pad[3];
} vka_sh_pair_ent;

typedef struct {
    vka_sh_pair_ent e[VKA_SH_WAYS];
    uint32_t rr, pad;
} vka_sh_pair_set;

typedef struct {
    vka_sh_pair_set s[VKA_SH_SETS];
} vka_sh_pair64_memo4;

typedef struct {
    uint64_t s, tex_u, tex_s, mod;
    uint32_t vin_u, vin_s, fout_u, fout_s;
    int32_t variant, frz;
    uint8_t valid, pad[7];
} vka_sh_tex_ent;

typedef struct {
    vka_sh_tex_ent e[VKA_SH_WAYS];
    uint32_t rr, pad;
} vka_sh_tex_set;

typedef struct {
    vka_sh_tex_set s[VKA_SH_SETS];
} vka_sh_tex_memo4;

typedef struct {
    uint64_t gen, early, late;
    int32_t kind, pad;
} vka_pfb_ent;

typedef struct {
    vka_pfb_ent e[VKA_SH_WAYS];
    uint32_t rr, pad;
} vka_pfb_bucket;

typedef struct {
    vka_pfb_bucket s[VKA_PFB_SETS];
} vka_pfb_memo4;

typedef struct {
    vka_sh_pair64_memo4 pair;
    vka_sh_tex_memo4 tex;
    vka_pfb_memo4 pfb;
} vka_sh_tls_block;

static inline uint32_t vka_sh_mix32(uint64_t x, uint32_t sets) {
    uint64_t h = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((h ^ (h >> 31)) & (uint64_t)(sets - 1u));
}

static inline uint32_t vka_sh_pair_idx(uint32_t tag, uint64_t obj, uint64_t k0, uint64_t k1) {
    uint64_t h = obj * 0x9e3779b97f4a7c15ull ^ k0 * 0xc2b2ae3d27d4eb4full ^ k1 * 0x165667b19e3779f9ull ^ tag;
    return vka_sh_mix32(h, VKA_SH_SETS);
}

static inline int vka_sh_pair64_find(const vka_sh_pair64_memo4 *m, uint32_t tag, uintptr_t obj,
                                     uint64_t k0, uint64_t k1, uint64_t *val_out) {
    assert(m != NULL && val_out != NULL);
    uint64_t uobj = (uint64_t)obj;
    const vka_sh_pair_set *set = &m->s[vka_sh_pair_idx(tag, uobj, k0, k1)];
    for (uint32_t w = 0; w < VKA_SH_WAYS; w++) {
        const vka_sh_pair_ent *e = &set->e[w];
        if (e->valid && e->tag == tag && e->obj == uobj && e->k0 == k0 &&
            (VKA_SHADER_FAST_FAULT(1) || e->k1 == k1)) {
            *val_out = e->val;
            return 1;
        }
    }
    return 0;
}

static inline void vka_sh_pair64_put(vka_sh_pair64_memo4 *m, uint32_t tag, uintptr_t obj,
                                     uint64_t k0, uint64_t k1, uint64_t val) {
    assert(m != NULL);
    uint64_t uobj = (uint64_t)obj;
    vka_sh_pair_set *set = &m->s[vka_sh_pair_idx(tag, uobj, k0, k1)];
    uint32_t slot = VKA_SH_WAYS;
    for (uint32_t w = 0; w < VKA_SH_WAYS; w++) {
        const vka_sh_pair_ent *e = &set->e[w];
        if (!e->valid || (e->tag == tag && e->obj == uobj && e->k0 == k0 && e->k1 == k1)) { slot = w; break; }
    }
    if (slot == VKA_SH_WAYS) slot = set->rr++ & (VKA_SH_WAYS - 1u);
    set->e[slot] = (vka_sh_pair_ent){uobj, k0, k1, val, tag, 1, {0, 0, 0}};
}

static inline uint32_t vka_sh_tex_idx(const vka_sh_tex_ent *k) {
    uint64_t h = k->s * 0x9e3779b97f4a7c15ull ^ k->tex_u ^ (k->tex_s * 0xc2b2ae3d27d4eb4full) ^
                 ((uint64_t)k->vin_u << 17) ^ ((uint64_t)k->vin_s << 33) ^
                 ((uint64_t)k->fout_u << 7) ^ ((uint64_t)k->fout_s << 23) ^
                 ((uint64_t)(uint32_t)k->variant << 48) ^ (uint64_t)(uint32_t)k->frz;
    return vka_sh_mix32(h, VKA_SH_SETS);
}

static inline int vka_sh_tex_same(const vka_sh_tex_ent *a, const vka_sh_tex_ent *b) {
    return a->valid && a->s == b->s && a->variant == b->variant && a->frz == b->frz &&
           a->vin_u == b->vin_u && a->vin_s == b->vin_s && a->fout_u == b->fout_u &&
           a->fout_s == b->fout_s && a->tex_u == b->tex_u && a->tex_s == b->tex_s;
}

static inline int vka_sh_tex_find(const vka_sh_tex_memo4 *m, const vka_sh_tex_ent *k, uint64_t *mod_out) {
    assert(m != NULL && k != NULL && mod_out != NULL);
    const vka_sh_tex_set *set = &m->s[vka_sh_tex_idx(k)];
    for (uint32_t w = 0; w < VKA_SH_WAYS; w++) {
        if (vka_sh_tex_same(&set->e[w], k)) { *mod_out = set->e[w].mod; return 1; }
    }
    return 0;
}

static inline void vka_sh_tex_put(vka_sh_tex_memo4 *m, const vka_sh_tex_ent *k, uint64_t mod) {
    assert(m != NULL && k != NULL);
    vka_sh_tex_set *set = &m->s[vka_sh_tex_idx(k)];
    uint32_t slot = VKA_SH_WAYS;
    for (uint32_t w = 0; w < VKA_SH_WAYS; w++) {
        if (!set->e[w].valid || vka_sh_tex_same(&set->e[w], k)) { slot = w; break; }
    }
    if (slot == VKA_SH_WAYS) slot = set->rr++ & (VKA_SH_WAYS - 1u);
    vka_sh_tex_ent ent = *k;
    ent.mod = mod;
    ent.valid = 1;
    set->e[slot] = ent;
}

static inline int vka_pfb_memo4_find(const vka_pfb_memo4 *m, uint64_t gen, uint64_t early,
                                     uint64_t *late_out, int *kind_out) {
    assert(m != NULL && late_out != NULL && kind_out != NULL);
    if (!gen || !early) return 0;
    const vka_pfb_bucket *set = &m->s[vka_sh_mix32(early, VKA_PFB_SETS)];
    for (uint32_t w = 0; w < VKA_SH_WAYS; w++) {
        const vka_pfb_ent *e = &set->e[w];
        if (e->early == early && e->gen && (VKA_SHADER_FAST_FAULT(2) || e->gen == gen)) {
            *late_out = e->late;
            *kind_out = e->kind;
            return 1;
        }
    }
    return 0;
}

static inline void vka_pfb_memo4_put(vka_pfb_memo4 *m, uint64_t gen, uint64_t early, uint64_t late, int kind) {
    assert(m != NULL);
    if (!gen || !early) return;
    vka_pfb_bucket *set = &m->s[vka_sh_mix32(early, VKA_PFB_SETS)];
    uint32_t slot = VKA_SH_WAYS;
    for (uint32_t w = 0; w < VKA_SH_WAYS; w++) {
        if (!set->e[w].gen || set->e[w].early == early) { slot = w; break; }
    }
    if (slot == VKA_SH_WAYS) slot = set->rr++ & (VKA_SH_WAYS - 1u);
    set->e[slot] = (vka_pfb_ent){gen, early, late, kind, 0};
}

#endif
