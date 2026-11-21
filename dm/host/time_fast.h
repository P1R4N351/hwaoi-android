#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TIME_FAST_RETRIES 8

static inline int time_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("DM_TIME_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_relaxed);
    }
    return v;
}

#ifdef T_W25_TEST_HOOKS
static _Atomic int g_time_fast_override = -1;

static inline void time_fast_reset_for_test(int en) {
    atomic_store_explicit(&g_time_fast_override, en, memory_order_relaxed);
}

static inline int time_fast_active(void) {
    int ov = atomic_load_explicit(&g_time_fast_override, memory_order_relaxed);
    return ov >= 0 ? ov : time_fast_enabled();
}

static inline int time_fast_w25_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("T_W25_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_relaxed);
    }
    return v;
}
#else
static inline int time_fast_active(void) { return time_fast_enabled(); }
static inline int time_fast_w25_fault(void) { return 0; }
#endif

static inline uint64_t time_fast_tz_tag(const char *tz) {
    if (!tz) return 0ull;
    uint64_t h = 14695981039346656037ull;
    for (int i = 0; i < 128 && tz[i]; i++) {
        h ^= (uint8_t)tz[i];
        h *= 1099511628211ull;
    }
    return h | 1ull;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic int64_t drift;
    _Atomic uint64_t w[4];
} time_fast_ctx_slot;

static inline void time_fast_ctx_write(time_fast_ctx_slot *s, int64_t d,
                                       const uint8_t ctx[0x20]) {
    uint64_t w[4] = {0};
    memcpy(w, ctx, 0x20);
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->drift, d, memory_order_relaxed);
    for (int i = 0; i < 4; i++)
        atomic_store_explicit(&s->w[i], w[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}

static inline int time_fast_ctx_read(const time_fast_ctx_slot *s, int64_t ft,
                                     int64_t d, uint8_t out[0x20]) {
    for (int r = 0; r < TIME_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        int64_t sd = atomic_load_explicit(&s->drift, memory_order_relaxed);
        int64_t diff = d >= sd ? d - sd : sd - d;
        if (ft < 0 && diff > 1) return 0;
        uint64_t w[4];
        for (int i = 0; i < 4; i++)
            w[i] = atomic_load_explicit(&s->w[i], memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2) {
            memcpy(out, w, 0x20);
            if (time_fast_w25_fault() == 1) out[0] ^= 0xffu;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic int64_t posix_sec;
    _Atomic uint64_t tz_tag;
    _Atomic uint64_t ct_w;
    _Atomic uint64_t ai_w[3];
} time_fast_cal_slot;

static inline void time_fast_cal_write(time_fast_cal_slot *s, int64_t sec,
                                       uint64_t tz, const void *ct,
                                       const void *ai) {
    uint64_t cw = 0;
    uint64_t aw[3] = {0, 0, 0};
    if (ct) memcpy(&cw, ct, 8);
    if (ai) memcpy(aw, ai, 24);
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->posix_sec, sec, memory_order_relaxed);
    atomic_store_explicit(&s->tz_tag, tz, memory_order_relaxed);
    atomic_store_explicit(&s->ct_w, cw, memory_order_relaxed);
    for (int i = 0; i < 3; i++)
        atomic_store_explicit(&s->ai_w[i], aw[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}

static inline int time_fast_cal_read(const time_fast_cal_slot *s, int64_t sec,
                                     uint64_t tz, void *ct_out, void *ai_out) {
    int f = time_fast_w25_fault();
    for (int r = 0; r < TIME_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        int64_t ssec = atomic_load_explicit(&s->posix_sec, memory_order_relaxed);
        uint64_t stz = atomic_load_explicit(&s->tz_tag, memory_order_relaxed);
        if ((f != 2 && ssec != sec) || (f != 3 && stz != tz)) return 0;
        uint64_t cw = atomic_load_explicit(&s->ct_w, memory_order_relaxed);
        uint64_t aw[3];
        for (int i = 0; i < 3; i++)
            aw[i] = atomic_load_explicit(&s->ai_w[i], memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2) {
            if (ct_out) memcpy(ct_out, &cw, 8);
            if (ai_out) memcpy(ai_out, aw, 24);
            return 1;
        }
    }
    return 0;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t tz_tag;
    _Atomic uint64_t loc_w[5];
} time_fast_loc_slot;

static inline void time_fast_loc_write(time_fast_loc_slot *s, uint64_t tz,
                                       const char in[0x24]) {
    uint64_t lw[5] = {0, 0, 0, 0, 0};
    memcpy(lw, in, 0x24);
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->tz_tag, tz, memory_order_relaxed);
    for (int i = 0; i < 5; i++)
        atomic_store_explicit(&s->loc_w[i], lw[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}

static inline int time_fast_loc_read(const time_fast_loc_slot *s, uint64_t tz,
                                     char out[0x24]) {
    for (int r = 0; r < TIME_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        uint64_t stz = atomic_load_explicit(&s->tz_tag, memory_order_relaxed);
        if (stz != tz) return 0;
        uint64_t lw[5];
        for (int i = 0; i < 5; i++)
            lw[i] = atomic_load_explicit(&s->loc_w[i], memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2) {
            memcpy(out, lw, 0x24);
            if (time_fast_w25_fault() == 4) memcpy(out, "BADTZ", 6);
            return 1;
        }
    }
    return 0;
}

typedef struct {
    _Atomic uint64_t lo_mask;
    _Atomic uint32_t last_key;
} time_fast_sf_seen;

static inline void time_fast_sf_seen_init(time_fast_sf_seen *s) {
    atomic_store_explicit(&s->lo_mask, 0ull, memory_order_relaxed);
    atomic_store_explicit(&s->last_key, 0u, memory_order_relaxed);
}

static inline int time_fast_sf_seen_mark(time_fast_sf_seen *s, unsigned key) {
    int first;
    if (key < 64u) {
        uint64_t bit = 1ull << key;
        uint64_t prev = atomic_fetch_or_explicit(&s->lo_mask, bit, memory_order_relaxed);
        first = (prev & bit) == 0;
    } else {
        uint32_t tag = key | 0x80000000u;
        first = atomic_exchange_explicit(&s->last_key, tag, memory_order_relaxed) != tag;
    }
    if (time_fast_w25_fault() == 5) return 1;
    return first;
}
