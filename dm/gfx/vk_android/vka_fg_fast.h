#pragma once
#include <float.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W38_TEST_HOOKS
extern _Atomic int g_vka_fg_fast_state;
extern _Atomic int g_vka_w38_fault;
static inline void vka_fg_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_fg_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w38_set_fault(int m) {
    atomic_store_explicit(&g_vka_w38_fault, m, memory_order_release);
}
static inline int vka_w38_fault(void) {
    return atomic_load_explicit(&g_vka_w38_fault, memory_order_acquire);
}
#else
static _Atomic int g_vka_fg_fast_state;
static inline int vka_w38_fault(void) { return 0; }
#endif

static inline int vka_fg_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_fg_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_FG_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_fg_fast_state, s, memory_order_release);
    }
    return s == 2;
}

enum { VKA_FMT_CACHE_N = 64u, VKA_LSFG_BATCH_MAX = 256u };

typedef struct {
    _Atomic uint32_t state;
    _Atomic uintptr_t pd;
    _Atomic uint32_t fmt;
    _Atomic uint32_t feat;
} vka_fmt_slot;

typedef struct {
    vka_fmt_slot slots[VKA_FMT_CACHE_N];
} vka_fmt_feat_cache;

static inline uint32_t vka_fmt_feat_hash(uintptr_t pd, uint32_t fmt) {
    uint64_t a = (uint64_t)(pd >> 3) * 11400714819323198485ull;
    uint64_t b = (uint64_t)fmt * 11400714819323198485ull;
    uint64_t x = a ^ b;
    return (uint32_t)((x ^ (x >> 32)) & (VKA_FMT_CACHE_N - 1u));
}

static inline int vka_fmt_feat_get(
    const vka_fmt_feat_cache *c, uintptr_t pd, uint32_t fmt, uint32_t *out) {
    if (!c || !pd || !out) return 0;
    uint32_t h = vka_fmt_feat_hash(pd, fmt);
    for (unsigned step = 0; step < 8u; step++) {
        const vka_fmt_slot *s = &c->slots[(h + step) & (VKA_FMT_CACHE_N - 1u)];
        if (atomic_load_explicit(&s->state, memory_order_acquire) != 2u) continue;
        if (atomic_load_explicit(&s->pd, memory_order_relaxed) == pd &&
            atomic_load_explicit(&s->fmt, memory_order_relaxed) == fmt) {
            uint32_t f = atomic_load_explicit(&s->feat, memory_order_relaxed);
            if (vka_w38_fault() == 1) f ^= 1u;
            *out = f;
            return 1;
        }
    }
    return 0;
}

static inline void vka_fmt_feat_put(
    vka_fmt_feat_cache *c, uintptr_t pd, uint32_t fmt, uint32_t feat) {
    if (!c || !pd) return;
    uint32_t h = vka_fmt_feat_hash(pd, fmt);
    for (unsigned step = 0; step < 8u; step++) {
        vka_fmt_slot *s = &c->slots[(h + step) & (VKA_FMT_CACHE_N - 1u)];
        uint32_t exp = 0u;
        if (atomic_compare_exchange_strong_explicit(
                &s->state, &exp, 1u, memory_order_acq_rel, memory_order_acquire)) {
            atomic_store_explicit(&s->pd, pd, memory_order_relaxed);
            atomic_store_explicit(&s->fmt, fmt, memory_order_relaxed);
            atomic_store_explicit(&s->feat, feat, memory_order_relaxed);
            atomic_store_explicit(&s->state, 2u, memory_order_release);
            return;
        }
        if (exp == 2u && atomic_load_explicit(&s->pd, memory_order_relaxed) == pd &&
            atomic_load_explicit(&s->fmt, memory_order_relaxed) == fmt) {
            return;
        }
    }
}

typedef struct {
    uint32_t gen;
    int settled;
    int last_fstate;
} vka_fg_off_tracker;

static inline int vka_fg_off_is_settled(
    const vka_fg_off_tracker *t, uint32_t gen, int has_chain, int fstate) {
    if (!t || has_chain || vka_w38_fault() == 5) return 0;
    return t->settled && t->gen == gen && t->last_fstate == fstate;
}

static inline void vka_fg_off_mark_settled(
    vka_fg_off_tracker *t, uint32_t gen, int fstate) {
    if (!t) return;
    t->gen = gen;
    t->last_fstate = fstate;
    t->settled = 1;
}

static inline void vka_fg_off_mark_active(vka_fg_off_tracker *t) {
    if (t) t->settled = 0;
}

typedef struct {
    uint32_t gen;
    int inited;
    int min_fps;
    int refresh_cap;
    int dump_n;
    int own_fmt;
} vka_fg_env_cache;

typedef struct {
    uint8_t n_total;
    uint8_t n_img;
    uint8_t n_ubo;
    uint8_t img_slot[16];
    uint8_t ubo_slot[4];
    uint8_t ubo_which[4];
    uint32_t img_is_tex_mask;
} vka_fsr3_pass_meta;

static inline void vka_fsr3_compile_kinds(
    vka_fsr3_pass_meta *m, int is_rcas, const char *kinds) {
    if (!m || !kinds) return;
    memset(m, 0, sizeof *m);
    for (uint32_t n = 0; n < 16u && kinds[n]; n++) {
        if (kinds[n] == 'U') {
            if (m->n_ubo < 4u) {
                uint8_t k = m->n_ubo++;
                m->ubo_slot[k] = (uint8_t)n;
                m->ubo_which[k] = (k == 0u) ? 0u : (is_rcas ? 2u : 1u);
            }
        } else {
            uint8_t k = m->n_img++;
            m->img_slot[k] = (uint8_t)n;
            if (kinds[n] == 'T') m->img_is_tex_mask |= (1u << k);
        }
        m->n_total++;
    }
    if (vka_w38_fault() == 2) m->img_is_tex_mask ^= 1u;
}

