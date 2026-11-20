#ifndef VKA_SERIAL_FAST_H
#define VKA_SERIAL_FAST_H
/* Wave 21 (Opt 23): Lock-free TLS smap / gpu_owned / vkfmt front-memos (DM_VK_SERIAL_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W21_TEST_HOOKS
extern int g_w21_fault;
#define VKA_SERIAL_FAULT(n) (g_w21_fault == (n))
#else
#define VKA_SERIAL_FAULT(n) 0
#endif

static inline int vka_serial_fast_on(void) {
#if defined(VKA_ENV) && !defined(W21_TEST_HOOKS)
    return VKA_ENV("DM_VK_SERIAL_FAST", 1) != 0;
#else
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("DM_VK_SERIAL_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_relaxed);
    }
    return v;
#endif
}

enum {
    VKA_SMAP_SETS = 64u,
    VKA_GPU_OWN_SETS = 64u,
    VKA_VKFMT_SETS = 32u,
    VKA_SERIAL_WAYS = 4u
};

static inline uint32_t vka_serial_hash64(uint64_t x) {
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    return (uint32_t)x ^ (uint32_t)(x >> 16);
}

typedef struct {
    uint64_t serial, vep;
    uintptr_t im_u;
    uint32_t sgen;
    uint32_t pad;
} vka_smap_ent;

typedef struct {
    vka_smap_ent e[VKA_SMAP_SETS][VKA_SERIAL_WAYS];
    uint8_t hand[VKA_SMAP_SETS];
} vka_smap_memo4;

static inline int vka_smap_memo4_get(const vka_smap_memo4 *m, uint64_t serial,
                                     uint32_t sgen, uint64_t vep, uintptr_t *im_out) {
    assert(m != NULL && im_out != NULL);
    if (!serial || !sgen) return 0;
    uint32_t s = vka_serial_hash64(serial) & (VKA_SMAP_SETS - 1u);
    for (uint32_t w = 0; w < VKA_SERIAL_WAYS; w++) {
        const vka_smap_ent *e = &m->e[s][w];
        if (!e->im_u || e->serial != serial) continue;
        if (!VKA_SERIAL_FAULT(1) && (e->sgen != sgen || e->vep != vep)) continue;
        *im_out = e->im_u;
        return 1;
    }
    return 0;
}

static inline void vka_smap_memo4_put(vka_smap_memo4 *m, uint64_t serial,
                                      uint32_t sgen, uint64_t vep, uintptr_t im_u) {
    assert(m != NULL);
    if (!serial || !sgen || !im_u) return;
    uint32_t s = vka_serial_hash64(serial) & (VKA_SMAP_SETS - 1u);
    for (uint32_t w = 0; w < VKA_SERIAL_WAYS; w++) {
        vka_smap_ent *e = &m->e[s][w];
        if (!e->im_u || e->serial == serial) {
            e->serial = VKA_SERIAL_FAULT(4) ? (serial ^ 1u) : serial;
            e->vep = vep; e->im_u = im_u; e->sgen = sgen; e->pad = 0;
            return;
        }
    }
    uint32_t w = m->hand[s] & (VKA_SERIAL_WAYS - 1u);
    m->hand[s] = (uint8_t)((w + 1u) & (VKA_SERIAL_WAYS - 1u));
    vka_smap_ent *e = &m->e[s][w];
    e->serial = VKA_SERIAL_FAULT(4) ? (serial ^ 1u) : serial;
    e->vep = vep; e->im_u = im_u; e->sgen = sgen; e->pad = 0;
}

typedef struct {
    uint64_t serial;
    uint32_t ogen;
    uint8_t owned, valid;
    uint8_t pad[2];
} vka_gpu_own_ent;

typedef struct {
    vka_gpu_own_ent e[VKA_GPU_OWN_SETS][VKA_SERIAL_WAYS];
    uint8_t hand[VKA_GPU_OWN_SETS];
} vka_gpu_own_memo4;

static inline int vka_gpu_own_memo4_get(const vka_gpu_own_memo4 *m, uint64_t serial,
                                        uint32_t ogen, uint8_t *owned_out) {
    assert(m != NULL && owned_out != NULL);
    if (!serial || !ogen) return 0;
    uint32_t s = vka_serial_hash64(serial) & (VKA_GPU_OWN_SETS - 1u);
    for (uint32_t w = 0; w < VKA_SERIAL_WAYS; w++) {
        const vka_gpu_own_ent *e = &m->e[s][w];
        if (!e->valid || e->serial != serial) continue;
        if (!VKA_SERIAL_FAULT(2) && e->ogen != ogen) continue;
        *owned_out = e->owned;
        return 1;
    }
    return 0;
}

static inline void vka_gpu_own_memo4_put(vka_gpu_own_memo4 *m, uint64_t serial,
                                         uint32_t ogen, uint8_t owned) {
    assert(m != NULL);
    if (!serial || !ogen) return;
    uint32_t s = vka_serial_hash64(serial) & (VKA_GPU_OWN_SETS - 1u);
    for (uint32_t w = 0; w < VKA_SERIAL_WAYS; w++) {
        vka_gpu_own_ent *e = &m->e[s][w];
        if (!e->valid || e->serial == serial) {
            e->serial = serial; e->ogen = ogen;
            e->owned = owned ? 1u : 0u; e->valid = 1u;
            return;
        }
    }
    uint32_t w = m->hand[s] & (VKA_SERIAL_WAYS - 1u);
    m->hand[s] = (uint8_t)((w + 1u) & (VKA_SERIAL_WAYS - 1u));
    vka_gpu_own_ent *e = &m->e[s][w];
    e->serial = serial; e->ogen = ogen;
    e->owned = owned ? 1u : 0u; e->valid = 1u;
}

typedef struct {
    uint32_t id, tver, vkfmt;
    uint8_t valid;
    uint8_t pad[3];
} vka_vkfmt_ent;

typedef struct {
    vka_vkfmt_ent e[VKA_VKFMT_SETS][VKA_SERIAL_WAYS];
    uint8_t hand[VKA_VKFMT_SETS];
} vka_vkfmt_memo4;

static inline int vka_vkfmt_memo4_get(const vka_vkfmt_memo4 *m, uint32_t id,
                                      uint32_t tver, uint32_t *fmt_out) {
    assert(m != NULL && fmt_out != NULL);
    if (!id || !tver) return 0;
    uint32_t s = ((id * UINT32_C(0x9e3779b1)) >> 16) & (VKA_VKFMT_SETS - 1u);
    for (uint32_t w = 0; w < VKA_SERIAL_WAYS; w++) {
        const vka_vkfmt_ent *e = &m->e[s][w];
        if (!e->valid || e->id != id) continue;
        if (!VKA_SERIAL_FAULT(3) && e->tver != tver) continue;
        *fmt_out = e->vkfmt;
        return 1;
    }
    return 0;
}

static inline void vka_vkfmt_memo4_put(vka_vkfmt_memo4 *m, uint32_t id,
                                       uint32_t tver, uint32_t fmt) {
    assert(m != NULL);
    if (!id || !tver) return;
    uint32_t s = ((id * UINT32_C(0x9e3779b1)) >> 16) & (VKA_VKFMT_SETS - 1u);
    for (uint32_t w = 0; w < VKA_SERIAL_WAYS; w++) {
        vka_vkfmt_ent *e = &m->e[s][w];
        if (!e->valid || e->id == id) {
            e->id = id; e->tver = tver; e->vkfmt = fmt; e->valid = 1u;
            return;
        }
    }
    uint32_t w = m->hand[s] & (VKA_SERIAL_WAYS - 1u);
    m->hand[s] = (uint8_t)((w + 1u) & (VKA_SERIAL_WAYS - 1u));
    vka_vkfmt_ent *e = &m->e[s][w];
    e->id = id; e->tver = tver; e->vkfmt = fmt; e->valid = 1u;
}

#endif
