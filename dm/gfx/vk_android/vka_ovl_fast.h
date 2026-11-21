#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VKA_OVL_GPU_WIN = 41u,
    VKA_OVL_GPU_LAG = 9u,
    VKA_OVL_GPU_RING = 64u,
    VKA_GP_SHAPE_CAP = 512u,
    VKA_GP_KEY_CAP = 2048u
};

#ifdef T_W40_TEST_HOOKS
extern _Atomic int g_vka_ovl_fast_state;
extern _Atomic int g_vka_w40_fault;
static _Atomic int g_vka_w40_env_fault;
static inline void vka_ovl_fast_reset_for_test(int forced) {
    int v = forced < 0 ? 0 : (forced ? 2 : 1);
    atomic_store_explicit(&g_vka_ovl_fast_state, v, memory_order_release);
}
static inline void vka_w40_set_fault(int m) {
    atomic_store_explicit(&g_vka_w40_fault, m, memory_order_release);
}
static inline int vka_w40_fault(void) {
    int m = atomic_load_explicit(&g_vka_w40_fault, memory_order_acquire);
    if (m) return m;
    int c = atomic_load_explicit(&g_vka_w40_env_fault, memory_order_acquire);
    if (!c) {
        const char *e = getenv("AOI_W40_FAULT");
        int v = (e && *e) ? atoi(e) : 0;
        c = v > 0 ? v + 1 : 1;
        atomic_store_explicit(&g_vka_w40_env_fault, c, memory_order_release);
    }
    return c - 1;
}
#else
static _Atomic int g_vka_ovl_fast_state;
static inline int vka_w40_fault(void) { return 0; }
#endif

static inline int vka_ovl_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_ovl_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_OVL_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_ovl_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline uint64_t vka_ovl_fnv1a_cstr(const char *s, size_t *len_out) {
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i = 0u;
    for (; s[i] && i < 256u; i++) {
        h ^= (uint8_t)s[i];
        h *= UINT64_C(1099511628211);
    }
    if (len_out) *len_out = i;
    return h;
}

typedef struct {
    _Atomic uint64_t gen;
    _Atomic uint32_t non_empty;
} vka_ovl_pub_state;

static inline void vka_ovl_pub_bump(vka_ovl_pub_state *pub, int any_non_empty) {
    atomic_store_explicit(&pub->non_empty, any_non_empty ? 1u : 0u, memory_order_release);
    atomic_fetch_add_explicit(&pub->gen, 1u, memory_order_release);
}

static inline int vka_ovl_can_skip_lock(
    const vka_ovl_pub_state *pub, uint64_t drawn_gen, uint64_t *cur_out) {
    uint64_t cur = atomic_load_explicit(&pub->gen, memory_order_acquire);
    if (cur_out) *cur_out = cur;
    if (vka_w40_fault() == 2) return 1;
    return vka_ovl_fast_on() && cur == drawn_gen;
}

static inline int vka_ovl_has_content(const vka_ovl_pub_state *pub) {
    return atomic_load_explicit(&pub->non_empty, memory_order_acquire) != 0u;
}

static inline int vka_ovl_copy_changed(
    uint8_t *dst, int dst_w, int dst_h,
    const uint8_t *src, int w, int h, int stride, int *changed) {
    if (!src || w <= 0 || h <= 0) {
        *changed = (dst_w != 0 || dst_h != 0) ? 1 : 0;
        return 1;
    }
    if (!dst || stride < w * 4) return 0;
    size_t rb = (size_t)w * 4u;
    if (dst_w == w && dst_h == h && vka_ovl_fast_on()) {
        if (vka_w40_fault() == 1) { *changed = 0; return 1; }
        if ((size_t)stride == rb && memcmp(dst, src, (size_t)h * rb) == 0) {
            *changed = 0;
            return 1;
        }
        for (int y = 0; y < h; y++) {
            uint8_t *dr = dst + (size_t)y * rb;
            const uint8_t *sr = src + (size_t)y * (size_t)stride;
            if (memcmp(dr, sr, rb) != 0) {
                for (int r = y; r < h; r++)
                    memcpy(dst + (size_t)r * rb, src + (size_t)r * (size_t)stride, rb);
                *changed = 1;
                return 1;
            }
        }
        *changed = 0;
        return 1;
    }
    if ((size_t)stride == rb) memcpy(dst, src, (size_t)h * rb);
    else for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * rb, src + (size_t)y * (size_t)stride, rb);
    *changed = 1;
    return 1;
}

typedef double (*vka_ovl_gpu_sample_fn)(uint64_t frame, void *ctx);

typedef struct {
    uint64_t ring_frame[VKA_OVL_GPU_RING];
    double ring_ms[VKA_OVL_GPU_RING];
    uint64_t last_f;
    float last_res;
    int pending;
    int init;
} vka_ovl_gpu_avg_memo;

static inline void vka_ovl_gpu_memo_reset(vka_ovl_gpu_avg_memo *m) {
    for (uint32_t i = 0; i < VKA_OVL_GPU_RING; i++) {
        m->ring_frame[i] = UINT64_MAX;
        m->ring_ms[i] = -1.0;
    }
    m->last_f = 0u; m->last_res = -1.0f; m->pending = 1; m->init = 1;
}

