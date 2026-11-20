/* Wave 23 (Opt 25): Lock-free HID motion, six-axis broadcast & haptics fast path. */
#pragma once
#include "hle_haptics.h"
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { HID_FAST_RETRIES = 8, HID_FAST_HAP_WORDS = 8 };

#ifdef T_W23_TEST_HOOKS
static _Atomic int g_hid_fast_ovr = -1;
static inline void dm_hid_fast_reset_for_test(int v) {
    atomic_store_explicit(&g_hid_fast_ovr, v, memory_order_release);
}
static inline int dm_hid_w23_fault(void) {
    static _Atomic int cached = -1;
    int f = atomic_load_explicit(&cached, memory_order_acquire);
    if (f < 0) {
        const char *e = getenv("T_W23_FAULT");
        f = e ? atoi(e) : 0;
        atomic_store_explicit(&cached, f, memory_order_release);
    }
    return f;
}
#else
static inline int dm_hid_w23_fault(void) { return 0; }
#endif

static inline int dm_hid_fast_on(void) {
#ifdef T_W23_TEST_HOOKS
    int o = atomic_load_explicit(&g_hid_fast_ovr, memory_order_acquire);
    if (o >= 0) return o != 0;
#endif
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_HID_FAST");
        v = !(e && e[0] == '0' && !e[1]);
        atomic_store_explicit(&cached, v, memory_order_release);
    }
    return v;
}

static inline int dm_rumble_log_fast_on(void) {
#ifdef T_W23_TEST_HOOKS
    return getenv("DM_RUMBLE_LOG") != NULL;
#else
    static _Atomic int cached = -1;
    int v = atomic_load_explicit(&cached, memory_order_acquire);
    if (v < 0) {
        v = getenv("DM_RUMBLE_LOG") != NULL;
        atomic_store_explicit(&cached, v, memory_order_release);
    }
    return v;
#endif
}

static inline int hid_fast_no_shake_window(
    int64_t newest_samp, int count, uint64_t t0, uint64_t end, uint64_t period_ns) {
    assert(period_ns > 0);
    if (dm_hid_w23_fault() == 3) return 1;
    if (count <= 1 || end <= t0) return 1;
    if (newest_samp < 0) return 0;
    uint64_t newest_t = (uint64_t)newest_samp * period_ns;
    int64_t oldest_samp = newest_samp - (int64_t)(count - 1);
    uint64_t oldest_t = oldest_samp > 0 ? (uint64_t)oldest_samp * period_ns : 0;
    return newest_t < t0 || oldest_t >= end;
}

static inline float hid_fast_amp(float x) {
    return isfinite(x) ? fmaxf(0, fminf(1, x)) : 0;
}

static inline float hid_fast_freq(float x, float fallback) {
    return isfinite(x) && x > 0 ? fmaxf(40, fminf(1250, x)) : fallback;
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t words[HID_FAST_HAP_WORDS];
} hid_fast_hap_slot;

_Static_assert(sizeof(dm_vibration) * 2 == HID_FAST_HAP_WORDS * 4, "hap words");

static inline void hid_fast_hap_write(
    hid_fast_hap_slot *s, dm_vibration m0, dm_vibration m1) {
    assert(s != NULL);
    dm_vibration pair[2] = {m0, m1};
    uint32_t tmp[HID_FAST_HAP_WORDS];
    memcpy(tmp, pair, sizeof pair);
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);
    atomic_thread_fence(memory_order_release);
    for (int i = 0; i < HID_FAST_HAP_WORDS; i++)
        atomic_store_explicit(&s->words[i], tmp[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 2u, memory_order_release);
}

static inline int hid_fast_hap_read_one(
    const hid_fast_hap_slot *s, int m, dm_vibration *out) {
    assert(s != NULL && (m == 0 || m == 1) && out != NULL);
    for (int t = 0; t < HID_FAST_RETRIES; t++) {
        uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        uint32_t tmp[4];
        int base = m * 4;
        for (int i = 0; i < 4; i++)
            tmp[i] = atomic_load_explicit(&s->words[base + i], memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2) { memcpy(out, tmp, sizeof *out); return 0; }
    }
    return -1;
}

