// Per-submission set-associative driver/support block memos & VkDescriptorSet reuse for vka_draw.c (DM_VK_DSET_FAST=1).
// Key-stamped 64x4 memo bypasses vka_drv_fill on alternating materials; content ring handles identical bytes across stamp changes.
#ifndef VKA_DSET_FAST_H
#define VKA_DSET_FAST_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#ifdef W8_TEST_HOOKS
extern int g_w8_fault;
#define VKA_DSET_FAST_FAULT(n) (g_w8_fault == (n))
#else
#define VKA_DSET_FAST_FAULT(n) 0
#endif

enum { VKA_DRV_SETS = 64, VKA_AREF_SETS = 16, VKA_DSET_SETS = 512, VKA_DSET_WAYS = 4 };

typedef struct {
    uint64_t k[4], buf, off;
    uint32_t sub_gen;
    uint16_t tag, sz;
} vka_drv_key_ent;

typedef struct {
    vka_drv_key_ent e[VKA_DSET_WAYS];
    uint32_t rr, pad;
} vka_drv_key_set;

typedef struct {
    vka_drv_key_set s[VKA_DRV_SETS];
} vka_drv_key4;

typedef struct {
    uint64_t h0, h1, buf, off;
    uint8_t *ptr;
    uint32_t sub_gen;
    uint16_t tag, sz;
} vka_drv_ring_ent;

typedef struct {
    vka_drv_ring_ent e[VKA_DSET_WAYS];
    uint32_t rr, pad;
} vka_drv_ring_set;

typedef struct {
    vka_drv_ring_set s[VKA_DRV_SETS];
} vka_drv_ring4;

typedef struct {
    uint64_t buf, off;
    uint8_t *ptr;
    uint32_t sub_gen, ref_bits;
} vka_aref_ent;

typedef struct {
    vka_aref_ent e[VKA_DSET_WAYS];
    uint32_t rr, pad;
} vka_aref_set;

typedef struct {
    vka_aref_set s[VKA_AREF_SETS];
} vka_aref_memo4;

typedef struct {
    uint64_t set0, h0, h1, set;
    uint32_t sub_gen, n;
} vka_dset_ent;

typedef struct {
    vka_dset_ent e[VKA_DSET_WAYS];
    uint32_t rr, pad;
} vka_dset_set;

typedef struct {
    vka_dset_set s[VKA_DSET_SETS];
} vka_dset_memo4;

static inline void vka_hash_mix64(uint64_t h[2], const uint64_t *w, size_t nw) {
    assert(h != NULL && (nw == 0 || w != NULL));
    uint64_t a = h[0], c = h[1];
    for (size_t i = 0; i < nw; i++) {
        uint64_t v = w[i];
        a = (a ^ v) * 0x9e3779b97f4a7c15ull;
        a ^= a >> 32;
        c = (c + v) * 0xc2b2ae3d27d4eb4full;
        c = (c << 31) | (c >> 33);
    }
    h[0] = a;
    h[1] = c;
}

static inline void vka_hash_dual64(const void *p, size_t n, uint64_t h[2]) {
    assert(h != NULL && (n == 0 || p != NULL));
    const uint8_t *b = (const uint8_t *)p;
    size_t i = 0;
    uint64_t a0 = 0xcbf29ce484222325ull, a1 = 0x9e3779b97f4a7c15ull;
    uint64_t c0 = 0x6a09e667f3bcc909ull, c1 = 0xbb67ae8584caa73bull;
    for (; i + 16 <= n; i += 16) {
        uint64_t w0, w1;
        memcpy(&w0, b + i, 8);
        memcpy(&w1, b + i + 8, 8);
        a0 = (a0 ^ w0) * 0x9e3779b97f4a7c15ull;
        a1 = (a1 ^ w1) * 0xbf58476d1ce4e5b9ull;
        c0 = (c0 + w0) * 0xc2b2ae3d27d4eb4full;
        c1 = (c1 + w1) * 0x94d049bb133111ebull;
    }
    uint64_t a = a0 ^ (a1 << 17 | a1 >> 47), c = c0 ^ (c1 << 31 | c1 >> 33);
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, b + i, 8);
        a = (a ^ w) * 0x9e3779b97f4a7c15ull;
        c = (c + w) * 0xc2b2ae3d27d4eb4full;
    }
    for (; i < n; i++) {
        a = (a ^ b[i]) * 0x100000001b3ull;
        c = (c + b[i]) * 0xc2b2ae3d27d4eb4full;
    }
    h[0] = a ^ (a >> 32);
    h[1] = c ^ (c >> 31);
}