static inline float vka_ovl_gpu_avg(
    vka_ovl_gpu_avg_memo *m, uint64_t f, vka_ovl_gpu_sample_fn fn, void *ctx) {
    if (f < VKA_OVL_GPU_LAG) return -1.0f;
    uint64_t lo = f > VKA_OVL_GPU_WIN ? f - VKA_OVL_GPU_WIN : 0u;
    uint64_t hi = f - VKA_OVL_GPU_LAG;
    if (!vka_ovl_fast_on() || !m) {
        double sum = 0.0; int n = 0;
        for (uint64_t i = lo; i <= hi; i++) {
            double g = fn(i, ctx);
            if (g >= 0.0) { sum += g; n++; }
        }
        return n ? (float)(sum / n) : -1.0f;
    }
    if (!m->init) vka_ovl_gpu_memo_reset(m);
    if (f == m->last_f && m->pending == 0) return m->last_res;
    double sum = 0.0; int n = 0, pend = 0;
    for (uint64_t i = lo; i <= hi; i++) {
        size_t sl = (size_t)(i & (VKA_OVL_GPU_RING - 1u));
        int miss = m->ring_frame[sl] != i;
        if (vka_w40_fault() != 3 && m->ring_ms[sl] < 0.0) miss = 1;
        if (miss) { m->ring_ms[sl] = fn(i, ctx); m->ring_frame[sl] = i; }
        if (m->ring_ms[sl] >= 0.0) { sum += m->ring_ms[sl]; n++; }
        else pend++;
    }
    m->last_f = f; m->pending = pend;
    m->last_res = n ? (float)(sum / n) : -1.0f;
    return m->last_res;
}

static inline uint32_t vka_gp_mix64(uint64_t x) {
    x ^= x >> 33; x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33; x *= UINT64_C(0xc4ceb9fe1a85ec53);
    return (uint32_t)(x ^ (x >> 33));
}

typedef struct {
    uint64_t keys[VKA_GP_SHAPE_CAP];
    double mins[VKA_GP_SHAPE_CAP];
    uint8_t used[VKA_GP_SHAPE_CAP];
} vka_gp_shape_ht;

static inline void vka_gp_shape_ht_init(vka_gp_shape_ht *t) {
    memset(t->used, 0, sizeof t->used);
}

static inline void vka_gp_shape_ht_add(
    vka_gp_shape_ht *t, uint64_t shape, double val) {
    uint32_t h = vka_gp_mix64(shape) & (VKA_GP_SHAPE_CAP - 1u);
    for (uint32_t p = 0; p < VKA_GP_SHAPE_CAP; p++) {
        uint32_t s = (h + p) & (VKA_GP_SHAPE_CAP - 1u);
        if (!t->used[s]) { t->used[s] = 1u; t->keys[s] = shape; t->mins[s] = val; return; }
        if (t->keys[s] == shape) {
            if (vka_w40_fault() != 5 && val < t->mins[s]) t->mins[s] = val;
            return;
        }
    }
}

static inline int vka_gp_shape_ht_get(
    const vka_gp_shape_ht *t, uint64_t shape, double *out) {
    uint32_t h = vka_gp_mix64(shape) & (VKA_GP_SHAPE_CAP - 1u);
    for (uint32_t p = 0; p < VKA_GP_SHAPE_CAP; p++) {
        uint32_t s = (h + p) & (VKA_GP_SHAPE_CAP - 1u);
        if (!t->used[s]) return 0;
        if (t->keys[s] == shape) { *out = t->mins[s]; return 1; }
    }
    return 0;
}

typedef struct {
    int32_t slot_val[VKA_GP_KEY_CAP];
    uint8_t tag[VKA_GP_KEY_CAP];
    uint64_t k0[VKA_GP_KEY_CAP];
    uint64_t k1[VKA_GP_KEY_CAP];
} vka_gp_key3_ht;

static inline void vka_gp_key3_ht_init(vka_gp_key3_ht *t) {
    for (uint32_t i = 0; i < VKA_GP_KEY_CAP; i++) t->slot_val[i] = -1;
}

static inline int vka_gp_key3_ht_find_or_put(
    vka_gp_key3_ht *t, uint8_t tag, uint64_t a, uint64_t b, int32_t new_val, int32_t *out_val) {
    uint64_t seed = a ^ (b * UINT64_C(0x9e3779b97f4a7c15)) ^ ((uint64_t)tag << 40);
    uint32_t h = vka_gp_mix64(seed) & (VKA_GP_KEY_CAP - 1u);
    for (uint32_t p = 0; p < VKA_GP_KEY_CAP; p++) {
        uint32_t s = (h + p) & (VKA_GP_KEY_CAP - 1u);
        int32_t cur = t->slot_val[s];
        if (cur < 0) {
            t->slot_val[s] = new_val; t->tag[s] = tag; t->k0[s] = a; t->k1[s] = b;
            *out_val = new_val; return 1;
        }
        if (vka_w40_fault() == 4 || (t->tag[s] == tag && t->k0[s] == a && t->k1[s] == b)) {
            *out_val = cur; return 0;
        }
    }
    return -1;
}