typedef struct {
    uint32_t env_gen;
    int env_valid;
    int out_w;
    int out_h;
    int fsr3_reset;
    float jx;
    float jy;
    float mx;
    float my;
    float sharp;
    uint32_t rw;
    uint32_t rh;
    uint32_t sgx;
    uint32_t sgy;
    uint32_t mips;
    uint8_t geom_valid;
    float dev_to_view[4];
    float tan_half_fov;
    uint32_t sharp_bits;
    uint32_t rcas0;
    uint32_t rcas1;
    int sharpen;
    uint8_t sharp_valid;
} vka_fsr3_fast_memo;

static inline uint16_t vka_f32_to_f16_bits(float f) {
    uint32_t u = 0;
    memcpy(&u, &f, 4);
    uint16_t sign = (uint16_t)((u >> 16) & 0x8000u);
    uint32_t exp = (u >> 23) & 0xffu;
    uint32_t mant = u & 0x7fffffu;
    if (exp == 0xffu) return (uint16_t)(sign | (mant ? 0x7e00u : 0x7c00u));
    int32_t he = (int32_t)exp - 127 + 15;
    if (he >= 31) return (uint16_t)(sign | 0x7c00u);
    if (he <= 0) {
        if (he < -10) return sign;
        mant |= 0x800000u;
        uint32_t sh = (uint32_t)(14 - he);
        uint32_t hb = mant >> sh;
        uint32_t rem = mant & ((1u << sh) - 1u);
        uint32_t half = 1u << (sh - 1u);
        if (rem > half || (rem == half && (hb & 1u))) hb++;
        return (uint16_t)(sign | (hb & 0x7fffu));
    }
    uint32_t hb = ((uint32_t)he << 10) | (mant >> 13);
    uint32_t rem = mant & 0x1fffu;
    if (rem > 0x1000u || (rem == 0x1000u && (hb & 1u))) hb++;
    return (uint16_t)(sign | (hb & 0x7fffu));
}

static inline void vka_fsr3_geom_sync(
    vka_fsr3_fast_memo *m, uint32_t rw, uint32_t rh) {
    if (!m || !rw || !rh) return;
    if (m->geom_valid && m->rw == rw && m->rh == rh && vka_w38_fault() != 3) return;
    const float near_plane = 0.1f;
    const float fov = 60.f * 3.14159265f / 180.f;
    const float aspect = (float)rw / (float)rh;
    const float cot = cosf(0.5f * fov) / sinf(0.5f * fov);
    m->dev_to_view[0] = -1.f * (0.f + FLT_EPSILON);
    m->dev_to_view[1] = near_plane;
    m->dev_to_view[2] = 1.f / (cot / aspect);
    m->dev_to_view[3] = 1.f / cot;
    m->tan_half_fov = tanf(atanf(tanf(fov * 0.5f) * aspect));
    m->sgx = (rw - 1u) / 64u + 1u;
    m->sgy = (rh - 1u) / 64u + 1u;
    uint32_t mx = rw > rh ? rw : rh;
    uint32_t mips = mx >= 2u ? (31u - (unsigned)__builtin_clz(mx)) : 0u;
    if (vka_w38_fault() == 3) mips ^= 1u;
    m->mips = mips > 12u ? 12u : mips;
    m->rw = rw;
    m->rh = rh;
    m->geom_valid = 1;
}

static inline void vka_fsr3_rcas_sync(vka_fsr3_fast_memo *m, float sharp) {
    if (!m) return;
    uint32_t sb = 0;
    memcpy(&sb, &sharp, 4);
    if (m->sharp_valid && m->sharp_bits == sb && vka_w38_fault() != 3) return;
    float cl = sharp < 0.f ? 0.f : (sharp > 1.f ? 1.f : sharp);
    float stops = 2.f - 2.f * cl;
    float sx = exp2f(-stops);
    uint32_t sx_u = 0;
    memcpy(&sx_u, &sx, 4);
    uint16_t h16 = vka_f32_to_f16_bits(sx);
    if (vka_w38_fault() == 3) sx_u ^= 1u;
    m->rcas0 = sx_u;
    m->rcas1 = (uint32_t)h16 | ((uint32_t)h16 << 16);
    m->sharpen = sharp > 0.f;
    m->sharp_bits = sb;
    m->sharp_valid = 1;
}

typedef struct {
    uint32_t used;
    uint32_t flushes;
    uint32_t emitted;
} vka_lsfg_batch_ctl;

static inline int vka_lsfg_batch_step(vka_lsfg_batch_ctl *c, uint32_t count) {
    if (!c || !count || count > VKA_LSFG_BATCH_MAX) return 0;
    int need_flush = (c->used + count > VKA_LSFG_BATCH_MAX);
    if (need_flush) {
        c->emitted += (vka_w38_fault() == 4 && c->used) ? (c->used - 1u) : c->used;
        c->flushes++;
        c->used = 0;
    }
    c->used += count;
    return need_flush;
}

static inline void vka_lsfg_batch_finish(vka_lsfg_batch_ctl *c) {
    if (!c || !c->used) return;
    c->emitted += (vka_w38_fault() == 4) ? (c->used - 1u) : c->used;
    c->flushes++;
    c->used = 0;
}