static inline uint32_t vka_drv_key_idx(uint16_t tag, const uint64_t k[4]) {
    uint64_t x = k[0] ^ (k[1] * 0x9e3779b97f4a7c15ull) ^ (k[2] * 0xc2b2ae3d27d4eb4full) ^ (k[3] * 0x165667b19e3779f9ull) ^ tag;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    return (uint32_t)((x ^ (x >> 27)) & (VKA_DRV_SETS - 1u));
}

static inline int vka_drv_key4_find(const vka_drv_key4 *m, uint32_t sub_gen, uint16_t tag,
                                    const uint64_t k[4], uint16_t sz, uint64_t *buf_out, uint64_t *off_out) {
    assert(m != NULL && k != NULL && buf_out != NULL && off_out != NULL);
    if (!sub_gen || !sz) return 0;
    const vka_drv_key_set *set = &m->s[vka_drv_key_idx(tag, k)];
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_drv_key_ent *e = &set->e[w];
        if ((VKA_DSET_FAST_FAULT(3) || e->sub_gen == sub_gen) && e->tag == tag && e->sz == sz &&
            e->k[0] == k[0] && e->k[1] == k[1] && e->k[2] == k[2] && e->k[3] == k[3]) {
            *buf_out = e->buf; *off_out = e->off;
            return 1;
        }
    }
    return 0;
}

static inline void vka_drv_key4_put(vka_drv_key4 *m, uint32_t sub_gen, uint16_t tag,
                                    const uint64_t k[4], uint16_t sz, uint64_t buf, uint64_t off) {
    assert(m != NULL && k != NULL);
    if (!sub_gen || !sz) return;
    vka_drv_key_set *set = &m->s[vka_drv_key_idx(tag, k)];
    uint32_t slot = VKA_DSET_WAYS;
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_drv_key_ent *e = &set->e[w];
        if (e->sub_gen != sub_gen || (e->tag == tag && e->sz == sz && e->k[0] == k[0] && e->k[1] == k[1] && e->k[2] == k[2] && e->k[3] == k[3])) {
            slot = w; break;
        }
    }
    if (slot == VKA_DSET_WAYS) slot = set->rr++ & (VKA_DSET_WAYS - 1u);
    set->e[slot] = (vka_drv_key_ent){{k[0], k[1], k[2], k[3]}, buf, off, sub_gen, tag, sz};
}

static inline uint32_t vka_drv_idx(uint16_t tag, uint64_t h0) {
    return (uint32_t)((h0 ^ ((uint64_t)tag * 0x9e3779b97f4a7c15ull >> 32)) & (VKA_DRV_SETS - 1u));
}

static inline int vka_drv_ring4_find(const vka_drv_ring4 *m, uint32_t sub_gen, uint16_t tag,
                                     const uint8_t *data, uint16_t sz, uint64_t *buf_out,
                                     uint8_t **ptr_out, uint64_t *off_out, uint64_t h_out[2]) {
    assert(m != NULL && data != NULL && buf_out != NULL && ptr_out != NULL && off_out != NULL && h_out != NULL);
    vka_hash_dual64(data, sz, h_out);
    if (!sub_gen || !sz) return 0;
    const vka_drv_ring_set *set = &m->s[vka_drv_idx(tag, h_out[0])];
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_drv_ring_ent *e = &set->e[w];
        if ((VKA_DSET_FAST_FAULT(3) || e->sub_gen == sub_gen) && e->tag == tag && e->sz == sz &&
            e->h0 == h_out[0] && e->h1 == h_out[1] && e->ptr && !memcmp(e->ptr, data, sz)) {
            *buf_out = e->buf; *ptr_out = e->ptr; *off_out = e->off;
            return 1;
        }
    }
    return 0;
}

