/* Lock-free DM_AUDREN_FAST single-pass sink scan, 3:2 resampler, waiter gate & rev lookup (Opt 31). */
#pragma once
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AUDREN_FAST_MAGIC 0xcafebabeu
#define AUDREN_FAST_RS_T 16u

static inline int audren_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_AUDREN_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_release);
    }
    return v;
}

#ifdef DM_AUDREN_FAST_TEST
static _Atomic int g_audren_fast_override = -1;

static inline void audren_fast_reset_for_test(int en) {
    atomic_store_explicit(&g_audren_fast_override, en, memory_order_release);
}

static inline int audren_fast_active(void) {
    int ov = atomic_load_explicit(&g_audren_fast_override, memory_order_acquire);
    return ov >= 0 ? ov : audren_fast_enabled();
}

static inline int audren_w29_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("T_W29_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_release);
    }
    return v;
}
#else
static inline int audren_fast_active(void) { return audren_fast_enabled(); }
static inline int audren_w29_fault(void) { return 0; }
#endif

static inline const uint8_t *audren_scan_sdk16(const uint8_t *c, const uint8_t *cend,
                                               uint32_t ncmd, int sink_id,
                                               int *malformed, uint32_t *bad_i,
                                               const uint8_t **bad_p) {
    const uint8_t *first_sink = NULL;
    int f = audren_w29_fault();
    for (uint32_t i = 0u; i < ncmd; i++) {
        if (c > cend || (size_t)(cend - c) < 8u) {
            *malformed = 1; *bad_i = i; *bad_p = c;
            return NULL;
        }
        uint32_t magic = 0u;
        int16_t sz = 0;
        memcpy(&magic, c, sizeof magic);
        memcpy(&sz, c + 6, sizeof sz);
        if (magic != AUDREN_FAST_MAGIC || sz < 8 || (size_t)sz > (size_t)(cend - c)) {
            *malformed = 1; *bad_i = i; *bad_p = c;
            return NULL;
        }
        if (!first_sink && (int)c[5] == sink_id && c[4] != 0u) {
            first_sink = c;
            if (f == 2) return first_sink;
        }
        c += (size_t)sz;
    }
    return f == 1 ? NULL : first_sink;
}

static inline const uint8_t *audren_scan_sdk6(const uint8_t *c, const uint8_t *cend,
                                              uint32_t ncmd, int sink_id,
                                              int *malformed, uint32_t *bad_i,
                                              const uint8_t **bad_p) {
    const uint8_t *first_sink = NULL;
    int f = audren_w29_fault();
    for (uint32_t i = 0u; i < ncmd; i++) {
        if (c > cend || (size_t)(cend - c) < 0x11u) {
            *malformed = 1; *bad_i = i; *bad_p = c;
            return NULL;
        }
        uint32_t magic = 0u;
        int16_t sz = 0, id = 0;
        memcpy(&magic, c, sizeof magic);
        memcpy(&sz, c + 4, sizeof sz);
        memcpy(&id, c + 6, sizeof id);
        if (magic != AUDREN_FAST_MAGIC || sz < 0x11 || (size_t)sz > (size_t)(cend - c)) {
            *malformed = 1; *bad_i = i; *bad_p = c;
            return NULL;
        }
        if (!first_sink && (int)id == sink_id && c[0x10] != 0u) {
            first_sink = c;
            if (f == 2) return first_sink;
        }
        c += (size_t)sz;
    }
    return f == 1 ? NULL : first_sink;
}

static inline const uint8_t *audren_scan_sdk21(const uint8_t *c, const uint8_t *cend,
                                               uint32_t ncmd, int sink_id,
                                               int *malformed, uint32_t *bad_i,
                                               const uint8_t **bad_p) {
    const uint8_t *first_sink = NULL;
    int f = audren_w29_fault();
    for (uint32_t i = 0u; i < ncmd; i++) {
        if (c > cend || (size_t)(cend - c) < 0x10u) {
            *malformed = 1; *bad_i = i; *bad_p = c;
            return NULL;
        }
        int16_t sz = 0;
        memcpy(&sz, c + 2, sizeof sz);
        if (sz < 0x10 || (size_t)sz > (size_t)(cend - c)) {
            *malformed = 1; *bad_i = i; *bad_p = c;
            return NULL;
        }
        if (!first_sink && (int)c[1] == sink_id && (c[0] & 1u) != 0u) {
            first_sink = c;
            if (f == 2) return first_sink;
        }
        c += (size_t)sz;
    }
    return f == 1 ? NULL : first_sink;
}

