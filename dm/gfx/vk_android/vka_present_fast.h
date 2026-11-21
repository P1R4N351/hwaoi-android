#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W36_TEST_HOOKS
extern _Atomic int g_vka_present_fast_state;
extern _Atomic int g_vka_w36_fault;
static inline void vka_present_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_present_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w36_set_fault(int mode) { atomic_store_explicit(&g_vka_w36_fault, mode, memory_order_release); }
static inline int vka_w36_fault(void) { return atomic_load_explicit(&g_vka_w36_fault, memory_order_acquire); }
#else
static _Atomic int g_vka_present_fast_state;
static inline int vka_w36_fault(void) { return 0; }
#endif

static inline int vka_present_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_present_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_PRESENT_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_present_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline int vka_parse_u32_span(const char *s, size_t len, size_t *pos, int max_v, int *out) {
    size_t i = *pos;
    int v = 0, any = 0;
    if (i < len && s[i] == '+') i++;
    for (unsigned step = 0; step < 16u && i < len && s[i] >= '0' && s[i] <= '9'; step++, i++) {
        any = 1;
        int d = (int)(s[i] - '0');
        if (v > (max_v - d) / 10) return 0;
        v = v * 10 + d;
    }
    if (!any || v <= 0) return 0;
    *pos = i;
    *out = v;
    return 1;
}

