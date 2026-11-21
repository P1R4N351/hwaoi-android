#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define GATE_FAST_OM_WAYS 64u
#define GATE_FAST_MOD_WAYS 64u
#define GATE_FAST_RETRIES 8

static inline int gate_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("DM_GATE_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_relaxed);
    }
    return v;
}

#ifdef T_W26_TEST_HOOKS
static _Atomic int g_gate_fast_override = -1;

static inline void gate_fast_reset_for_test(int en) {
    atomic_store_explicit(&g_gate_fast_override, en, memory_order_relaxed);
}

static inline int gate_fast_active(void) {
    int ov = atomic_load_explicit(&g_gate_fast_override, memory_order_relaxed);
    return ov >= 0 ? ov : gate_fast_enabled();
}

static inline int gate_fast_w26_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("T_W26_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_relaxed);
    }
    return v;
}
#else
static inline int gate_fast_active(void) { return gate_fast_enabled(); }
static inline int gate_fast_w26_fault(void) { return 0; }
#endif

static inline unsigned gate_fast_ptr_hash(uintptr_t x) {
    x ^= x >> 29;
    x *= 0xbf58476d1ce4e5b9ull;
    return (unsigned)(x >> 40);
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uintptr_t k;
    _Atomic uintptr_t v;
    _Atomic uint64_t gen;
} gate_fast_om_slot;

typedef struct {
    _Atomic uint64_t gen;
    gate_fast_om_slot s[GATE_FAST_OM_WAYS];
} gate_fast_om_cache;

static inline int gate_fast_om_get(const gate_fast_om_cache *c, const void *k, void **out) {
    uintptr_t kp = (uintptr_t)k;
    const gate_fast_om_slot *s = &c->s[gate_fast_ptr_hash(kp) & (GATE_FAST_OM_WAYS - 1u)];
    int f = gate_fast_w26_fault();
    uint64_t g0 = atomic_load_explicit(&c->gen, memory_order_acquire);
    for (int r = 0; r < GATE_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        uintptr_t sk = atomic_load_explicit(&s->k, memory_order_relaxed);
        uintptr_t sv = atomic_load_explicit(&s->v, memory_order_relaxed);
        uint64_t sg = atomic_load_explicit(&s->gen, memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 != s2) continue;
        uint64_t g1 = atomic_load_explicit(&c->gen, memory_order_acquire);
        if (sk == kp && (f == 2 || (sg == g0 && sg == g1))) {
            if (f == 1) sv ^= 0x10u;
            *out = (void *)sv;
            return 1;
        }
        return 0;
    }
    return 0;
}

static inline void gate_fast_om_put(gate_fast_om_cache *c, const void *k, const void *v, uint64_t gen) {
    gate_fast_om_slot *s = &c->s[gate_fast_ptr_hash((uintptr_t)k) & (GATE_FAST_OM_WAYS - 1u)];
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->k, (uintptr_t)k, memory_order_relaxed);
    atomic_store_explicit(&s->v, (uintptr_t)v, memory_order_relaxed);
    atomic_store_explicit(&s->gen, gen, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}

static inline uint64_t gate_fast_om_invalidate(gate_fast_om_cache *c, const void *k) {
    uint64_t g = atomic_fetch_add_explicit(&c->gen, 1ull, memory_order_acq_rel) + 1ull;
    gate_fast_om_slot *s = &c->s[gate_fast_ptr_hash((uintptr_t)k) & (GATE_FAST_OM_WAYS - 1u)];
    if (atomic_load_explicit(&s->k, memory_order_relaxed) == (uintptr_t)k) {
        uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
        atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
        atomic_store_explicit(&s->k, (uintptr_t)0, memory_order_relaxed);
        atomic_store_explicit(&s->v, (uintptr_t)0, memory_order_relaxed);
        atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
    }
    return g;
}

typedef struct {
    _Atomic int nmod;
    _Atomic int oz;
} gate_fast_ounce_slot;

static inline int gate_fast_ounce_read(const gate_fast_ounce_slot *s, int cur_nmod, int *out) {
    int n = atomic_load_explicit(&s->nmod, memory_order_acquire);
    if (n <= 0 || n != cur_nmod) return 0;
    int v = atomic_load_explicit(&s->oz, memory_order_relaxed);
    if (gate_fast_w26_fault() == 3) v ^= 1;
    *out = v;
    return 1;
}