static inline void vka_drv_ring4_put(vka_drv_ring4 *m, uint32_t sub_gen, uint16_t tag,
                                     uint16_t sz, const uint64_t h[2], uint64_t buf, uint8_t *ptr, uint64_t off) {
    assert(m != NULL && h != NULL);
    if (!sub_gen || !sz || !ptr) return;
    vka_drv_ring_set *set = &m->s[vka_drv_idx(tag, h[0])];
    uint32_t slot = VKA_DSET_WAYS;
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_drv_ring_ent *e = &set->e[w];
        if (e->sub_gen != sub_gen || (e->tag == tag && e->sz == sz && e->h0 == h[0] && e->h1 == h[1])) { slot = w; break; }
    }
    if (slot == VKA_DSET_WAYS) slot = set->rr++ & (VKA_DSET_WAYS - 1u);
    set->e[slot] = (vka_drv_ring_ent){h[0], h[1], buf, off, ptr, sub_gen, tag, sz};
}

static inline uint32_t vka_aref_idx(uint32_t ref_bits) {
    return (uint32_t)(((ref_bits * 2654435761u) >> 28) & (VKA_AREF_SETS - 1u));
}

static inline int vka_aref_memo4_find(const vka_aref_memo4 *m, uint32_t sub_gen, uint32_t ref_bits,
                                      uint64_t *buf_out, uint8_t **ptr_out, uint64_t *off_out) {
    assert(m != NULL && buf_out != NULL && ptr_out != NULL && off_out != NULL);
    if (!sub_gen) return 0;
    const vka_aref_set *set = &m->s[vka_aref_idx(ref_bits)];
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_aref_ent *e = &set->e[w];
        if (e->sub_gen == sub_gen && e->ref_bits == ref_bits && e->ptr) {
            *buf_out = e->buf; *ptr_out = e->ptr; *off_out = e->off;
            return 1;
        }
    }
    return 0;
}

static inline void vka_aref_memo4_put(vka_aref_memo4 *m, uint32_t sub_gen, uint32_t ref_bits,
                                      uint64_t buf, uint8_t *ptr, uint64_t off) {
    assert(m != NULL);
    if (!sub_gen || !ptr) return;
    vka_aref_set *set = &m->s[vka_aref_idx(ref_bits)];
    uint32_t slot = VKA_DSET_WAYS;
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_aref_ent *e = &set->e[w];
        if (e->sub_gen != sub_gen || e->ref_bits == ref_bits) { slot = w; break; }
    }
    if (slot == VKA_DSET_WAYS) slot = set->rr++ & (VKA_DSET_WAYS - 1u);
    set->e[slot] = (vka_aref_ent){buf, off, ptr, sub_gen, ref_bits};
}

static inline int vka_dset_memo4_find(const vka_dset_memo4 *m, uint32_t sub_gen, uint64_t set0,
                                      uint32_t n, const uint64_t h[2], uint64_t *set_out) {
    assert(m != NULL && h != NULL && set_out != NULL);
    if (!sub_gen || !set0) return 0;
    const vka_dset_set *set = &m->s[(uint32_t)(h[0] & (VKA_DSET_SETS - 1u))];
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_dset_ent *e = &set->e[w];
        if (e->sub_gen == sub_gen && e->set0 == set0 && e->n == n &&
            (VKA_DSET_FAST_FAULT(4) || (e->h0 == h[0] && e->h1 == h[1]))) {
            *set_out = e->set;
            return 1;
        }
    }
    return 0;
}

static inline void vka_dset_memo4_put(vka_dset_memo4 *m, uint32_t sub_gen, uint64_t set0,
                                      uint32_t n, const uint64_t h[2], uint64_t set_val) {
    assert(m != NULL && h != NULL);
    if (!sub_gen || !set0 || !set_val) return;
    vka_dset_set *set = &m->s[(uint32_t)(h[0] & (VKA_DSET_SETS - 1u))];
    uint32_t slot = VKA_DSET_WAYS;
    for (uint32_t w = 0; w < VKA_DSET_WAYS; w++) {
        const vka_dset_ent *e = &set->e[w];
        if (e->sub_gen != sub_gen || (e->set0 == set0 && e->n == n && e->h0 == h[0] && e->h1 == h[1])) { slot = w; break; }
    }
    if (slot == VKA_DSET_WAYS) slot = set->rr++ & (VKA_DSET_WAYS - 1u);
    set->e[slot] = (vka_dset_ent){set0, h[0], h[1], set_val, sub_gen, n};
}

#endif