static inline int hid_fast_vib_eq(dm_vibration a, dm_vibration b) {
    return a.amplitude_low == b.amplitude_low && a.frequency_low == b.frequency_low &&
           a.amplitude_high == b.amplitude_high && a.frequency_high == b.frequency_high;
}

static inline int hid_fast_hap_unchanged(
    const hid_fast_hap_slot *slots, uint32_t active_mask,
    const dm_vibration_update *u, int n) {
    assert(slots != NULL && u != NULL && n > 0);
    dm_vibration cur[DM_PLAYERS * 2];
    uint32_t loaded = 0;
    for (int i = 0; i < n; i++) {
        int p = u[i].player, m = u[i].motor;
        if (p < 0 || p >= DM_PLAYERS || m < 0 || m > 1) continue;
        if (!(active_mask >> p & 1u)) continue;
        if (dm_hid_w23_fault() == 5 && m == 1) continue;
        int k = p * 2 + m;
        if (!(loaded >> k & 1u)) {
            if (hid_fast_hap_read_one(&slots[p], m, &cur[k]) < 0) return 0;
            loaded |= 1u << k;
        }
        dm_vibration v = u[i].value;
        v.amplitude_low = hid_fast_amp(v.amplitude_low);
        v.amplitude_high = hid_fast_amp(v.amplitude_high);
        v.frequency_low = hid_fast_freq(v.frequency_low, 160);
        v.frequency_high = hid_fast_freq(v.frequency_high, 320);
        if (!hid_fast_vib_eq(v, cur[k])) return 0;
    }
    return 1;
}

#ifdef HID_FAST_MOTION
enum {
    HID_FAST_MOTION_SLOTS = DM_PLAYERS * 2 + 1,
    HID_FAST_MOTION_WORDS = (sizeof(motion_state) + 3u) / 4u
};

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint64_t last_ns, recv_ns;
    _Atomic uint32_t words[HID_FAST_MOTION_WORDS];
} hid_fast_motion_slot;

static inline void hid_fast_motion_write(
    hid_fast_motion_slot *s, const motion_state *st, uint64_t last_ns, uint64_t recv_ns) {
    assert(s != NULL && st != NULL);
    uint32_t tmp[HID_FAST_MOTION_WORDS];
    memset(tmp, 0, sizeof tmp);
    memcpy(tmp, st, sizeof *st);
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&s->last_ns, last_ns, memory_order_relaxed);
    atomic_store_explicit(&s->recv_ns, recv_ns, memory_order_relaxed);
    for (size_t i = 0; i < HID_FAST_MOTION_WORDS; i++)
        atomic_store_explicit(&s->words[i], tmp[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 2u, memory_order_release);
}

static inline void hid_fast_motion_clear_slot(hid_fast_motion_slot *s) {
    assert(s != NULL);
    uint32_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 1u, memory_order_release);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&s->last_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&s->recv_ns, 0, memory_order_relaxed);
    atomic_store_explicit(&s->seq, q + 2u, memory_order_release);
}

static inline int hid_fast_motion_read(
    const hid_fast_motion_slot *s, uint64_t now_t, uint64_t stale_ns,
    motion_state *out, uint64_t *out_last) {
    assert(s != NULL && out != NULL && out_last != NULL);
    int f = dm_hid_w23_fault();
    for (int t = 0; t < HID_FAST_RETRIES; t++) {
        uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if ((s1 & 1u) && f != 2) continue;
        uint64_t last = atomic_load_explicit(&s->last_ns, memory_order_relaxed);
        uint64_t recv = atomic_load_explicit(&s->recv_ns, memory_order_relaxed);
        uint32_t tmp[HID_FAST_MOTION_WORDS];
        for (size_t i = 0; i < HID_FAST_MOTION_WORDS; i++)
            tmp[i] = atomic_load_explicit(&s->words[i], memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2 || f == 2) {
            if (!last || (recv <= now_t && now_t - recv > stale_ns)) return 0;
            if (f == 2) tmp[0] ^= 0x3f800000u;
            memcpy(out, tmp, sizeof *out);
            *out_last = last;
            return 1;
        }
    }
    return -1;
}
#endif
