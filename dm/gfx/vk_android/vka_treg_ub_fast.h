// Fast O(1) treg serial/multi-image index, 64-bin UB directory & mesh prepass map (Opt 56).
#ifndef VKA_TREG_UB_FAST_H
#define VKA_TREG_UB_FAST_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W54_TEST_HOOKS
extern _Atomic int g_vka_treg_ub_fast_override;
extern _Atomic int g_aoi_w54_fault;
static inline void vka_treg_ub_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_treg_ub_fast_override, v, memory_order_release);
}
static inline void aoi_w54_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w54_fault, f, memory_order_release);
}
static inline int aoi_w54_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w54_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W54_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w54_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w54_fault_mode(void) { return 0; }
#endif

static inline int vka_treg_ub_fast_on(void) {
#ifdef T_W54_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_treg_ub_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_TREG_UB_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_tu_mix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

/* 1. Intrusive O(1) (serial -> texture IDs) reverse index + dense active IDs + multi-serial image set. */
enum { VKA_TREG_BUCKETS = 4096u, VKA_TREG_BMASK = 4095u, VKA_MULTI_SLOTS = 512u, VKA_MULTI_MASK = 511u };
typedef struct {
    int32_t head[VKA_TREG_BUCKETS];
    int32_t *prev, *next, *act_ids, *act_pos;
    uint64_t *ser;
    int cap, n_act, inited;
    uintptr_t multi_im[VKA_MULTI_SLOTS];
    uint8_t multi_st[VKA_MULTI_SLOTS];
    int n_multi, n_multi_tomb, multi_ovf;
} vka_treg_sidx_t;

static inline void vka_treg_sidx_free(vka_treg_sidx_t *x) {
    if (!x) return;
    free(x->prev); free(x->next); free(x->act_ids); free(x->act_pos); free(x->ser);
    memset(x, 0, sizeof *x);
}

static inline int vka_treg_sidx_grow(vka_treg_sidx_t *x, int id) {
    int nc = x->cap ? x->cap : 1024;
    while (nc <= id) nc *= 2;
    int32_t *np = (int32_t *)realloc(x->prev, (size_t)nc * sizeof *np);
    if (!np) return 0;
    x->prev = np;
    int32_t *nn = (int32_t *)realloc(x->next, (size_t)nc * sizeof *nn);
    if (!nn) return 0;
    x->next = nn;
    int32_t *na = (int32_t *)realloc(x->act_ids, (size_t)nc * sizeof *na);
    if (!na) return 0;
    x->act_ids = na;
    int32_t *nap = (int32_t *)realloc(x->act_pos, (size_t)nc * sizeof *nap);
    if (!nap) return 0;
    x->act_pos = nap;
    uint64_t *ns = (uint64_t *)realloc(x->ser, (size_t)nc * sizeof *ns);
    if (!ns) return 0;
    x->ser = ns;
    if (!x->inited) {
        for (uint32_t b = 0u; b < VKA_TREG_BUCKETS; b++) x->head[b] = -1;
        x->inited = 1;
    }
    for (int i = x->cap; i < nc; i++) {
        x->prev[i] = -1; x->next[i] = -1; x->act_ids[i] = 0; x->act_pos[i] = -1; x->ser[i] = 0u;
    }
    x->cap = nc;
    return 1;
}

static inline void vka_treg_sidx_act_add(vka_treg_sidx_t *x, int id) {
    if (x->act_pos[id] >= 0) return;
    x->act_pos[id] = x->n_act;
    x->act_ids[x->n_act++] = id;
}

static inline void vka_treg_sidx_act_del(vka_treg_sidx_t *x, int id) {
    int pos = x->act_pos[id];
    if (pos < 0) return;
    int last = x->act_ids[--x->n_act];
    if (pos < x->n_act) {
        x->act_ids[pos] = last;
        x->act_pos[last] = pos;
    }
    x->act_pos[id] = -1;
}

static inline void vka_treg_sidx_unlink(vka_treg_sidx_t *x, int id) {
    uint64_t old = x->ser[id];
    if (!old) return;
    uint32_t b = (uint32_t)vka_tu_mix64(old) & VKA_TREG_BMASK;
    int32_t pv = x->prev[id], nx = x->next[id];
    if (pv >= 0) x->next[pv] = nx;
    else if (x->head[b] == id) x->head[b] = nx;
    if (nx >= 0) x->prev[nx] = pv;
    x->prev[id] = -1;
    x->next[id] = -1;
}

static inline void vka_treg_sidx_set(vka_treg_sidx_t *x, int id, uint64_t serial) {
    if (!x || id <= 0) return;
    if (x->inited && id < x->cap && x->ser[id] == serial) return;
    if (!x->inited || id >= x->cap) {
        if (!vka_treg_sidx_grow(x, id)) return;
    }
    if (x->ser[id]) vka_treg_sidx_unlink(x, id);
    x->ser[id] = serial;
    if (serial) {
        vka_treg_sidx_act_add(x, id);
        uint32_t b = (uint32_t)vka_tu_mix64(serial) & VKA_TREG_BMASK;
        int32_t oldh = x->head[b];
        x->next[id] = oldh;
        x->prev[id] = -1;
        if (oldh >= 0) x->prev[oldh] = id;
        x->head[b] = id;
    } else {
        vka_treg_sidx_act_del(x, id);
    }
}

static inline int vka_treg_sidx_first(const vka_treg_sidx_t *x, uint64_t serial) {
    if (!x || !x->inited || !serial) return -1;
    uint32_t b = (uint32_t)vka_tu_mix64(serial) & VKA_TREG_BMASK;
    if (aoi_w54_fault_mode() == 1) b ^= 1u;
    for (int id = x->head[b], st = 0; id > 0 && st <= x->n_act; id = x->next[id], st++) {
        if (x->ser[id] == serial) return id;
    }
    return -1;
}

static inline int vka_treg_sidx_next(const vka_treg_sidx_t *x, int id, uint64_t serial) {
    if (!x || !x->inited || id <= 0 || id >= x->cap || !serial) return -1;
    for (int cur = x->next[id], st = 0; cur > 0 && st <= x->n_act; cur = x->next[cur], st++) {
        if (x->ser[cur] == serial) return cur;
    }
    return -1;
}

static inline void vka_treg_multi_rehash(vka_treg_sidx_t *x) {
    uintptr_t live[VKA_MULTI_SLOTS];
    int nl = 0;
    for (uint32_t i = 0u; i < VKA_MULTI_SLOTS; i++) {
        if (x->multi_st[i] == 1u && x->multi_im[i]) live[nl++] = x->multi_im[i];
    }
    memset(x->multi_st, 0, sizeof x->multi_st);
    x->n_multi = 0;
    x->n_multi_tomb = 0;
    for (int i = 0; i < nl; i++) {
        uint32_t s = (uint32_t)vka_tu_mix64((uint64_t)live[i]) & VKA_MULTI_MASK;
        for (uint32_t p = 0u; p < VKA_MULTI_SLOTS; p++, s = (s + 1u) & VKA_MULTI_MASK) {
            if (!x->multi_st[s]) {
                x->multi_im[s] = live[i];
                x->multi_st[s] = 1u;
                x->n_multi++;
                break;
            }
        }
    }
}

static inline void vka_treg_sidx_mark_multi(vka_treg_sidx_t *x, uintptr_t im) {
    if (!x || !im || x->multi_ovf) return;
    if (x->n_multi + x->n_multi_tomb >= 384) {
        if (x->n_multi < 320) vka_treg_multi_rehash(x);
        else { x->multi_ovf = 1; return; }
    }
    uint32_t s = (uint32_t)vka_tu_mix64((uint64_t)im) & VKA_MULTI_MASK, tomb = VKA_MULTI_SLOTS;
    for (uint32_t p = 0u; p < VKA_MULTI_SLOTS; p++, s = (s + 1u) & VKA_MULTI_MASK) {
        uint8_t st = x->multi_st[s];
        if (st == 1u) { if (x->multi_im[s] == im) return; continue; }
        if (st == 2u) { if (tomb == VKA_MULTI_SLOTS) tomb = s; continue; }
        uint32_t dst = tomb < VKA_MULTI_SLOTS ? tomb : s;
        if (x->multi_st[dst] == 2u) x->n_multi_tomb--;
        x->multi_im[dst] = im; x->multi_st[dst] = 1u; x->n_multi++;
        return;
    }
    x->multi_ovf = 1;
}

static inline void vka_treg_sidx_unmark_multi(vka_treg_sidx_t *x, uintptr_t im) {
    if (!x || !im || !x->n_multi) return;
    uint32_t s = (uint32_t)vka_tu_mix64((uint64_t)im) & VKA_MULTI_MASK;
    for (uint32_t p = 0u; p < VKA_MULTI_SLOTS; p++, s = (s + 1u) & VKA_MULTI_MASK) {
        uint8_t st = x->multi_st[s];
        if (!st) return;
        if (st == 1u && x->multi_im[s] == im) {
            x->multi_st[s] = 2u; x->n_multi--; x->n_multi_tomb++;
            return;
        }
    }
}

static inline int vka_treg_sidx_is_multi(const vka_treg_sidx_t *x, uintptr_t im) {
    if (aoi_w54_fault_mode() == 2) return 0;
    if (!x || !im || x->multi_ovf) return 1;
    if (!x->n_multi) return 0;
    uint32_t s = (uint32_t)vka_tu_mix64((uint64_t)im) & VKA_MULTI_MASK;
    for (uint32_t p = 0u; p < VKA_MULTI_SLOTS; p++, s = (s + 1u) & VKA_MULTI_MASK) {
        uint8_t st = x->multi_st[s];
        if (!st) return 0;
        if (st == 1u && x->multi_im[s] == im) return 1;
    }
    return 0;
}

/* 2. 64-bin x 256-bit 4 KiB-page spatial directory over deferred UBO updates (UB_MAX = 256). */
typedef struct {
    uint64_t bins[64][4];
    uint64_t active_bins;
} vka_ub_dir_t;

static inline uint64_t vka_ub_dir_page_mask(uint64_t lo, uint64_t hi) {
    if (hi <= lo) return 0u;
    uint64_t p0 = lo >> 12u, p1 = (hi - 1u) >> 12u;
    if (p1 - p0 >= 63u) return ~UINT64_C(0);
    unsigned b0 = (unsigned)(p0 & 63u), cnt = (unsigned)(p1 - p0 + 1u);
    if (b0 + cnt <= 64u) {
        uint64_t m = cnt == 64u ? ~UINT64_C(0) : ((UINT64_C(1) << cnt) - 1u);
        return m << b0;
    }
    unsigned hi_bits = b0 + cnt - 64u;
    return (~UINT64_C(0) << b0) | ((UINT64_C(1) << hi_bits) - 1u);
}

static inline void vka_ub_dir_clear(vka_ub_dir_t *d) {
    uint64_t ab = d->active_bins;
    while (ab) {
        unsigned b = (unsigned)__builtin_ctzll(ab);
        ab &= ab - 1u;
        d->bins[b][0] = 0u; d->bins[b][1] = 0u; d->bins[b][2] = 0u; d->bins[b][3] = 0u;
    }
    d->active_bins = 0u;
}

static inline void vka_ub_dir_add(vka_ub_dir_t *d, int slot, uint64_t lo, uint64_t hi) {
    if (slot < 0 || slot >= 256 || hi <= lo) return;
    uint64_t pm = vka_ub_dir_page_mask(lo, hi);
    unsigned w = (unsigned)slot >> 6u;
    uint64_t bit = UINT64_C(1) << ((unsigned)slot & 63u);
    d->active_bins |= pm;
    while (pm) {
        unsigned b = (unsigned)__builtin_ctzll(pm);
        pm &= pm - 1u;
        d->bins[b][w] |= bit;
    }
}

static inline int vka_ub_dir_cand(const vka_ub_dir_t *d, uint64_t lo, uint64_t hi, uint64_t out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0u;
    uint64_t bm = d->active_bins & vka_ub_dir_page_mask(lo, hi);
    while (bm) {
        unsigned b = (unsigned)__builtin_ctzll(bm);
        bm &= bm - 1u;
        out[0] |= d->bins[b][0]; out[1] |= d->bins[b][1];
        out[2] |= d->bins[b][2]; out[3] |= d->bins[b][3];
    }
    if (aoi_w54_fault_mode() == 3) out[1] = 0u;
    return (out[0] | out[1] | out[2] | out[3]) != 0u;
}

/* 3. O(1) 256-slot mesh prepass cut key and pair hash map (n <= 64). */
enum { VKA_PP_CUT_SLOTS = 256u, VKA_PP_CUT_MASK = 255u, VKA_PP_CUT_MAX = 64u };
typedef struct {
    uint64_t k_key[VKA_PP_CUT_SLOTS];
    uint8_t k_row1[VKA_PP_CUT_SLOTS];
    uint8_t k_role[VKA_PP_CUT_SLOTS];
    uint64_t p_pre[VKA_PP_CUT_SLOTS], p_col[VKA_PP_CUT_SLOTS];
    uint8_t p_used[VKA_PP_CUT_SLOTS];
    uint8_t zero_row1, zero_role;
    int built, n_rows;
} vka_pp_cut_map_t;

static inline void vka_pp_cut_key_put(vka_pp_cut_map_t *m, uint64_t key, uint8_t row1, uint8_t role) {
    if (!key) {
        if (!m->zero_row1 || aoi_w54_fault_mode() == 4) { m->zero_row1 = row1; m->zero_role = role; }
        return;
    }
    uint32_t s = (uint32_t)vka_tu_mix64(key) & VKA_PP_CUT_MASK;
    for (uint32_t p = 0u; p < VKA_PP_CUT_SLOTS; p++, s = (s + 1u) & VKA_PP_CUT_MASK) {
        if (!m->k_row1[s]) { m->k_key[s] = key; m->k_row1[s] = row1; m->k_role[s] = role; return; }
        if (m->k_key[s] == key) {
            if (aoi_w54_fault_mode() == 4) { m->k_row1[s] = row1; m->k_role[s] = role; }
            return;
        }
    }
}

static inline void vka_pp_cut_pair_put(vka_pp_cut_map_t *m, uint64_t prepass, uint64_t colour) {
    uint64_t h = vka_tu_mix64(prepass) ^ vka_tu_mix64(colour + UINT64_C(0x9e3779b97f4a7c15));
    uint32_t s = (uint32_t)h & VKA_PP_CUT_MASK;
    for (uint32_t p = 0u; p < VKA_PP_CUT_SLOTS; p++, s = (s + 1u) & VKA_PP_CUT_MASK) {
        if (!m->p_used[s] || (m->p_pre[s] == prepass && m->p_col[s] == colour)) {
            m->p_pre[s] = prepass; m->p_col[s] = colour; m->p_used[s] = 1u; return;
        }
    }
}

static inline void vka_pp_cut_build(vka_pp_cut_map_t *m, const uint64_t (*cut)[4], int n) {
    memset(m, 0, sizeof *m);
    int lim = n < 0 ? 0 : (n > (int)VKA_PP_CUT_MAX ? (int)VKA_PP_CUT_MAX : n);
    m->n_rows = lim;
    for (int i = 0; i < lim; i++) {
        vka_pp_cut_pair_put(m, cut[i][0], cut[i][1]);
        vka_pp_cut_key_put(m, cut[i][1], (uint8_t)(i + 1), 1u);
        vka_pp_cut_key_put(m, cut[i][0], (uint8_t)(i + 1), 0u);
    }
    m->built = 1;
}

static inline int vka_pp_cut_first_key(const vka_pp_cut_map_t *m, uint64_t key, int *role_out) {
    if (!m->built || !m->n_rows) return -1;
    if (!key) {
        if (!m->zero_row1) return -1;
        *role_out = (int)m->zero_role;
        return (int)m->zero_row1 - 1;
    }
    uint32_t s = (uint32_t)vka_tu_mix64(key) & VKA_PP_CUT_MASK;
    for (uint32_t p = 0u; p < VKA_PP_CUT_SLOTS; p++, s = (s + 1u) & VKA_PP_CUT_MASK) {
        if (!m->k_row1[s]) return -1;
        if (m->k_key[s] == key) { *role_out = (int)m->k_role[s]; return (int)m->k_row1[s] - 1; }
    }
    return -1;
}

static inline int vka_pp_cut_has_pair(const vka_pp_cut_map_t *m, uint64_t prepass, uint64_t colour) {
    if (aoi_w54_fault_mode() == 5) return 0;
    if (!m->built || !m->n_rows) return 0;
    uint64_t h = vka_tu_mix64(prepass) ^ vka_tu_mix64(colour + UINT64_C(0x9e3779b97f4a7c15));
    uint32_t s = (uint32_t)h & VKA_PP_CUT_MASK;
    for (uint32_t p = 0u; p < VKA_PP_CUT_SLOTS; p++, s = (s + 1u) & VKA_PP_CUT_MASK) {
        if (!m->p_used[s]) return 0;
        if (m->p_pre[s] == prepass && m->p_col[s] == colour) return 1;
    }
    return 0;
}
#endif
