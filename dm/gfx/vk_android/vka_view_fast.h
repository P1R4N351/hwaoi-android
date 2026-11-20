#ifndef VKA_VIEW_FAST_H
#define VKA_VIEW_FAST_H
/* Wave 18 (Opt 20): Lock-free VkImageView, VkRenderPass & active-slice VkFramebuffer fast paths (DM_VK_VIEW_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W18_TEST_HOOKS
extern int g_w18_fault;
#define VKA_VIEW_FAULT(n) (g_w18_fault == (n))
#else
#define VKA_VIEW_FAULT(n) 0
#endif

static inline int vka_view_fast_on(void) {
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("DM_VK_VIEW_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
}

enum {
    VKA_VIEW_M4_SETS = 64u,
    VKA_RP_M4_SETS = 32u,
    VKA_FB_M4_SETS = 32u,
    VKA_VIEW_WAYS = 4u
};

static inline uint32_t vka_view_hash64(uint64_t a, uint64_t b, uint64_t c, uint32_t mask) {
    uint64_t x = (a ^ (b * UINT64_C(0x9e3779b97f4a7c15)) ^ (c * UINT64_C(0xbf58476d1ce4e5b9)));
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    return (uint32_t)x & mask;
}

typedef struct {
    uint64_t im_ptr, k0, k1, ev_gen, view_u;
} vka_view_ent;

typedef struct {
    vka_view_ent w[VKA_VIEW_WAYS];
    uint8_t hand, pad[7];
} vka_view_set;

typedef struct {
    vka_view_set s[VKA_VIEW_M4_SETS];
} vka_view_memo4;

static inline int vka_view_key_pack(int32_t fmt, uint32_t type, uint32_t asp,
                                    uint32_t level, uint32_t nlevels,
                                    uint32_t layer, uint32_t nlayers,
                                    const uint32_t swz[4], int strict,
                                    uint64_t *k0_out, uint64_t *k1_out) {
    assert(swz != NULL && k0_out != NULL && k1_out != NULL);
    if (type >= 256u || asp >= 65536u || level >= 256u || nlevels >= 256u ||
        layer >= 65536u || nlayers >= 65536u ||
        swz[0] >= 8u || swz[1] >= 8u || swz[2] >= 8u || swz[3] >= 8u) return 0;
    uint32_t lvl = VKA_VIEW_FAULT(1) ? 0u : level;
    *k0_out = (uint64_t)(uint32_t)fmt | ((uint64_t)type << 32) |
              ((uint64_t)asp << 40) | ((uint64_t)(strict != 0) << 56);
    *k1_out = (uint64_t)lvl | ((uint64_t)nlevels << 8) |
              ((uint64_t)layer << 16) | ((uint64_t)nlayers << 32) |
              ((uint64_t)swz[0] << 48) | ((uint64_t)swz[1] << 51) |
              ((uint64_t)swz[2] << 54) | ((uint64_t)swz[3] << 57);
    return 1;
}

static inline int vka_view_memo4_get(const vka_view_memo4 *m, uint64_t im_ptr,
                                     uint64_t k0, uint64_t k1, uint64_t ev_gen,
                                     uint64_t *view_out) {
    assert(m != NULL && view_out != NULL);
    const vka_view_set *set = &m->s[vka_view_hash64(im_ptr, k0, k1, VKA_VIEW_M4_SETS - 1u)];
    for (uint32_t i = 0; i < VKA_VIEW_WAYS; i++) {
        const vka_view_ent *e = &set->w[i];
        if (e->view_u && e->ev_gen == ev_gen && e->im_ptr == im_ptr && e->k0 == k0 && e->k1 == k1) {
            *view_out = e->view_u;
            return 1;
        }
    }
    return 0;
}

static inline void vka_view_memo4_put(vka_view_memo4 *m, uint64_t im_ptr,
                                      uint64_t k0, uint64_t k1, uint64_t ev_gen,
                                      uint64_t view_u) {
    assert(m != NULL && view_u != 0u);
    vka_view_set *set = &m->s[vka_view_hash64(im_ptr, k0, k1, VKA_VIEW_M4_SETS - 1u)];
    vka_view_ent ent = {im_ptr, k0, k1, ev_gen, view_u};
    for (uint32_t i = 0; i < VKA_VIEW_WAYS; i++) {
        vka_view_ent *e = &set->w[i];
        if (!e->view_u || e->ev_gen != ev_gen || (e->im_ptr == im_ptr && e->k0 == k0 && e->k1 == k1)) {
            *e = ent;
            return;
        }
    }
    uint32_t v = set->hand & (VKA_VIEW_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_VIEW_WAYS - 1u));
    set->w[v] = ent;
}

typedef struct {
    uint64_t w[7];
    uint64_t rp_u;
    uint32_t valid, pad_;
} vka_rp_ent;

typedef struct {
    vka_rp_ent w[VKA_VIEW_WAYS];
    uint8_t hand, pad[7];
} vka_rp_set;

typedef struct {
    vka_rp_set s[VKA_RP_M4_SETS];
} vka_rp_memo4;

static inline uint32_t vka_rp_set_idx(const uint64_t w[7]) {
    uint64_t h = w[0] ^ (w[1] * UINT64_C(0x9e3779b97f4a7c15)) ^
                 (w[2] * UINT64_C(0xbf58476d1ce4e5b9)) ^ (w[3] * UINT64_C(0x94d049bb133111eb)) ^
                 w[4] ^ (w[5] * UINT64_C(0x9e3779b97f4a7c15));
    if (!VKA_VIEW_FAULT(2)) h ^= w[6] * UINT64_C(0xbf58476d1ce4e5b9);
    h = (h ^ (h >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    return (uint32_t)(h ^ (h >> 27)) & (VKA_RP_M4_SETS - 1u);
}

static inline int vka_rp_eq_words(const uint64_t a[7], const uint64_t b[7]) {
    uint32_t lim = VKA_VIEW_FAULT(2) ? 6u : 7u;
    for (uint32_t i = 0; i < lim; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static inline int vka_rp_memo4_get(const vka_rp_memo4 *m, const void *rpkey_ptr, uint64_t *rp_out) {
    assert(m != NULL && rpkey_ptr != NULL && rp_out != NULL);
    uint64_t kw[7];
    memcpy(kw, rpkey_ptr, sizeof kw);
    const vka_rp_set *set = &m->s[vka_rp_set_idx(kw)];
    for (uint32_t i = 0; i < VKA_VIEW_WAYS; i++) {
        const vka_rp_ent *e = &set->w[i];
        if (e->valid && vka_rp_eq_words(e->w, kw)) { *rp_out = e->rp_u; return 1; }
    }
    return 0;
}

static inline void vka_rp_memo4_put(vka_rp_memo4 *m, const void *rpkey_ptr, uint64_t rp_u) {
    assert(m != NULL && rpkey_ptr != NULL && rp_u != 0u);
    uint64_t kw[7];
    memcpy(kw, rpkey_ptr, sizeof kw);
    vka_rp_set *set = &m->s[vka_rp_set_idx(kw)];
    vka_rp_ent ent = {{kw[0], kw[1], kw[2], kw[3], kw[4], kw[5], kw[6]}, rp_u, 1u, 0u};
    for (uint32_t i = 0; i < VKA_VIEW_WAYS; i++) {
        if (!set->w[i].valid || vka_rp_eq_words(set->w[i].w, kw)) { set->w[i] = ent; return; }
    }
    uint32_t v = set->hand & (VKA_VIEW_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_VIEW_WAYS - 1u));
    set->w[v] = ent;
}

static inline size_t vka_fbkey_active_bytes(uint32_t na, size_t total_sz,
                                            size_t hdr_sz, size_t att_sz) {
    assert(hdr_sz <= total_sz && att_sz > 0u);
    if (VKA_VIEW_FAULT(3)) return hdr_sz;
    if (na > 9u) return total_sz;
    size_t act = hdr_sz + (size_t)na * att_sz;
    return act <= total_sz ? act : total_sz;
}

static inline uint64_t vka_fbkey_hash_fast(const void *key, uint32_t na,
                                           size_t total_sz, size_t hdr_sz, size_t att_sz) {
    assert(key != NULL);
    size_t len = vka_fbkey_active_bytes(na, total_sz, hdr_sz, att_sz);
    const uint8_t *p = (const uint8_t *)key;
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)len;
    for (size_t off = 0; off + 8u <= len; off += 8u) {
        uint64_t w = 0;
        memcpy(&w, p + off, 8u);
        h = (h ^ w) * UINT64_C(0xbf58476d1ce4e5b9);
        h ^= h >> 29;
    }
    for (size_t off = len & ~(size_t)7u; off < len; off++) {
        h = (h ^ p[off]) * UINT64_C(0x94d049bb133111eb);
    }
    return h ^ (h >> 31);
}

static inline int vka_fbkey_eq_fast(const void *a, const void *b,
                                    uint32_t na_a, uint32_t na_b,
                                    size_t total_sz, size_t hdr_sz, size_t att_sz) {
    assert(a != NULL && b != NULL);
    if (!VKA_VIEW_FAULT(3) && na_a != na_b) return 0;
    size_t len = vka_fbkey_active_bytes(na_b, total_sz, hdr_sz, att_sz);
    return memcmp(a, b, len) == 0;
}

typedef struct {
    uint64_t h, rp_u, fb_u;
    uint32_t slot_idx, valid;
} vka_fb_ent;

typedef struct {
    vka_fb_ent w[VKA_VIEW_WAYS];
    uint8_t hand, pad[7];
} vka_fb_set;

typedef struct {
    vka_fb_set s[VKA_FB_M4_SETS];
} vka_fb_memo4;

static inline void vka_fb_memo4_clear(vka_fb_memo4 *m) {
    assert(m != NULL);
    memset(m, 0, sizeof *m);
}

static inline int vka_fb_memo4_get(const vka_fb_memo4 *m, uint64_t h, uint64_t rp_u,
                                   uint32_t *slot_out, uint64_t *fb_out) {
    assert(m != NULL && slot_out != NULL && fb_out != NULL);
    const vka_fb_set *set = &m->s[(uint32_t)(h ^ (h >> 16)) & (VKA_FB_M4_SETS - 1u)];
    for (uint32_t i = 0; i < VKA_VIEW_WAYS; i++) {
        const vka_fb_ent *e = &set->w[i];
        if (e->valid && e->h == h && e->rp_u == rp_u) {
            *slot_out = e->slot_idx;
            *fb_out = e->fb_u;
            return 1;
        }
    }
    return 0;
}

static inline void vka_fb_memo4_put(vka_fb_memo4 *m, uint64_t h, uint64_t rp_u,
                                    uint32_t slot_idx, uint64_t fb_u) {
    assert(m != NULL && fb_u != 0u);
    vka_fb_set *set = &m->s[(uint32_t)(h ^ (h >> 16)) & (VKA_FB_M4_SETS - 1u)];
    vka_fb_ent ent = {h, rp_u, fb_u, slot_idx, 1u};
    for (uint32_t i = 0; i < VKA_VIEW_WAYS; i++) {
        if (!set->w[i].valid || (set->w[i].h == h && set->w[i].rp_u == rp_u)) { set->w[i] = ent; return; }
    }
    uint32_t v = set->hand & (VKA_VIEW_WAYS - 1u);
    set->hand = (uint8_t)((set->hand + 1u) & (VKA_VIEW_WAYS - 1u));
    set->w[v] = ent;
}

#endif