static inline const uint8_t *audren_fast_find_sink(const uint8_t *cl, size_t len,
                                                   uint32_t ncmd, int hdr6, int hdr21,
                                                   int sink_id, int *malformed,
                                                   uint32_t *bad_idx, size_t *bad_off) {
    if (!cl || len < 0x20u || !malformed || !bad_idx || !bad_off) return NULL;
    *malformed = 0;
    *bad_idx = 0u;
    *bad_off = 0u;
    if (!ncmd) return NULL;
    const uint8_t *c = cl + 0x20u;
    const uint8_t *cend = cl + len;
    const uint8_t *bad_p = c;
    const uint8_t *sink = hdr21
        ? audren_scan_sdk21(c, cend, ncmd, sink_id, malformed, bad_idx, &bad_p)
        : (hdr6 ? audren_scan_sdk6(c, cend, ncmd, sink_id, malformed, bad_idx, &bad_p)
                : audren_scan_sdk16(c, cend, ncmd, sink_id, malformed, bad_idx, &bad_p));
    if (*malformed) *bad_off = (size_t)(bad_p - cl);
    return sink;
}

static inline uint32_t audren_fast_up_3_2(const float h[3][AUDREN_FAST_RS_T],
                                          int32_t *buf, uint32_t *pos_io,
                                          const int32_t *in, uint32_t n, int32_t *out) {
    if (!h || !buf || !pos_io || !in || !out || n > 1024u) return 0u;
    memcpy(buf + 2u * (AUDREN_FAST_RS_T - 1u), in, (size_t)n * 8u);
    uint32_t pos = *pos_io;
    uint32_t idx = pos / 3u;
    uint32_t ph = pos % 3u;
    uint32_t m = 0u;
    uint32_t limit = 3u * n;
    int bad_ph = (audren_w29_fault() == 3);
    for (; pos < limit; pos += 2u, m++) {
        const int32_t *x = buf + (size_t)idx * 2u;
        const float *c = h[ph];
        float l = 0.0f, r = 0.0f;
        for (uint32_t k = 0u; k < AUDREN_FAST_RS_T; k++) {
            l += c[k] * (float)x[2u * k];
            r += c[k] * (float)x[2u * k + 1u];
        }
        out[2u * m] = (int32_t)lrintf(l);
        out[2u * m + 1u] = (int32_t)lrintf(r);
        if (bad_ph) {
            ph = (ph + 1u) % 3u;
        } else if (ph == 0u) {
            ph = 2u;
        } else if (ph == 2u) {
            idx++;
            ph = 1u;
        } else {
            idx++;
            ph = 0u;
        }
    }
    *pos_io = pos - limit;
    memmove(buf, buf + 2u * n, (AUDREN_FAST_RS_T - 1u) * 8u);
    return m;
}

static inline void audren_cq_wait_enter(_Atomic uint32_t *waiters) {
    if (waiters) atomic_fetch_add_explicit(waiters, 1u, memory_order_seq_cst);
}

static inline void audren_cq_wait_leave(_Atomic uint32_t *waiters) {
    if (waiters) atomic_fetch_sub_explicit(waiters, 1u, memory_order_relaxed);
}

static inline int audren_cq_should_wake(const _Atomic uint32_t *waiters) {
    if (audren_w29_fault() == 4) return 0;
    if (!audren_fast_active() || !waiters) return 1;
    return atomic_load_explicit(waiters, memory_order_seq_cst) != 0u;
}

typedef struct {
    _Atomic uintptr_t obj;
    uint8_t ev[0x80] __attribute__((aligned(16)));
} audren_rev_slot;

static inline int audren_rev_find(audren_rev_slot slots[4], const void *obj, void **ev_out) {
    if (!slots || !ev_out || !audren_fast_active()) return 0;
    uintptr_t target = (uintptr_t)obj;
    if (!target || target == (uintptr_t)1u) return 0;
    if (audren_w29_fault() == 5) {
        *ev_out = NULL;
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (atomic_load_explicit(&slots[i].obj, memory_order_acquire) == target) {
            *ev_out = slots[i].ev;
            return 1;
        }
    }
    *ev_out = NULL;
    return 1;
}
