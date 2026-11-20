// Open-addressing hash table from fixed-length byte keys -> pointers (linear probing + backward shift on deletion, no tombstones).
// The replay hot path uses it instead of NSDictionary: keys are not boxed, no allocation, a lookup does only one hash + memcmp.
// No locking: callers protect it with their own locks. Ownership of values belongs to the caller (put / del return the replaced / deleted old value). Unit test tests/t_hmap.c.
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *keys; void **vals; uint32_t klen, cap, n; } nvn_hmap;   // vals[i] == NULL means an empty slot

static inline uint64_t nvn_hm_fin(uint64_t h) {
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

static inline uint64_t nvn_hm_hash(const void *k, uint32_t len) {
    assert(k != NULL || len == 0);
    const uint8_t *p = k; uint64_t h = 0x9e3779b97f4a7c15ull ^ len;
    if (len == 8) {
        uint64_t w; memcpy(&w, p, 8);
        h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32;
        return nvn_hm_fin(h);
    }
    if (len == 16) {
        uint64_t w0, w1; memcpy(&w0, p, 8); memcpy(&w1, p + 8, 8);
        h = (h ^ w0) * 0xff51afd7ed558ccdull; h ^= h >> 32;
        h = (h ^ w1) * 0xff51afd7ed558ccdull; h ^= h >> 32;
        return nvn_hm_fin(h);
    }
    for (; len >= 8; p += 8, len -= 8) { uint64_t w; memcpy(&w, p, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; }
    if (len) { uint64_t w = 0; memcpy(&w, p, len); h = (h ^ w) * 0xff51afd7ed558ccdull; }
    return nvn_hm_fin(h);
}

static inline int nvn_hm_eq16(const void *a, uint64_t k0, uint64_t k1) {
    uint64_t a0, a1; memcpy(&a0, a, 8); memcpy(&a1, (const uint8_t *)a + 8, 8);
    return a0 == k0 && a1 == k1;
}

static inline void nvn_hm_init(nvn_hmap *m, uint32_t klen) {
    assert(m != NULL && klen > 0);
    memset(m, 0, sizeof *m); m->klen = klen;
}

static inline void *nvn_hm_get16(const nvn_hmap *m, const void *k) {
    uint64_t k0, k1; memcpy(&k0, k, 8); memcpy(&k1, (const uint8_t *)k + 8, 8);
    uint32_t mask = m->cap - 1u, i = (uint32_t)nvn_hm_hash(k, 16) & mask;
    for (uint32_t step = 0; step < m->cap; step++, i = (i + 1u) & mask) {
        if (!m->vals[i]) return NULL;
        if (nvn_hm_eq16(m->keys + (size_t)i * 16u, k0, k1)) return m->vals[i];
    }
    return NULL;
}

static inline void *nvn_hm_get(const nvn_hmap *m, const void *k) {
    assert(m != NULL && k != NULL);
    if (!m->n) return NULL;
    uint32_t mask = m->cap - 1u;
    if (m->klen == 8) {
        uint64_t kk; memcpy(&kk, k, 8);
        uint32_t i = (uint32_t)nvn_hm_hash(k, 8) & mask;
        for (uint32_t step = 0; step < m->cap; step++, i = (i + 1u) & mask) {
            if (!m->vals[i]) return NULL;
            uint64_t ki; memcpy(&ki, m->keys + (size_t)i * 8u, 8);
            if (ki == kk) return m->vals[i];
        }
        return NULL;
    }
    if (m->klen == 16) return nvn_hm_get16(m, k);
    uint32_t i = (uint32_t)nvn_hm_hash(k, m->klen) & mask;
    for (uint32_t step = 0; step < m->cap; step++, i = (i + 1u) & mask) {
        if (!m->vals[i]) return NULL;
        if (!memcmp(m->keys + (size_t)i * m->klen, k, m->klen)) return m->vals[i];
    }
    return NULL;
}

static inline void *nvn_hm_put(nvn_hmap *m, const void *k, void *v);
static inline void nvn_hm_grow(nvn_hmap *m) {
    assert(m != NULL && m->klen > 0);
    nvn_hmap o = *m;
    m->cap = o.cap ? o.cap * 2u : 64u; m->n = 0;
    m->keys = malloc((size_t)m->cap * m->klen); m->vals = calloc(m->cap, sizeof *m->vals);
    if (!m->keys || !m->vals) abort();
    for (uint32_t i = 0; i < o.cap; i++) if (o.vals[i]) nvn_hm_put(m, o.keys + (size_t)i * o.klen, o.vals[i]);
    free(o.keys); free(o.vals);
}

// v must not be NULL; returns the old value for the same key (NULL if none)
static inline void *nvn_hm_put(nvn_hmap *m, const void *k, void *v) {
    assert(m != NULL && k != NULL && v != NULL);
    if ((m->n + 1u) * 4u > m->cap * 3u) nvn_hm_grow(m);   // Load factor <= 3/4
    uint32_t mask = m->cap - 1u, i = (uint32_t)nvn_hm_hash(k, m->klen) & mask;
    for (uint32_t step = 0; step < m->cap; step++, i = (i + 1u) & mask) {
        uint8_t *ki = m->keys + (size_t)i * m->klen;
        if (!m->vals[i]) { memcpy(ki, k, m->klen); m->vals[i] = v; m->n++; return NULL; }
        int eq = m->klen == 8 ? !memcmp(ki, k, 8) :
                 m->klen == 16 ? !memcmp(ki, k, 16) : !memcmp(ki, k, m->klen);
        if (eq) { void *old = m->vals[i]; m->vals[i] = v; return old; }
    }
    abort();
}

// After deletion, shift back later entries on the same probe chain to fill the hole, so that a lookup can stop at the first empty slot
static inline void *nvn_hm_del(nvn_hmap *m, const void *k) {
    assert(m != NULL && k != NULL);
    if (!m->n) return NULL;
    uint32_t mask = m->cap - 1u, i = (uint32_t)nvn_hm_hash(k, m->klen) & mask, found = UINT32_MAX;
    for (uint32_t step = 0; step < m->cap; step++, i = (i + 1u) & mask) {
        if (!m->vals[i]) return NULL;
        int eq = m->klen == 8 ? !memcmp(m->keys + (size_t)i * 8u, k, 8) :
                 m->klen == 16 ? !memcmp(m->keys + (size_t)i * 16u, k, 16) : !memcmp(m->keys + (size_t)i * m->klen, k, m->klen);
        if (eq) { found = i; break; }
    }
    if (found == UINT32_MAX) return NULL;
    i = found; void *old = m->vals[i];
    for (uint32_t outer = 0, j = i; outer < m->cap; outer++) {
        m->vals[i] = NULL;
        for (uint32_t inner = 0; inner < m->cap; inner++) {
            j = (j + 1u) & mask;
            if (!m->vals[j]) { m->n--; return old; }
            uint32_t h = (uint32_t)nvn_hm_hash(m->keys + (size_t)j * m->klen, m->klen) & mask;
            if (i <= j ? (i < h && h <= j) : (i < h || h <= j)) continue;
            break;
        }
        memcpy(m->keys + (size_t)i * m->klen, m->keys + (size_t)j * m->klen, m->klen);
        m->vals[i] = m->vals[j]; i = j;
    }
    return old;
}

// Clear; when rel is non-null, call it once for each value (releasing ownership)
static inline void nvn_hm_clear(nvn_hmap *m, void (*rel)(void *)) {
    assert(m != NULL);
    for (uint32_t i = 0; i < m->cap; i++) if (m->vals[i]) { if (rel) rel(m->vals[i]); m->vals[i] = NULL; }
    m->n = 0;
}
