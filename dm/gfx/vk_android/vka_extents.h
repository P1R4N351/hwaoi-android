// The free space of one image memory block (vka_imgmem.c) as a sorted list of free extents, in units (256 bytes there): an allocation takes exactly the units it needs at the
// first extent where an aligned start fits (first fit), and a free merges with its neighbours. No Vulkan, no locking (the caller's), so the host tests run it on the Mac
// (dm/host/tests/t_android_imgmem.c). An earlier version rounded every image up to a power-of-two run (a buddy allocator): about a third more memory for mipped textures
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vka_imgmem_fast.h"

typedef struct { uint32_t a, n; } vke_ext;                    // units [a, a + n)
_Static_assert(sizeof(vke_ext) == 8 && offsetof(vke_ext, a) == 0 && offsetof(vke_ext, n) == 4, "vke_ext layout");
typedef struct {
    vke_ext *f;
    uint32_t nf, cap, units, used, max_n;
    uint8_t max_valid, fast_on;
} vke_list;   // free extents by start; used: allocated units

static inline int vke_init(vke_list *l, uint32_t units) {
    memset(l, 0, sizeof *l);
    if (!(l->f = calloc(16, sizeof *l->f))) return 0;
    l->cap = 16; l->units = units; l->f[0] = (vke_ext){0, units}; l->nf = 1;
    l->max_n = units; l->max_valid = 1; l->fast_on = (uint8_t)vka_imgmem_fast_on();
    return 1;
}
static inline void vke_fini(vke_list *l) { free(l->f); memset(l, 0, sizeof *l); }
static inline int vke_empty(const vke_list *l) { return l->used == 0; }
static inline int vke_room(vke_list *l) {   // room for one more extent
    if (!l) return 0;
    if (l->nf < l->cap) return 1;
    uint32_t nc = l->cap ? 2u * l->cap : 16u;
    vke_ext *g = realloc(l->f, (size_t)nc * sizeof *g); if (!g) return 0;
    l->f = g; l->cap = nc; return 1;
}
static inline int64_t vke_alloc_carve(vke_list *l, uint32_t i, uint64_t s, uint32_t n) {
    vke_ext e = l->f[i];
    uint32_t front = (uint32_t)(s - e.a), back = (uint32_t)((uint64_t)e.a + e.n - s - n);
    if (front && back) {   // split in two: one more extent
        if (!vke_room(l)) return -1;
        if (l->nf - i - 1u > 0u) memmove(&l->f[i + 2], &l->f[i + 1], (l->nf - i - 1u) * sizeof *l->f);
        l->nf++;
        l->f[i] = (vke_ext){e.a, front}; l->f[i + 1] = (vke_ext){(uint32_t)(s + n), back};
    } else if (front) l->f[i].n = front;
    else if (back) l->f[i] = (vke_ext){(uint32_t)(s + n), back};
    else {
        if (l->nf - i - 1u > 0u) memmove(&l->f[i], &l->f[i + 1], (l->nf - i - 1u) * sizeof *l->f);
        l->nf--;
    }
    l->used += n;
    if (l->nf == 0u) { l->max_n = 0u; l->max_valid = 1u; }
    else if (l->nf == 1u) { l->max_n = l->f[0].n; l->max_valid = 1u; }
    else if (l->max_valid && e.n == l->max_n) {
        uint32_t rem = front > back ? front : back;
        if (rem < l->max_n) l->max_valid = 0u;
    }
    return (int64_t)s;
}
// n units at a multiple of align (a power of two): the first unit, or -1
static inline int64_t vke_alloc(vke_list *l, uint32_t n, uint32_t align) {
    if (!l || !l->f || !n || !align) return -1;
    if (vka_imgmem_fast_on()) {
        if (n > l->units - l->used || (l->max_valid && n > l->max_n)) return -1;
        uint64_t s = 0u;
        uint32_t mx = 0u;
        uint32_t hit = vke_scan_fit4((const uint8_t *)l->f, l->nf, n, align,
                                     l->max_valid, l->max_valid ? NULL : &mx, &s, vka_w32_fault_mode());
        if (hit == UINT32_MAX) {
            if (!l->max_valid) { l->max_n = mx; l->max_valid = 1u; }
            return -1;
        }
        return vke_alloc_carve(l, hit, s, n);
    }
    l->max_valid = 0u;
    for (uint32_t i = 0; i < l->nf; i++) {
        vke_ext e = l->f[i];
        uint64_t s = ((uint64_t)e.a + align - 1) & ~(uint64_t)(align - 1);
        if (s + n > (uint64_t)e.a + e.n) continue;
        return vke_alloc_carve(l, i, s, n);
    }
    return -1;
}
// The n units at a back (they must be one vke_alloc gave): 0; -2 when the range overlaps a free extent (a double free, or a range never given: nothing changes);
// -1 when the extent list could not grow (the units stay lost to reuse; vka_imgmem.c still counts them free, so the block can empty and be given back). used changes only on success
static inline int vke_free(vke_list *l, uint32_t a, uint32_t n) {
    if (!l || !l->f || !n) return -2;
    uint32_t lo = 0, hi = l->nf;   // the first extent starting after a
    for (uint32_t step = 0u; step < 32u && lo < hi; step++) {
        uint32_t m = (lo + hi) / 2u; if (l->f[m].a < a) lo = m + 1u; else hi = m;
    }
    if ((lo > 0 && l->f[lo - 1].a + l->f[lo - 1].n > a) || (lo < l->nf && a + n > l->f[lo].a) || n > l->used) return -2;   // checked in release builds too (no assert)
    int left = lo > 0 && l->f[lo - 1].a + l->f[lo - 1].n == a, right = lo < l->nf && a + n == l->f[lo].a;
    uint32_t merged_n = n;
    if (left && right) {
        merged_n = l->f[lo - 1].n + n + l->f[lo].n;
        l->f[lo - 1].n = merged_n;
        if (l->nf - lo - 1u > 0u) memmove(&l->f[lo], &l->f[lo + 1], (l->nf - lo - 1u) * sizeof *l->f);
        l->nf--;
    } else if (left) {
        merged_n = l->f[lo - 1].n + n;
        l->f[lo - 1].n = merged_n;
    } else if (right) {
        merged_n = l->f[lo].n + n;
        l->f[lo].a = a; l->f[lo].n = merged_n;
    } else {
        if (!vke_room(l)) return -1;
        if (l->nf - lo > 0u) memmove(&l->f[lo + 1], &l->f[lo], (l->nf - lo) * sizeof *l->f);
        l->nf++; l->f[lo] = (vke_ext){a, n};
    }
    l->used -= n;
    if (l->nf == 1u) { l->max_n = merged_n; l->max_valid = 1u; }
    else if (l->max_valid && merged_n > l->max_n) l->max_n = merged_n;
    return 0;
}
