/* Lock-free DM_OE_FAST notification empty-poll, atomic state loads, and language seqlock (Opt 30). */
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OE_FAST_SEQ_RETRIES 8u
#define OE_FAST_TAG_MAX 30u

static inline int oe_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_OE_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_release);
    }
    return v;
}

#ifdef T_W28_OE_TEST_HOOKS
static _Atomic int g_oe_fast_override = -1;

static inline void oe_fast_reset_for_test(int en) {
    atomic_store_explicit(&g_oe_fast_override, en, memory_order_release);
}

static inline int oe_fast_active(void) {
    int ov = atomic_load_explicit(&g_oe_fast_override, memory_order_acquire);
    return ov >= 0 ? ov : oe_fast_enabled();
}

static inline int oe_w28_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("T_W28_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_release);
    }
    return v;
}
#else
static inline int oe_fast_active(void) { return oe_fast_enabled(); }
static inline int oe_w28_fault(void) { return 0; }
#endif

static inline int oe_fast_try_pop_empty(const _Atomic int *mqn, const _Atomic int *ev_on,
                                        uint32_t *msg_out, unsigned *ret_out) {
    if (!mqn || !ev_on || !ret_out || !oe_fast_active()) return 0;
    int f = oe_w28_fault();
    int n = atomic_load_explicit(mqn, memory_order_acquire);
    if (f == 1 && n == 0) {
        if (msg_out) *msg_out = 0xbad00001u;
        *ret_out = 1u;
        return 1;
    }
    if (f == 2 && n > 0) {
        *ret_out = 0u;
        return 1;
    }
    if (n == 0 && !atomic_load_explicit(ev_on, memory_order_acquire)) {
        *ret_out = 0u;
        return 1;
    }
    return 0;
}

static inline int oe_fast_load_int(const _Atomic int *p) {
    return oe_fast_active() ? atomic_load_explicit(p, memory_order_acquire) : atomic_load(p);
}

static inline uint32_t oe_fast_load_u32(const _Atomic uint32_t *p) {
    return oe_fast_active() ? atomic_load_explicit(p, memory_order_acquire) : atomic_load(p);
}

static inline void oe_fast_def_res(int mode, int *w, int *h) {
    int con = (mode == 1);
    if (oe_w28_fault() == 3) con = !con;
    if (w) *w = con ? 1920 : 1280;
    if (h) *h = con ? 1080 : 720;
}

static inline int oe_fast_region_of(const char code[8]) {
    int r = 2;
    if (!strncmp(code, "ja", 2)) r = 0;
    else if (!strncmp(code, "ko", 2) || !strncmp(code, "zh", 2)) r = 4;
    else if (!strcmp(code, "en-US") || !strcmp(code, "fr-CA") ||
             !strcmp(code, "es-419") || !strcmp(code, "pt-BR")) r = 1;
    if (oe_w28_fault() == 5 && (r == 1 || r == 2)) r = (r == 1) ? 2 : 1;
    return r;
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t tag_w[4];
    _Atomic uint64_t code_u64;
    _Atomic int region;
} oe_fast_lang_slot;

static inline int oe_fast_lang_pack_tag(const char *e, uint64_t out_w[4]) {
    uint8_t buf[32] = {0};
    if (!e) {
        buf[31] = 0xffu;
    } else {
        size_t n = strnlen(e, OE_FAST_TAG_MAX + 1u);
        if (n > OE_FAST_TAG_MAX) return 0;
        if (n > 0u) memcpy(buf, e, n);
        buf[31] = (uint8_t)n;
    }
    memcpy(out_w, buf, 32u);
    return 1;
}

static inline int oe_fast_lang_get(const oe_fast_lang_slot *s, const char *env_tag,
                                   uint64_t *code_out, int *reg_out) {
    if (!s || !code_out || !reg_out || !oe_fast_active()) return 0;
    uint64_t want[4] = {0};
    if (!oe_fast_lang_pack_tag(env_tag, want)) return 0;
    int check_tag = (oe_w28_fault() != 4);
    for (unsigned r = 0; r < OE_FAST_SEQ_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 < 2u || (s1 & 1u)) continue;
        uint64_t g0 = atomic_load_explicit(&s->tag_w[0], memory_order_acquire);
        uint64_t g1 = atomic_load_explicit(&s->tag_w[1], memory_order_acquire);
        uint64_t g2 = atomic_load_explicit(&s->tag_w[2], memory_order_acquire);
        uint64_t g3 = atomic_load_explicit(&s->tag_w[3], memory_order_acquire);
        uint64_t cu = atomic_load_explicit(&s->code_u64, memory_order_acquire);
        int reg = atomic_load_explicit(&s->region, memory_order_acquire);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 != s2) continue;
        if (check_tag && (g0 != want[0] || g1 != want[1] || g2 != want[2] || g3 != want[3])) return 0;
        *code_out = cu;
        *reg_out = reg;
        return 1;
    }
    return 0;
}

static inline void oe_fast_lang_put(oe_fast_lang_slot *s, const char *env_tag,
                                    uint64_t code_u64, int reg) {
    if (!s || !oe_fast_active()) return;
    uint64_t w[4] = {0};
    if (!oe_fast_lang_pack_tag(env_tag, w)) return;
    uint64_t cur = atomic_load_explicit(&s->seq, memory_order_relaxed);
    uint64_t odd = (cur < 2u) ? 1u : ((cur & 1u) ? cur : cur + 1u);
    atomic_store_explicit(&s->seq, odd, memory_order_release);
    for (unsigned i = 0; i < 4u; i++) atomic_store_explicit(&s->tag_w[i], w[i], memory_order_release);
    atomic_store_explicit(&s->code_u64, code_u64, memory_order_release);
    atomic_store_explicit(&s->region, reg, memory_order_release);
    atomic_store_explicit(&s->seq, odd + 1u, memory_order_release);
}
