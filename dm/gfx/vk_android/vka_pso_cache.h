// Lock-free replay-thread front-caches for graphics (512x4 dynamic-VIN + 64x4 full-key) and compute (128x4) PSOs (DM_VK_PSO_MEMO=1).
#pragma once
#include <stdint.h>
#include <string.h>

enum {
    VKA_PSO_FAST_KEY_MAX = 320,
    VKA_PSO_FULL_KEY_MAX = 1152,
    VKA_PSO_FAST_SETS = 512,
    VKA_PSO_FULL_SETS = 64,
    VKA_PSO_CS_SETS = 128,
    VKA_PSO_WAYS = 4
};

typedef struct {
    uint64_t h;
    uint32_t len;
    void *g;
    uint8_t key[VKA_PSO_FAST_KEY_MAX];
} vka_pso_fast_ent;

typedef struct {
    vka_pso_fast_ent ways[VKA_PSO_WAYS];
    uint8_t next;
} vka_pso_fast_set;

typedef struct {
    vka_pso_fast_set sets[VKA_PSO_FAST_SETS];
} vka_pso_fast_cache;

typedef struct {
    uint64_t h;
    uint32_t len;
    void *g;
    uint8_t key[VKA_PSO_FULL_KEY_MAX];
} vka_pso_full_ent;

typedef struct {
    vka_pso_full_ent ways[VKA_PSO_WAYS];
    uint8_t next;
} vka_pso_full_set;

typedef struct {
    vka_pso_full_set sets[VKA_PSO_FULL_SETS];
} vka_pso_full_cache;

typedef struct {
    const void *cs;
    uint64_t layout;
    uint64_t umask;
    uint64_t smask;
    void *g;
} vka_pso_cs_ent;

typedef struct {
    vka_pso_cs_ent ways[VKA_PSO_WAYS];
    uint8_t next;
} vka_pso_cs_set;

typedef struct {
    vka_pso_cs_set sets[VKA_PSO_CS_SETS];
} vka_pso_cs_cache;

static inline uint64_t vka_pso_splitmix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

static inline uint32_t vka_pso_cs_set_idx(const void *cs, uint64_t layout, uint64_t umask, uint64_t smask) {
    uint64_t h = (uint64_t)(uintptr_t)cs * 11400714819323198485ull + layout * 0x9e3779b97f4a7c15ull;
    h ^= umask * 0xbf58476d1ce4e5b9ull + smask;
    return (uint32_t)((vka_pso_splitmix64(h) >> 57) & (VKA_PSO_CS_SETS - 1));
}

static inline void *vka_pso_gfx_get(const vka_pso_fast_cache *fast, const vka_pso_full_cache *full,
                                    const void *fk, uint32_t len, uint64_t h) {
    if (!fk || !len || len > VKA_PSO_FULL_KEY_MAX) return NULL;
    uint64_t mix = vka_pso_splitmix64(h);
    if (len <= VKA_PSO_FAST_KEY_MAX && fast) {
        const vka_pso_fast_ent *ways = fast->sets[(mix >> 55) & (VKA_PSO_FAST_SETS - 1)].ways;
        for (int w = 0; w < VKA_PSO_WAYS; w++) {
            const vka_pso_fast_ent *e = &ways[w];
            if (e->g && e->h == h && e->len == len && !memcmp(e->key, fk, len)) return e->g;
        }
        return NULL;
    }
    if (!full) return NULL;
    const vka_pso_full_ent *ways = full->sets[(mix >> 58) & (VKA_PSO_FULL_SETS - 1)].ways;
    for (int w = 0; w < VKA_PSO_WAYS; w++) {
        const vka_pso_full_ent *e = &ways[w];
        if (e->g && e->h == h && e->len == len && !memcmp(e->key, fk, len)) return e->g;
    }
    return NULL;
}

static inline void vka_pso_gfx_put(vka_pso_fast_cache *fast, vka_pso_full_cache *full,
                                   const void *fk, uint32_t len, uint64_t h, void *g) {
    if (!fk || !g || !len || len > VKA_PSO_FULL_KEY_MAX) return;
    uint64_t mix = vka_pso_splitmix64(h);
    if (len <= VKA_PSO_FAST_KEY_MAX && fast) {
        vka_pso_fast_set *s = &fast->sets[(mix >> 55) & (VKA_PSO_FAST_SETS - 1)];
        int slot = -1;
        for (int w = 0; w < VKA_PSO_WAYS; w++) {
            vka_pso_fast_ent *e = &s->ways[w];
            if (!e->g || (e->h == h && e->len == len && !memcmp(e->key, fk, len))) { slot = w; break; }
        }
        if (slot < 0) { slot = s->next & (VKA_PSO_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
        s->ways[slot].h = h;
        s->ways[slot].len = len;
        s->ways[slot].g = g;
        memcpy(s->ways[slot].key, fk, len);
        return;
    }
    if (!full) return;
    vka_pso_full_set *s = &full->sets[(mix >> 58) & (VKA_PSO_FULL_SETS - 1)];
    int slot = -1;
    for (int w = 0; w < VKA_PSO_WAYS; w++) {
        vka_pso_full_ent *e = &s->ways[w];
        if (!e->g || (e->h == h && e->len == len && !memcmp(e->key, fk, len))) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VKA_PSO_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot].h = h;
    s->ways[slot].len = len;
    s->ways[slot].g = g;
    memcpy(s->ways[slot].key, fk, len);
}

static inline void *vka_pso_cs_get(const vka_pso_cs_cache *c, const void *cs,
                                   uint64_t layout, uint64_t umask, uint64_t smask) {
    if (!c || !cs || !layout) return NULL;
    const vka_pso_cs_ent *ways = c->sets[vka_pso_cs_set_idx(cs, layout, umask, smask)].ways;
    for (int w = 0; w < VKA_PSO_WAYS; w++) {
        const vka_pso_cs_ent *e = &ways[w];
        if (e->g && e->cs == cs && e->layout == layout && e->umask == umask && e->smask == smask) return e->g;
    }
    return NULL;
}

static inline void vka_pso_cs_put(vka_pso_cs_cache *c, const void *cs,
                                  uint64_t layout, uint64_t umask, uint64_t smask, void *g) {
    if (!c || !cs || !layout || !g) return;
    vka_pso_cs_set *s = &c->sets[vka_pso_cs_set_idx(cs, layout, umask, smask)];
    int slot = -1;
    for (int w = 0; w < VKA_PSO_WAYS; w++) {
        vka_pso_cs_ent *e = &s->ways[w];
        if (!e->g || (e->cs == cs && e->layout == layout && e->umask == umask && e->smask == smask)) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VKA_PSO_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot] = (vka_pso_cs_ent){cs, layout, umask, smask, g};
}