static inline int vka_parse_aspect_fast(const char *s, int *aw, int *ah, int fault_mode) {
    if (!s || !aw || !ah) return 0;
    size_t len = strlen(s), i = 0;
    if (len < 3u || len > 64u) return 0;
    for (unsigned step = 0; step < 32u && i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'); step++, i++) {}
    int a = 0, b = 0;
    if (!vka_parse_u32_span(s, len, &i, 65536, &a) || i >= len || s[i] != ':') return 0;
    i++;
    for (unsigned step = 0; step < 32u && i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'); step++, i++) {}
    if (!vka_parse_u32_span(s, len, &i, 65536, &b)) return 0;
    *aw = fault_mode == 1 ? a + 1 : a;
    *ah = b;
    return 1;
}

typedef struct {
    uint32_t gen;
    int inited, ps, rot, stretch, pic_pct, fsr_env, near_env, ownfmt, prewait, fr_on;
    int aspect_w, aspect_h;
    uint32_t rcas_con0_bits;
    uint64_t fr_hold_ms, gate_wait_turnip_ns, gate_wait_other_ns;
    double fsr_sharp;
} vka_pres_cfg;

static inline const vka_pres_cfg *vka_pres_cfg_sync(
    vka_pres_cfg *c, uint32_t env_gen, void (*loader)(vka_pres_cfg *), int fault_mode) {
    if (!c) return NULL;
    if (c->inited && c->gen == env_gen) return c;
    if (fault_mode == 2 && c->inited) return c;
    c->gen = env_gen;
    c->inited = 1;
    if (loader) loader(c);
    return c;
}

enum { VKA_PRES_NFRAME = 3 };
typedef struct {
    uint64_t rot_serial[VKA_PRES_NFRAME], rot_img[VKA_PRES_NFRAME], rot_view[VKA_PRES_NFRAME];
    int rot_samp[VKA_PRES_NFRAME];
    uint64_t fsr_serial[VKA_PRES_NFRAME], fsr_img[VKA_PRES_NFRAME], fsr_view[VKA_PRES_NFRAME];
    uint64_t fsr_mid_rev[VKA_PRES_NFRAME];
} vka_pres_dset_memo;

static inline int vka_pres_rot_need_write(
    vka_pres_dset_memo *m, unsigned k, uint64_t serial, uint64_t img_h, uint64_t view_h, int samp_idx, int fault_mode) {
    if (!m || k >= VKA_PRES_NFRAME || !serial || !img_h || !view_h) return 1;
    if (m->rot_serial[k] == serial && m->rot_img[k] == img_h &&
        m->rot_view[k] == view_h && m->rot_samp[k] == samp_idx) return 0;
    if (fault_mode == 3) return 0;
    m->rot_serial[k] = serial;
    m->rot_img[k] = img_h;
    m->rot_view[k] = view_h;
    m->rot_samp[k] = samp_idx;
    return 1;
}

static inline int vka_pres_fsr_src_need_write(
    vka_pres_dset_memo *m, unsigned k, uint64_t serial, uint64_t img_h, uint64_t view_h, int fault_mode) {
    if (!m || k >= VKA_PRES_NFRAME || !serial || !img_h || !view_h) return 1;
    if (m->fsr_serial[k] == serial && m->fsr_img[k] == img_h && m->fsr_view[k] == view_h) return 0;
    if (fault_mode == 3) return 0;
    m->fsr_serial[k] = serial;
    m->fsr_img[k] = img_h;
    m->fsr_view[k] = view_h;
    return 1;
}

static inline int vka_pres_fsr_mid_need_write(
    vka_pres_dset_memo *m, unsigned k, uint64_t mid_rev, int fault_mode) {
    if (!m || k >= VKA_PRES_NFRAME || !mid_rev) return 1;
    if (m->fsr_mid_rev[k] == mid_rev) return 0;
    if (fault_mode == 3) return 0;
    m->fsr_mid_rev[k] = mid_rev;
    return 1;
}

static inline void vka_pres_dset_clear_mid(vka_pres_dset_memo *m, unsigned k) {
    if (m && k < VKA_PRES_NFRAME) m->fsr_mid_rev[k] = 0u;
}

static inline void vka_pres_dset_reset(vka_pres_dset_memo *m) {
    if (m) memset(m, 0, sizeof *m);
}

enum { VKA_PRES_PRING = 512 };
typedef struct {
    _Atomic uint32_t seq;
    uint32_t pad;
    _Atomic uint64_t n;
    _Atomic uint64_t t_bits;
} vka_pres_ring_slot;

typedef struct {
    vka_pres_ring_slot s[VKA_PRES_PRING];
} vka_pres_ring512;

static inline void vka_pres_ring_note(vka_pres_ring512 *r, uint64_t n, double t) {
    if (!r || !n) return;
    vka_pres_ring_slot *sl = &r->s[n % VKA_PRES_PRING];
    uint32_t seq = atomic_load_explicit(&sl->seq, memory_order_relaxed);
    uint64_t tb = 0u;
    memcpy(&tb, &t, sizeof tb);
    (void)atomic_exchange_explicit(&sl->seq, seq + 1u, memory_order_acq_rel);
    atomic_store_explicit(&sl->n, n, memory_order_release);
    atomic_store_explicit(&sl->t_bits, tb, memory_order_release);
    atomic_store_explicit(&sl->seq, seq + 2u, memory_order_release);
}

static inline double vka_pres_ring_read(const vka_pres_ring512 *r, uint64_t n, int fault_mode) {
    if (!r || !n) return 0.0;
    const vka_pres_ring_slot *sl = &r->s[n % VKA_PRES_PRING];
    for (unsigned it = 0; it < 64u; it++) {
        uint32_t s0 = atomic_load_explicit(&sl->seq, memory_order_acquire);
        if (s0 & 1u) continue;
        uint64_t rn = atomic_load_explicit(&sl->n, memory_order_acquire);
        uint64_t tb = atomic_load_explicit(&sl->t_bits, memory_order_acquire);
        uint32_t s1 = atomic_load_explicit(&sl->seq, memory_order_relaxed);
        if (s0 == s1) {
            if (rn != n) return 0.0;
            if (fault_mode == 4) return -999.0;
            double t = 0.0;
            memcpy(&t, &tb, sizeof t);
            return t;
        }
    }
    return 0.0;
}

typedef struct {
    _Atomic uint64_t in_sh, done_sh;
    uint32_t waiters;
} vka_pq_sync;

static inline int vka_pq_already_done(const vka_pq_sync *q, uint64_t tk, int fault_mode) {
    if (!q) return 0;
    if (fault_mode == 5) return 1;
    return atomic_load_explicit(&q->done_sh, memory_order_acquire) >= tk;
}

static inline int vka_pq_should_wake(uint32_t waiters, int fast) {
    return !fast || waiters > 0u;
}
