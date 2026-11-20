#ifndef VKA_CS_FAST_H
#define VKA_CS_FAST_H
/* Wave 13 (Opt 15): Compute dispatch front-memo, pipe elision & gen-cached diag filters (DM_VK_CS_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W13_TEST_HOOKS
extern int g_w13_fault;
#define VKA_CS_FAULT(n) (g_w13_fault == (n))
#else
#define VKA_CS_FAULT(n) 0
#endif

static inline int vka_cs_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_CS_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

typedef struct {
    uint64_t cb_u, pipe_u;
    int valid, pad;
} vka_cs_pipe_state;

static inline void vka_cs_pipe_reset(vka_cs_pipe_state *s) {
    assert(s != NULL);
    s->valid = 0;
    s->pipe_u = 0;
}

static inline int vka_cs_pipe_bind_needed(vka_cs_pipe_state *s, int enabled,
                                          uint64_t cb_u, uint64_t pipe_u) {
    assert(s != NULL);
    if (!enabled) {
        vka_cs_pipe_reset(s);
        return 1;
    }
    if (s->valid && s->cb_u == cb_u && (VKA_CS_FAULT(1) || s->pipe_u == pipe_u)) {
        return 0;
    }
    s->valid = 1;
    s->cb_u = cb_u;
    s->pipe_u = pipe_u;
    return 1;
}

enum { VKA_CS_DISP_SETS = 64u, VKA_CS_DISP_WAYS = 4u, VKA_CS_LAY_MAX = 32u };

typedef struct {
    uintptr_t cprog_u;
    uint64_t serial;
    uint32_t layer_gen, valid;
    uintptr_t sh5_u, pipe_u;
    uint8_t lay_bytes[VKA_CS_LAY_MAX];
} vka_cs_disp_ent;

typedef struct {
    vka_cs_disp_ent ways[VKA_CS_DISP_WAYS];
    uint32_t next_way, pad;
} vka_cs_disp_set;

typedef struct {
    vka_cs_disp_set sets[VKA_CS_DISP_SETS];
} vka_cs_disp_memo4;

static inline uint32_t vka_cs_disp_idx(uintptr_t cprog_u) {
    uint64_t x = (uint64_t)cprog_u;
    return (uint32_t)(((x >> 5) ^ (x >> 11) ^ (x >> 17)) & (VKA_CS_DISP_SETS - 1u));
}

static inline int vka_cs_disp_memo4_find(const vka_cs_disp_memo4 *m, uintptr_t cprog_u,
                                         uint64_t serial, uint32_t layer_gen,
                                         uintptr_t *sh5_out, void *lay_out,
                                         size_t lay_sz, uintptr_t *pipe_out) {
    assert(m != NULL && sh5_out != NULL && lay_out != NULL && pipe_out != NULL);
    assert(lay_sz <= VKA_CS_LAY_MAX);
    const vka_cs_disp_set *s = &m->sets[vka_cs_disp_idx(cprog_u)];
    for (uint32_t i = 0; i < VKA_CS_DISP_WAYS; i++) {
        const vka_cs_disp_ent *w = &s->ways[i];
        if (!w->valid) continue;
        if (w->cprog_u != cprog_u || w->layer_gen != layer_gen) continue;
        if (!VKA_CS_FAULT(2) && w->serial != serial) continue;
        *sh5_out = w->sh5_u;
        *pipe_out = w->pipe_u;
        memcpy(lay_out, w->lay_bytes, lay_sz);
        return 1;
    }
    return 0;
}

static inline void vka_cs_disp_memo4_put(vka_cs_disp_memo4 *m, uintptr_t cprog_u,
                                         uint64_t serial, uint32_t layer_gen,
                                         uintptr_t sh5_u, const void *lay_in,
                                         size_t lay_sz, uintptr_t pipe_u) {
    assert(m != NULL && lay_in != NULL && lay_sz <= VKA_CS_LAY_MAX);
    vka_cs_disp_set *s = &m->sets[vka_cs_disp_idx(cprog_u)];
    for (uint32_t i = 0; i < VKA_CS_DISP_WAYS; i++) {
        vka_cs_disp_ent *w = &s->ways[i];
        if (!w->valid || (w->cprog_u == cprog_u && (w->serial != serial || w->layer_gen == layer_gen))) {
            w->cprog_u = cprog_u; w->serial = serial; w->layer_gen = layer_gen;
            w->sh5_u = sh5_u; w->pipe_u = pipe_u; w->valid = 1u;
            memcpy(w->lay_bytes, lay_in, lay_sz);
            return;
        }
    }
    vka_cs_disp_ent *vict = &s->ways[s->next_way & (VKA_CS_DISP_WAYS - 1u)];
    s->next_way = (s->next_way + 1u) & (VKA_CS_DISP_WAYS - 1u);
    vict->cprog_u = cprog_u; vict->serial = serial; vict->layer_gen = layer_gen;
    vict->sh5_u = sh5_u; vict->pipe_u = pipe_u; vict->valid = 1u;
    memcpy(vict->lay_bytes, lay_in, lay_sz);
}

typedef struct {
    uint32_t gen;
    int nk, ready;
    uint64_t keys[16];
} vka_diag_keys;

static inline void vka_diag_keys_parse(vka_diag_keys *dk, uint32_t env_gen, const char *raw) {
    assert(dk != NULL);
    dk->ready = 1;
    dk->gen = env_gen;
    dk->nk = 0;
    for (const char *q = raw; q && *q && dk->nk < 16;) {
        char *e = NULL;
        unsigned long long k = strtoull(q, &e, 16);
        if (e == q) break;
        dk->keys[dk->nk++] = (uint64_t)k;
        q = (*e == ',') ? (e + 1) : e;
    }
}

static inline int vka_diag_keys_match(const vka_diag_keys *dk, uint64_t key) {
    assert(dk != NULL);
    for (int i = 0; i < dk->nk && i < 16; i++) {
        if (dk->keys[i] == key) return 1;
    }
    return 0;
}

typedef struct {
    uint32_t gen;
    int ready, active;
    uint64_t a, b;
} vka_diag_range;

static inline void vka_diag_range_parse(vka_diag_range *dr, uint32_t env_gen, const char *raw) {
    assert(dr != NULL);
    dr->ready = 1;
    dr->gen = env_gen;
    dr->active = 0;
    dr->a = 0;
    dr->b = ~0ull;
    if (raw && *raw) {
        char *e = NULL;
        dr->a = (uint64_t)strtoull(raw, &e, 10);
        if (e != raw) {
            dr->b = (*e == ',') ? (uint64_t)strtoull(e + 1, NULL, 10) : ~0ull;
            dr->active = 1;
        }
    }
}

static inline int vka_diag_range_hit(const vka_diag_range *dr, uint64_t di) {
    assert(dr != NULL);
    return dr->active && di >= dr->a && di <= dr->b;
}

static inline int vka_diag_need_reload(int ready, uint32_t cached_gen, uint32_t env_gen) {
    if (!ready) return 1;
    return !VKA_CS_FAULT(3) && cached_gen != env_gen;
}

#endif
