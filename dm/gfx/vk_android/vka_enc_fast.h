#ifndef VKA_ENC_FAST_H
#define VKA_ENC_FAST_H
/* Wave 14 (Opt 16): SPSC tail shadow, dpool seqlock & packed UBO/SSBO writers (DM_VK_ENC_FAST=1). */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W14_TEST_HOOKS
extern int g_w14_fault;
#define VKA_ENC_FAULT(n) (g_w14_fault == (n))
#else
#define VKA_ENC_FAULT(n) 0
#endif

static inline int vka_enc_fast_on(void) {
    static _Atomic int v = -1;
    int r = atomic_load_explicit(&v, memory_order_relaxed);
    if (r < 0) {
        const char *e = getenv("DM_VK_ENC_FAST");
        r = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&v, r, memory_order_relaxed);
    }
    return r;
}

static inline int vka_spsc_has_room(uint64_t ph, size_t n, uint64_t cap,
                                    uint64_t *tail_shadow,
                                    const _Atomic uint64_t *tail_atom) {
    assert(tail_shadow != NULL && tail_atom != NULL && cap > 0u);
    if (ph + (uint64_t)n - *tail_shadow <= cap) {
        return 1;
    }
    *tail_shadow = atomic_load_explicit(tail_atom, memory_order_acquire);
    if (VKA_ENC_FAULT(1)) return 1;
    return ph + (uint64_t)n - *tail_shadow <= cap;
}

static inline int vka_dpool_seq_find(const _Atomic uint32_t *seq, const void *ents,
                                     size_t ent_stride, int n_ents,
                                     const void *pool, int *out_base) {
    assert(seq != NULL && ents != NULL && out_base != NULL);
    assert(ent_stride >= sizeof(void *) + sizeof(int));
    int lim = n_ents < 0 ? 0 : (n_ents > 16 ? 16 : n_ents);
    for (int tries = 0; tries < 4; tries++) {
        uint32_t s0 = atomic_load_explicit(seq, memory_order_acquire);
        if (!VKA_ENC_FAULT(2) && (s0 & 1u)) continue;
        int found = 0;
        for (int i = 0; i < lim; i++) {
            const uint8_t *row = (const uint8_t *)ents + (size_t)i * ent_stride;
            const void *ep = NULL;
            memcpy(&ep, row, sizeof ep);
            if (ep == pool) {
                memcpy(&found, row + sizeof(void *), sizeof found);
                break;
            }
        }
        atomic_thread_fence(memory_order_acquire);
        uint32_t s1 = atomic_load_explicit(seq, memory_order_relaxed);
        if (VKA_ENC_FAULT(2) || s0 == s1) {
            *out_base = found;
            return 1;
        }
    }
    return 0;
}

static inline void vka_pack_ubo_small_one(uint8_t *dst, uint32_t op_ubo_small,
                                          uint16_t st, uint16_t idx,
                                          uint64_t addr, uint32_t sz) {
    assert(dst != NULL);
    uint32_t hdr[2] = {op_ubo_small, 24u};
    uint16_t si[2] = {st, VKA_ENC_FAULT(3) ? 0u : idx};
    memcpy(dst, hdr, 8);
    memcpy(dst + 8, si, 4);
    memcpy(dst + 12, &sz, 4);
    memcpy(dst + 16, &addr, 8);
}

static inline void vka_pack_ubo_small_batch(uint8_t *dst, uint32_t op_ubo_small,
                                            uint16_t st, uint16_t first, int n,
                                            const uint64_t *addr_size_pairs) {
    assert(dst != NULL && (n == 0 || addr_size_pairs != NULL));
    for (int i = 0; i < n; i++) {
        uint16_t idx = VKA_ENC_FAULT(3) ? first : (uint16_t)(first + i);
        uint64_t addr = addr_size_pairs[2 * i];
        uint32_t sz = (uint32_t)addr_size_pairs[2 * i + 1];
        vka_pack_ubo_small_one(dst + (size_t)i * 24u, op_ubo_small, st, idx, addr, sz);
    }
}

static inline void vka_pack_buf_one(uint8_t *dst, uint32_t op, int32_t st,
                                    int32_t idx, uint64_t addr, uint64_t sz) {
    assert(dst != NULL);
    uint32_t hdr[2] = {op, 32u};
    int32_t si[2] = {st, VKA_ENC_FAULT(3) ? 0 : idx};
    memcpy(dst, hdr, 8);
    memcpy(dst + 8, si, 8);
    memcpy(dst + 16, &addr, 8);
    memcpy(dst + 24, &sz, 8);
}

static inline void vka_pack_ssbo_batch(uint8_t *dst, uint32_t op_ssbo,
                                       int32_t st, int32_t first, int n,
                                       const uint64_t *addr_size_pairs) {
    assert(dst != NULL && (n == 0 || addr_size_pairs != NULL));
    for (int i = 0; i < n; i++) {
        int32_t idx = VKA_ENC_FAULT(3) ? first : (first + i);
        uint64_t addr = addr_size_pairs[2 * i];
        uint64_t sz = addr_size_pairs[2 * i + 1];
        vka_pack_buf_one(dst + (size_t)i * 32u, op_ssbo, st, idx, addr, sz);
    }
}

#endif
