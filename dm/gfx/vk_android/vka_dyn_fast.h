#ifndef VKA_DYN_FAST_H
#define VKA_DYN_FAST_H
/* Wave 12 (Opt 14): Dynamic-state guard & zero-alloc push dedup (DM_VK_DYN_MEMO=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W12_TEST_HOOKS
extern int g_w12_fault;
#define VKA_DYN_FAULT(n) (g_w12_fault == (n))
#else
#define VKA_DYN_FAULT(n) 0
#endif

static inline int vka_dyn_memo_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_DYN_MEMO");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

typedef struct {
    uintptr_t d_ptr;
    uint64_t s_vfmt;
    uint32_t bind_gen;
    uint32_t full_w, full_h;
    int32_t area_x, area_y;
    uint32_t area_w, area_h;
    uint32_t df, sf;
    int32_t sc_x, sc_y, sc_w, sc_h;
    uint32_t topo;
    uint32_t flags;
    int32_t xp_scissor;
} vka_dyn_key;

typedef struct {
    int valid;
    vka_dyn_key k;
} vka_dyn_guard;

static inline uint32_t vka_dyn_pack_flags(int has_sc, int mesh, int eq_relax,
                                          int restart, int restart_itype,
                                          int origin_ll, int q_origin_ll) {
    uint32_t f = (uint32_t)(has_sc != 0);
    f |= (uint32_t)(mesh != 0) << 1;
    f |= (uint32_t)(eq_relax != 0) << 2;
    f |= (uint32_t)(restart != 0) << 3;
    f |= ((uint32_t)restart_itype & 0x3u) << 4;
    f |= (uint32_t)(origin_ll != 0) << 6;
    f |= (uint32_t)(q_origin_ll != 0) << 7;
    return f;
}

static inline void vka_dyn_key_fill(vka_dyn_key *out, const void *d_ptr,
                                    uint64_t s_vfmt, uint32_t bind_gen,
                                    uint32_t full_w, uint32_t full_h,
                                    int32_t area_x, int32_t area_y,
                                    uint32_t area_w, uint32_t area_h,
                                    uint32_t df, uint32_t sf,
                                    int32_t sc_x, int32_t sc_y,
                                    int32_t sc_w, int32_t sc_h,
                                    uint32_t topo, uint32_t flags,
                                    int32_t xp_scissor) {
    assert(out != NULL);
    memset(out, 0, sizeof *out);
    out->d_ptr = (uintptr_t)d_ptr;
    out->s_vfmt = s_vfmt;
    out->bind_gen = bind_gen;
    out->full_w = full_w;
    out->full_h = full_h;
    out->area_x = area_x;
    out->area_y = area_y;
    out->area_w = area_w;
    out->area_h = area_h;
    out->df = df;
    out->sf = sf;
    out->sc_x = sc_x;
    out->sc_y = sc_y;
    out->sc_w = sc_w;
    out->sc_h = sc_h;
    out->topo = topo;
    out->flags = flags;
    out->xp_scissor = xp_scissor;
}

static inline void vka_dyn_guard_reset(vka_dyn_guard *g) {
    assert(g != NULL);
    g->valid = 0;
}

static inline int vka_dyn_guard_hit(const vka_dyn_guard *g, const vka_dyn_key *k) {
    assert(g != NULL && k != NULL);
    if (!g->valid) return 0;
    const vka_dyn_key *a = &g->k;
    if (a->d_ptr != k->d_ptr || a->s_vfmt != k->s_vfmt) return 0;
    if (!VKA_DYN_FAULT(2) && a->bind_gen != k->bind_gen) return 0;
    if (a->full_w != k->full_w || a->full_h != k->full_h) return 0;
    if (a->area_x != k->area_x || a->area_y != k->area_y ||
        a->area_w != k->area_w || a->area_h != k->area_h) return 0;
    if (a->df != k->df || a->sf != k->sf || a->topo != k->topo ||
        a->flags != k->flags || a->xp_scissor != k->xp_scissor) return 0;
    if (a->sc_x != k->sc_x || a->sc_y != k->sc_y || a->sc_h != k->sc_h) return 0;
    if (!VKA_DYN_FAULT(1) && a->sc_w != k->sc_w) return 0;
    return 1;
}

static inline void vka_dyn_guard_put(vka_dyn_guard *g, const vka_dyn_key *k) {
    assert(g != NULL && k != NULL);
    g->k = *k;
    g->valid = 1;
}

static inline int vka_pd_grow(uint64_t **pw, size_t *pcap, size_t need) {
    assert(pw != NULL && pcap != NULL);
    if (need <= *pcap) return 1;
    size_t ncap = need * 2u;
    if (ncap < need) return 0;
    uint64_t *np = (uint64_t *)realloc(*pw, ncap * sizeof *np);
    if (!np) return 0;
    *pw = np;
    *pcap = ncap;
    return 1;
}

static inline int vka_pd_eq4(const uint64_t dst[4], const uint64_t src[4]) {
    assert(dst != NULL && src != NULL);
    if (dst[0] != src[0] || dst[1] != src[1] || dst[3] != src[3]) return 0;
    if (!VKA_DYN_FAULT(3) && dst[2] != src[2]) return 0;
    return 1;
}

#endif
