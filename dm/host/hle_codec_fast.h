#ifndef HLE_CODEC_FAST_H
#define HLE_CODEC_FAST_H
/* Wave 5 Opus host fast paths (DM_OPUS_FAST=1):
 *   1) NEON peak-amplitude reduction (exact for all int16_t values, -32768 -> 32768).
 *   2) Lock-free C11 atomic seqlock 16x4 caches for older C Opus handles (avoiding
 *      objmap global mutex + per-packet GetOpusDecoderChannelCount call) and
 *      SDK 21 manager decoder handles (avoiding g_mdec_lock + linked-list walk). */
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

enum { DM_OPUS_CACHE_SETS = 16, DM_OPUS_CACHE_WAYS = 4 };

typedef struct {
    uintptr_t key;
    void *val;
    int ch;
} dm_opus_slot;

typedef struct {
    _Atomic uint32_t seq;
    uint8_t hand;
    dm_opus_slot ways[DM_OPUS_CACHE_WAYS];
} dm_opus_set;

typedef struct {
    pthread_mutex_t wmu;
    dm_opus_set sets[DM_OPUS_CACHE_SETS];
} dm_opus_cache;

static inline unsigned dm_opus_cache_set(uintptr_t k) {
    uint64_t x = (uint64_t)k >> 4;
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    return (unsigned)(x & (DM_OPUS_CACHE_SETS - 1));
}

static inline void dm_opus_cache_init(dm_opus_cache *c) {
    if (!c) return;
    pthread_mutex_init(&c->wmu, NULL);
    for (unsigned s = 0; s < DM_OPUS_CACHE_SETS; s++) {
        atomic_init(&c->sets[s].seq, 0);
        c->sets[s].hand = 0;
        for (unsigned w = 0; w < DM_OPUS_CACHE_WAYS; w++) {
            c->sets[s].ways[w] = (dm_opus_slot){0, NULL, 0};
        }
    }
}

static inline void *dm_opus_cache_get(const dm_opus_cache *c, uintptr_t key, int *ch_out) {
    if (!c || !key) return NULL;
    const dm_opus_set *s = &c->sets[dm_opus_cache_set(key)];
    for (unsigned tries = 0; tries < 4; tries++) {
        uint32_t seq0 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (seq0 & 1u) continue;
        void *found = NULL;
        int ch = 0;
        for (unsigned w = 0; w < DM_OPUS_CACHE_WAYS; w++) {
            if (s->ways[w].key == key) { found = s->ways[w].val; ch = s->ways[w].ch; break; }
        }
        atomic_thread_fence(memory_order_acquire);
        uint32_t seq1 = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (seq0 == seq1) { if (found && ch_out) *ch_out = ch; return found; }
    }
    return NULL;
}

static inline void dm_opus_cache_put(dm_opus_cache *c, uintptr_t key, void *val, int ch) {
    if (!c || !key || !val) return;
    dm_opus_set *s = &c->sets[dm_opus_cache_set(key)];
    pthread_mutex_lock(&c->wmu);
    uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
    unsigned idx = DM_OPUS_CACHE_WAYS;
    for (unsigned w = 0; w < DM_OPUS_CACHE_WAYS; w++) {
        if (s->ways[w].key == key || !s->ways[w].key) { idx = w; break; }
    }
    if (idx >= DM_OPUS_CACHE_WAYS) idx = (unsigned)(s->hand++ & (DM_OPUS_CACHE_WAYS - 1));
    s->ways[idx] = (dm_opus_slot){key, val, ch};
    atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
    pthread_mutex_unlock(&c->wmu);
}

static inline void dm_opus_cache_del(dm_opus_cache *c, uintptr_t key) {
    if (!c || !key) return;
    dm_opus_set *s = &c->sets[dm_opus_cache_set(key)];
    pthread_mutex_lock(&c->wmu);
    for (unsigned w = 0; w < DM_OPUS_CACHE_WAYS; w++) {
        if (s->ways[w].key == key) {
            uint32_t seq = atomic_load_explicit(&s->seq, memory_order_relaxed);
            atomic_store_explicit(&s->seq, seq + 1u, memory_order_release);
            s->ways[w] = (dm_opus_slot){0, NULL, 0};
            atomic_store_explicit(&s->seq, seq + 2u, memory_order_release);
            break;
        }
    }
    pthread_mutex_unlock(&c->wmu);
}

static inline int dm_opus_pcm_peak_scalar(const int16_t *p, size_t n) {
    int pk = 0;
    for (size_t i = 0; i < n; i++) {
        int v = p[i] < 0 ? -(int)p[i] : (int)p[i];
        if (v > pk) pk = v;
    }
    return pk;
}

static inline int dm_opus_pcm_peak_fast(const int16_t *p, size_t n) {
    if (!p || !n) return 0;
    size_t i = 0;
    int pk = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    /* Track min & max in 16-bit NEON without widening; negate min once in int32_t so -32768 -> +32768. */
    int16x8_t vmin0 = vdupq_n_s16(0), vmax0 = vdupq_n_s16(0);
    int16x8_t vmin1 = vdupq_n_s16(0), vmax1 = vdupq_n_s16(0);
    for (; i + 16 <= n; i += 16) {
        int16x8_t a = vld1q_s16(p + i), b = vld1q_s16(p + i + 8);
        vmin0 = vminq_s16(vmin0, a); vmax0 = vmaxq_s16(vmax0, a);
        vmin1 = vminq_s16(vmin1, b); vmax1 = vmaxq_s16(vmax1, b);
    }
    int16x8_t vmin = vminq_s16(vmin0, vmin1), vmax = vmaxq_s16(vmax0, vmax1);
    for (; i + 8 <= n; i += 8) {
        int16x8_t a = vld1q_s16(p + i);
        vmin = vminq_s16(vmin, a); vmax = vmaxq_s16(vmax, a);
    }
    int pmin = -(int)vminvq_s16(vmin);
    int pmax = (int)vmaxvq_s16(vmax);
    pk = pmin > pmax ? pmin : pmax;
#endif
    for (; i < n; i++) {
        int v = p[i] < 0 ? -(int)p[i] : (int)p[i];
        if (v > pk) pk = v;
    }
    return pk;
}

#endif