static inline void gate_fast_ounce_write(gate_fast_ounce_slot *s, int cur_nmod, int oz) {
    if (cur_nmod <= 0) return;
    atomic_store_explicit(&s->oz, oz ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&s->nmod, cur_nmod, memory_order_release);
}

typedef struct {
    _Atomic uint64_t seq;
    _Atomic uint64_t chunk;
    _Atomic(void *) mod;
    _Atomic uintptr_t base;
    _Atomic uintptr_t end;
    _Atomic uintptr_t pbase;
    _Atomic uintptr_t pend;
} gate_fast_mod_slot;

typedef struct {
    _Atomic int ready;
    _Atomic uintptr_t lo;
    _Atomic uintptr_t hi;
    gate_fast_mod_slot s[GATE_FAST_MOD_WAYS];
} gate_fast_mod_idx;

static inline void gate_fast_mod_publish_span(gate_fast_mod_idx *idx, uintptr_t lo, uintptr_t hi) {
    atomic_store_explicit(&idx->lo, lo, memory_order_relaxed);
    atomic_store_explicit(&idx->hi, hi, memory_order_relaxed);
    atomic_store_explicit(&idx->ready, hi > lo ? 1 : 0, memory_order_release);
}

static inline int gate_fast_mod_has(uintptr_t a, uintptr_t b, uintptr_t e, uintptr_t pb, uintptr_t pe) {
    return (a >= b && a < e) || (pe > pb && a >= pb && a < pe);
}

static inline int gate_fast_mod_try(const gate_fast_mod_idx *idx, const void *p, void **out) {
    if (!atomic_load_explicit(&idx->ready, memory_order_acquire)) return 0;
    uintptr_t a = (uintptr_t)p;
    uintptr_t lo = atomic_load_explicit(&idx->lo, memory_order_relaxed);
    uintptr_t hi = atomic_load_explicit(&idx->hi, memory_order_relaxed);
    int f = gate_fast_w26_fault();
    if (a < lo || a >= hi || f == 4) {
        *out = NULL;
        return 1;
    }
    uint64_t ch = (uint64_t)(a >> 16);
    const gate_fast_mod_slot *s = &idx->s[(unsigned)(ch * 0x9e3779b97f4a7c15ull >> 58) & (GATE_FAST_MOD_WAYS - 1u)];
    for (int r = 0; r < GATE_FAST_RETRIES; r++) {
        uint64_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (!s1 || (s1 & 1ull)) continue;
        uint64_t sc = atomic_load_explicit(&s->chunk, memory_order_relaxed);
        void *sm = atomic_load_explicit(&s->mod, memory_order_relaxed);
        uintptr_t sb = atomic_load_explicit(&s->base, memory_order_relaxed);
        uintptr_t se = atomic_load_explicit(&s->end, memory_order_relaxed);
        uintptr_t spb = atomic_load_explicit(&s->pbase, memory_order_relaxed);
        uintptr_t spe = atomic_load_explicit(&s->pend, memory_order_relaxed);
        uint64_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (s1 == s2 && sc == ch && gate_fast_mod_has(a, sb, se, spb, spe)) {
            *out = f == 5 ? NULL : sm;
            return 1;
        }
        return 0;
    }
    return 0;
}

static inline void gate_fast_mod_write(gate_fast_mod_idx *idx, const void *p, void *m,
                                       uintptr_t b, uintptr_t e, uintptr_t pb, uintptr_t pe) {
    if (!m || !atomic_load_explicit(&idx->ready, memory_order_acquire)) return;
    uint64_t ch = (uint64_t)((uintptr_t)p >> 16);
    gate_fast_mod_slot *s = &idx->s[(unsigned)(ch * 0x9e3779b97f4a7c15ull >> 58) & (GATE_FAST_MOD_WAYS - 1u)];
    uint64_t q = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 1u) | 1ull, memory_order_release);
    atomic_store_explicit(&s->chunk, ch, memory_order_relaxed);
    atomic_store_explicit(&s->mod, m, memory_order_relaxed);
    atomic_store_explicit(&s->base, b, memory_order_relaxed);
    atomic_store_explicit(&s->end, e, memory_order_relaxed);
    atomic_store_explicit(&s->pbase, pb, memory_order_relaxed);
    atomic_store_explicit(&s->pend, pe, memory_order_relaxed);
    atomic_store_explicit(&s->seq, (q + 2u) & ~1ull, memory_order_release);
}
