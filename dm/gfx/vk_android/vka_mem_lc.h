#ifndef VKA_MEM_LC_H
#define VKA_MEM_LC_H
/* 1-entry whole-segment MRU + 256x4 set-associative segment lookup front-cache.
   Read-only on hit (zero dirty L1 lines); 1 MiB set indexing with 4 ways absorbs
   64 MiB stride aliasing AND covers 16x more bytes per cold miss than 64 KiB blocks,
   while lazy per-set gen eliminates 40+ KiB memset on g_sg_gen bumps. */
#include <stdint.h>
#include <string.h>

enum { VKA_LC_SETS = 256, VKA_LC_WAYS = 4 };

#ifndef VKA_MEM_SEG_DEFINED
typedef struct {
    uintptr_t lo, hi;
    uint64_t payload[2];
    int refs, fd_alias;
} seg_t;
#endif

typedef struct {
    uint64_t mru_gen;
    seg_t mru;
    uint64_t set_gen[VKA_LC_SETS];
    uint8_t hand[VKA_LC_SETS];
    seg_t ways[VKA_LC_SETS][VKA_LC_WAYS];
} vka_lc4;

static inline unsigned vka_lc4_set(uintptr_t a) {
    uint64_t x = (uint64_t)(a >> 20) ^ (uint64_t)(a >> 28) ^ (uint64_t)(a >> 36);
    x *= UINT64_C(0x9e3779b97f4a7c15);
    return (unsigned)(x >> 56);
}

static inline const seg_t *vka_lc4_get(const vka_lc4 *c, uint64_t gen, uintptr_t a) {
    if (!c || !gen) return NULL;
    if (c->mru_gen == gen && a >= c->mru.lo && a < c->mru.hi) return &c->mru;
    unsigned s = vka_lc4_set(a);
    if (c->set_gen[s] != gen) return NULL;
    const seg_t *w = c->ways[s];
    if (a >= w[0].lo && a < w[0].hi) return &w[0];
    if (a >= w[1].lo && a < w[1].hi) return &w[1];
    if (a >= w[2].lo && a < w[2].hi) return &w[2];
    if (a >= w[3].lo && a < w[3].hi) return &w[3];
    return NULL;
}

static inline void vka_lc4_put(vka_lc4 *c, uint64_t gen, uintptr_t a, const seg_t *seg) {
    if (!c || !gen || !seg || seg->hi <= seg->lo) return;
    c->mru = *seg;
    c->mru_gen = gen;
    unsigned s = vka_lc4_set(a);
    if (c->set_gen[s] != gen) {
        memset(c->ways[s], 0, sizeof c->ways[s]);
        c->set_gen[s] = gen;
        c->hand[s] = 0;
    }
    for (unsigned w = 0; w < VKA_LC_WAYS; w++) {
        if (c->ways[s][w].lo == seg->lo || c->ways[s][w].hi <= c->ways[s][w].lo) {
            c->ways[s][w] = *seg;
            return;
        }
    }
    unsigned victim = (unsigned)(c->hand[s]++ & (VKA_LC_WAYS - 1));
    c->ways[s][victim] = *seg;
}

#endif
