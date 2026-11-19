// Shared-page edge hashing and selective unpinning for tracked GPU textures (vka_tex.c).
// When a sub-allocated texture shares boundary page(s) with hot CPU neighbor writes, re-protecting those
// shared edge pages triggers a page-fault + mprotect storm and forces a full live-image scan on every submit.
// After VKA_EDGE_UNPIN_AFTER consecutive neighbor-only skips, leave shared edge pages writable, protect only
// exclusive full interior pages [ia, ie), and poll the small edge byte ranges [r0], [r1] directly by hash.
#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { VKA_EDGE_UNPIN_AFTER = 2, VKA_EDGE_INTERIOR_HASH_MAX = 1u << 20 };

typedef struct {
    uintptr_t r[2][2];
    uintptr_t ia;
    uintptr_t ie;
    size_t pg;
    int has_edges;
} vka_edge_geom;

static inline void vka_edge_split(uintptr_t a, size_t size, size_t pg, vka_edge_geom *g) {
    assert(g != NULL);
    assert(pg != 0 && (pg & (pg - 1u)) == 0);
    assert(size > 0);
    uintptr_t e = a + size;
    assert(e >= a);
    uintptr_t ia = (a + pg - 1u) & ~(pg - 1u);
    uintptr_t ie = e & ~(pg - 1u);
    g->pg = pg;
    if (ie <= ia) {
        g->r[0][0] = a; g->r[0][1] = e;
        g->r[1][0] = 0; g->r[1][1] = 0;
        g->ia = 0; g->ie = 0;
    } else {
        g->r[0][0] = a;  g->r[0][1] = ia;
        g->r[1][0] = ie; g->r[1][1] = e;
        g->ia = ia; g->ie = ie;
    }
    g->has_edges = (g->r[0][1] > g->r[0][0]) || (g->r[1][1] > g->r[1][0]);
}

static inline uint64_t vka_edge_hash(uintptr_t lo, uintptr_t hi) {
    assert(hi >= lo);
    const uint8_t *p = (const uint8_t *)lo;
    size_t n = hi - lo, i = 0;
    uint64_t h = 0x9e3779b97f4a7c15ull ^ n, w = 0;
    for (; i + 8 <= n; i += 8) {
        memcpy(&w, p + i, 8);
        h ^= w * 0x87c37b91114253d5ull;
        h = ((h << 31) | (h >> 33)) * 0x4cf5ad432745937full;
    }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h ^ (h >> 29);
}

static inline void vka_edge_snapshot(const vka_edge_geom *g, uint64_t out[2]) {
    assert(g != NULL);
    assert(out != NULL);
    out[0] = g->r[0][1] > g->r[0][0] ? vka_edge_hash(g->r[0][0], g->r[0][1]) : 0;
    out[1] = g->r[1][1] > g->r[1][0] ? vka_edge_hash(g->r[1][0], g->r[1][1]) : 0;
}

static inline int vka_edge_changed(const vka_edge_geom *g, const uint64_t saved[2], int *h0_bad, int *h1_bad) {
    assert(g != NULL);
    assert(saved != NULL);
    assert(h0_bad != NULL && h1_bad != NULL);
    *h0_bad = (g->r[0][1] > g->r[0][0]) && (vka_edge_hash(g->r[0][0], g->r[0][1]) != saved[0]);
    *h1_bad = (g->r[1][1] > g->r[1][0]) && (vka_edge_hash(g->r[1][0], g->r[1][1]) != saved[1]);
    return *h0_bad || *h1_bad;
}

static inline void vka_edge_expand(uintptr_t s0, uintptr_t s1, uintptr_t *alo, uintptr_t *ahi) {
    assert(alo != NULL && ahi != NULL);
    if (s1 <= s0) return;
    if (s0 < *alo) *alo = s0;
    if (s1 > *ahi) *ahi = s1;
}

static inline void vka_edge_dirty_bounds(const vka_edge_geom *g, uintptr_t base, size_t size,
                                         int h0_bad, int h1_bad, int in_dirty, size_t in_lo, size_t in_hi,
                                         size_t *lo, size_t *hi) {
    assert(g != NULL);
    assert(lo != NULL && hi != NULL);
    assert(size > 0);
    uintptr_t alo = base + size, ahi = base;
    if (h0_bad) vka_edge_expand(g->r[0][0], g->r[0][1], &alo, &ahi);
    if (in_dirty && g->ie > g->ia) {
        size_t span = (size_t)(g->ie - g->ia);
        size_t cl_lo = in_lo < span ? in_lo : 0;
        size_t cl_hi = (in_hi <= span && in_hi > cl_lo) ? in_hi : span;
        vka_edge_expand(g->ia + cl_lo, g->ia + cl_hi, &alo, &ahi);
    }
    if (h1_bad) vka_edge_expand(g->r[1][0], g->r[1][1], &alo, &ahi);
    if (ahi <= alo || alo < base || ahi > base + size) { *lo = 0; *hi = size; return; }
    *lo = (size_t)(alo - base);
    *hi = (size_t)(ahi - base);
}

static inline int vka_edge_can_hash_interior(const vka_edge_geom *g, int edge_unpinned) {
    assert(g != NULL);
    return !edge_unpinned && g->has_edges && g->ie > g->ia && (g->ie - g->ia) <= VKA_EDGE_INTERIOR_HASH_MAX;
}

static inline int vka_edge_interior_collateral(const vka_edge_geom *g, uint64_t saved_in_h,
                                               int in_hashed, int edge_pg_dirty) {
    assert(g != NULL);
    if (!in_hashed || !edge_pg_dirty || g->ie <= g->ia) return 0;
    return vka_edge_hash(g->ia, g->ie) == saved_in_h;
}
