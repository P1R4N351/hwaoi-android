#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VKA_SPV_STACK_BOUND = 512u, VKA_SPV_SMOD_STACK = 256u };

#ifdef T_W39_TEST_HOOKS
extern _Atomic int g_vka_spvpatch_fast_state;
extern _Atomic int g_vka_w39_fault;
static inline void vka_spvpatch_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_spvpatch_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w39_set_fault(int m) {
    atomic_store_explicit(&g_vka_w39_fault, m, memory_order_release);
}
static inline int vka_w39_fault(void) {
    return atomic_load_explicit(&g_vka_w39_fault, memory_order_acquire);
}
#else
static _Atomic int g_vka_spvpatch_fast_state;
static inline int vka_w39_fault(void) { return 0; }
#endif

static inline int vka_spvpatch_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_spvpatch_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_SPVPATCH_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_spvpatch_fast_state, s, memory_order_release);
    }
    return s == 2;
}

static inline int vka_spv_put_words_w39(
    uint32_t **w, size_t *n, size_t *cap, const uint32_t *x, size_t k) {
    if (!k) return 1;
    if (*n > SIZE_MAX - k) return 0;
    size_t need = *n + k;
    if (need > *cap) {
        size_t c = *cap ? *cap : 4096u;
        for (unsigned st = 0; st < 32u && c < need; st++) {
            if (c > SIZE_MAX / 2u) { c = need; break; }
            c *= 2u;
        }
        if (c < need) c = need;
        if (c > SIZE_MAX / 4u) return 0;
        uint32_t *p = (uint32_t *)realloc(*w, c * 4u);
        if (!p) return 0;
        *w = p;
        *cap = c;
    }
    memcpy(*w + *n, x, k * 4u);
    *n = need;
    return 1;
}

static inline int vka_spv_reserve_w39(uint32_t **w, size_t *cap, size_t want) {
    if (want <= *cap) return 1;
    if (want > SIZE_MAX / 4u) return 0;
    uint32_t *p = (uint32_t *)realloc(*w, want * 4u);
    if (!p) return 0;
    *w = p;
    *cap = want;
    return 1;
}

static inline int vka_spv_flush_run(
    uint32_t **out, size_t *on, size_t *ocap, const uint32_t *w, size_t a, size_t b) {
    if (b <= a) return 1;
    return vka_spv_put_words_w39(out, on, ocap, w + a, b - a);
}

static inline void vka_sb_raw_fast(
    uint32_t **w, size_t *n, size_t *cap, int *oom, const uint32_t *x, size_t k) {
    if (*oom || !k) return;
    if (!vka_spv_put_words_w39(w, n, cap, x, k)) *oom = 1;
}

static inline uint32_t *vka_spv_spec_freeze_fast(
    const uint32_t *w, size_t nw, uint32_t spec_id, uint32_t value, size_t *out_nw) {
    enum { MAXS = 16 };
    if (nw < 5u || w[0] != 0x07230203u) return NULL;
    uint32_t sid[MAXS], sval[MAXS];
    int ns = 0, lane = 0;
    size_t first_fn = nw;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) return NULL;
        if (op == 54u) { first_fn = i; break; }
        if (op == 71u && len >= 4u && w[i + 2] == 1u) {
            if (ns == MAXS) return NULL;
            sid[ns] = w[i + 1]; sval[ns++] = w[i + 3];
        } else if (op == 71u && len >= 4u && w[i + 2] == 11u &&
                   (w[i + 3] == 41u || (w[i + 3] >= 4416u && w[i + 3] <= 4420u))) {
            lane = 1;
        } else if (op == 51u || op == 52u) return NULL;
        i += len;
    }
    if (!lane || !ns) return NULL;
    uint32_t *o = (uint32_t *)malloc(nw * 4u);
    if (!o) return NULL;
    memcpy(o, w, 20u);
    size_t n = 5u;
    for (size_t i = 5u; i < first_fn;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (op == 71u && len >= 4u && w[i + 2] == 1u) { i += len; continue; }
        memcpy(o + n, w + i, len * 4u);
        if ((op == 48u || op == 49u || op == 50u) && len >= 3u) {
            int k = 0;
            while (k < ns && sid[k] != w[i + 2]) k++;
            int mine = k < ns && sval[k] == spec_id;
            if (op == 50u) {
                o[n] = (len << 16) | 43u;
                if (mine && len == 4u) o[n + 3] = (vka_w39_fault() == 1) ? (value ^ 1u) : value;
            } else {
                uint32_t c_op = mine ? (value ? 41u : 42u) : (op == 48u ? 41u : 42u);
                if (mine && vka_w39_fault() == 1) c_op = (c_op == 41u) ? 42u : 41u;
                o[n] = (len << 16) | c_op;
            }
        }
        n += len; i += len;
    }
    if (first_fn < nw) { memcpy(o + n, w + first_fn, (nw - first_fn) * 4u); n += (nw - first_fn); }
    *out_nw = n;
    return o;
}

typedef struct {
    int use_heap;
    uint32_t bound;
    int32_t *loc;
    uint32_t *ptee, *arr_el, *arr_c, *vec_base, *cval;
    uint8_t *has_bi, *sc_kind, *is_c, *is_arr, *in_var;
    int32_t loc_s[VKA_SPV_STACK_BOUND];
    uint32_t ptee_s[VKA_SPV_STACK_BOUND], arr_el_s[VKA_SPV_STACK_BOUND];
    uint32_t arr_c_s[VKA_SPV_STACK_BOUND], vec_base_s[VKA_SPV_STACK_BOUND];
    uint32_t cval_s[VKA_SPV_STACK_BOUND];
    uint8_t bi_s[VKA_SPV_STACK_BOUND], sc_s[VKA_SPV_STACK_BOUND];
    uint8_t isc_s[VKA_SPV_STACK_BOUND], isa_s[VKA_SPV_STACK_BOUND], inv_s[VKA_SPV_STACK_BOUND];
} vka_fs_locs_tbl;

static inline int vka_fs_locs_tbl_init(vka_fs_locs_tbl *t, uint32_t bound) {
    uint32_t n = bound ? bound : 1u;
    t->bound = bound;
    if (n <= VKA_SPV_STACK_BOUND) {
        t->use_heap = 0;
        t->loc = t->loc_s; t->ptee = t->ptee_s; t->arr_el = t->arr_el_s;
        t->arr_c = t->arr_c_s; t->vec_base = t->vec_base_s; t->cval = t->cval_s;
        t->has_bi = t->bi_s; t->sc_kind = t->sc_s; t->is_c = t->isc_s;
        t->is_arr = t->isa_s; t->in_var = t->inv_s;
        memset(t->ptee, 0, n * 4u); memset(t->arr_el, 0, n * 4u);
        memset(t->arr_c, 0, n * 4u); memset(t->vec_base, 0, n * 4u);
        memset(t->has_bi, 0, n); memset(t->sc_kind, 0, n);
        memset(t->is_c, 0, n); memset(t->is_arr, 0, n); memset(t->in_var, 0, n);
    } else {
        t->use_heap = 1;
        t->loc = (int32_t *)malloc((size_t)n * 4u);
        t->ptee = (uint32_t *)calloc(n, 4u); t->arr_el = (uint32_t *)calloc(n, 4u);
        t->arr_c = (uint32_t *)calloc(n, 4u); t->vec_base = (uint32_t *)calloc(n, 4u);
        t->cval = (uint32_t *)calloc(n, 4u); t->has_bi = (uint8_t *)calloc(n, 1u);
        t->sc_kind = (uint8_t *)calloc(n, 1u); t->is_c = (uint8_t *)calloc(n, 1u);
        t->is_arr = (uint8_t *)calloc(n, 1u); t->in_var = (uint8_t *)calloc(n, 1u);
        if (!t->loc || !t->ptee || !t->arr_el || !t->arr_c || !t->vec_base ||
            !t->cval || !t->has_bi || !t->sc_kind || !t->is_c || !t->is_arr || !t->in_var) return 0;
    }
    for (uint32_t i = 0; i < n; i++) t->loc[i] = -1;
    return 1;
}

static inline void vka_fs_locs_tbl_free(vka_fs_locs_tbl *t) {
    if (!t->use_heap) return;
    free(t->loc); free(t->ptee); free(t->arr_el); free(t->arr_c);
    free(t->vec_base); free(t->cval); free(t->has_bi); free(t->sc_kind);
    free(t->is_c); free(t->is_arr); free(t->in_var);
}

static inline int vka_fs_locs_scan_hdr(
    const uint32_t *w, size_t nw, vka_fs_locs_tbl *t, uint32_t *ep_ok, size_t *fn_at) {
    uint32_t b = t->bound;
    *ep_ok = 0u; *fn_at = nw;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) return 0;
        if (op == 54u && *fn_at == nw) { *fn_at = i; break; }
        if (op == 15u) *ep_ok = 1u;
        else if (op == 71u && len >= 4u && w[i + 1] < b) {
            if (w[i + 2] == 11u) t->has_bi[w[i + 1]] = 1u;
            else if (w[i + 2] == 30u) t->loc[w[i + 1]] = (int32_t)w[i + 3];
        } else if (op == 22u && len >= 3u && w[i + 1] < b && w[i + 2] == 32u) t->sc_kind[w[i + 1]] = 1u;
        else if (op == 21u && len >= 4u && w[i + 1] < b && w[i + 2] == 32u) t->sc_kind[w[i + 1]] = 1u;
        else if (op == 20u && len >= 2u && w[i + 1] < b) t->sc_kind[w[i + 1]] = 1u;
        else if (op == 23u && len >= 4u && w[i + 1] < b) t->vec_base[w[i + 1]] = w[i + 2];
        else if (op == 28u && len >= 4u && w[i + 1] < b) {
            t->is_arr[w[i + 1]] = 1u; t->arr_el[w[i + 1]] = w[i + 2]; t->arr_c[w[i + 1]] = w[i + 3];
        } else if (op == 32u && len >= 4u && w[i + 1] < b) t->ptee[w[i + 1]] = w[i + 3];
        else if (op == 43u && len >= 4u && w[i + 2] < b) { t->is_c[w[i + 2]] = 1u; t->cval[w[i + 2]] = w[i + 3]; }
        else if (op == 59u && len >= 4u && w[i + 3] == 1u && w[i + 1] < b && w[i + 2] < b) {
            t->in_var[w[i + 2]] = 1u; t->ptee[w[i + 2]] = t->ptee[w[i + 1]];
        }
        i += len;
    }
    return 1;
}

static inline int vka_memu_fs_input_locs_fast(const uint32_t *w, size_t nw, uint64_t *mask) {
    *mask = 0u;
    if (nw < 5u || w[0] != 0x07230203u) return 0;
    vka_fs_locs_tbl t;
    if (!vka_fs_locs_tbl_init(&t, w[3])) { vka_fs_locs_tbl_free(&t); return 0; }
    uint32_t ep_ok = 0u;
    size_t fn_at = nw;
    int ok = vka_fs_locs_scan_hdr(w, nw, &t, &ep_ok, &fn_at);
    for (size_t i = fn_at; ok && i < nw;) {
        uint32_t len = w[i] >> 16;
        if (!len || i + len > nw) ok = 0;
        i += len ? len : 1u;
    }
    if (!ep_ok) ok = 0;
    for (uint32_t id = 0; ok && id < t.bound; id++) {
        if (!t.in_var[id] || t.has_bi[id]) continue;
        uint32_t pt = t.ptee[id], n = 1u, el = pt;
        if (pt < t.bound && t.is_arr[pt]) {
            uint32_t cid = t.arr_c[pt];
            n = (cid < t.bound && t.is_c[cid]) ? t.cval[cid] : 0u;
            el = t.arr_el[pt];
            if (!n) { ok = 0; break; }
        }
        uint32_t ct = (el < t.bound && t.vec_base[el]) ? t.vec_base[el] : el;
        int sc = (ct < t.bound) ? t.sc_kind[ct] : 0;
        if (t.loc[id] < 0 || !sc || (uint32_t)t.loc[id] + n > 64u) { ok = 0; break; }
        for (uint32_t k = 0; k < n; k++) *mask |= UINT64_C(1) << ((uint32_t)t.loc[id] + k);
    }
    vka_fs_locs_tbl_free(&t);
    if (!ok) { *mask = 0u; return 0; }
    if (vka_w39_fault() == 5) *mask ^= UINT64_C(1);
    return 1;
}

typedef struct {
    int use_heap;
    uint32_t bound;
    uint32_t *cls, *loc, *ty, *ptee;
    uint32_t cls_s[VKA_SPV_STACK_BOUND], loc_s[VKA_SPV_STACK_BOUND];
    uint32_t ty_s[VKA_SPV_STACK_BOUND], ptee_s[VKA_SPV_STACK_BOUND];
} vka_retype_tbl;

static inline int vka_retype_tbl_init(vka_retype_tbl *t, uint32_t bound, int need_ty) {
    uint32_t n = bound ? bound : 1u;
    t->bound = bound;
    if (n <= VKA_SPV_STACK_BOUND) {
        t->use_heap = 0;
        t->cls = t->cls_s; t->loc = t->loc_s; t->ty = t->ty_s; t->ptee = t->ptee_s;
        memset(t->cls, 0, n * 4u);
        if (need_ty) { memset(t->ty, 0, n * 4u); memset(t->ptee, 0, n * 4u); }
    } else {
        t->use_heap = 1;
        t->cls = (uint32_t *)calloc(n, 4u);
        t->loc = (uint32_t *)malloc((size_t)n * 4u);
        t->ty = need_ty ? (uint32_t *)calloc(n, 4u) : NULL;
        t->ptee = need_ty ? (uint32_t *)calloc(n, 4u) : NULL;
        if (!t->cls || !t->loc || (need_ty && (!t->ty || !t->ptee))) return 0;
    }
    for (uint32_t i = 0; i < n; i++) t->loc[i] = ~0u;
    return 1;
}

static inline void vka_retype_tbl_free(vka_retype_tbl *t) {
    if (!t->use_heap) return;
    free(t->cls); free(t->loc); free(t->ty); free(t->ptee);
}

static inline int vka_vin_special_ok(uint32_t op) {
    return op == 5u || op == 6u || op == 15u || op == 71u ||
           op == 72u || op == 332u || op == 5632u || op == 5633u;
}

static inline int vka_vin_scan_fast(
    const uint32_t *w, size_t nw, uint32_t umask, uint32_t smask,
    vka_retype_tbl *t, uint32_t *t_s, uint32_t *t_v, uint32_t p_in[2][2],
    uint32_t moved[32], int *nmoved, size_t *fn_at, char *err, size_t errn) {
    uint32_t b = t->bound, t_f32 = 0u, t_v4f = 0u, p_in_v4f = 0u;
    *fn_at = nw;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) { snprintf(err, errn, "bad instruction at word %zu", i); return 0; }
        if (op == 54u && *fn_at == nw) { *fn_at = i; break; }
        if (op == 71u && len >= 4u && w[i + 2] == 30u && w[i + 1] < b) t->loc[w[i + 1]] = w[i + 3];
        else if (op == 22u && len >= 3u && w[i + 2] == 32u) t_f32 = w[i + 1];
        else if (op == 21u && len >= 4u && w[i + 2] == 32u) t_s[w[i + 3] ? 1 : 0] = w[i + 1];
        else if (op == 23u && len >= 4u && w[i + 3] == 4u) {
            if (w[i + 2] == t_f32 && t_f32) t_v4f = w[i + 1];
            else if (w[i + 2] == t_s[0] && t_s[0]) t_v[0] = w[i + 1];
            else if (w[i + 2] == t_s[1] && t_s[1]) t_v[1] = w[i + 1];
        } else if (op == 32u && len >= 4u && w[i + 2] == 1u) {
            uint32_t pt = w[i + 3];
            if (pt == t_v4f && pt) p_in_v4f = w[i + 1];
            for (int c = 0; c < 2; c++) {
                if (pt == t_s[c] && pt) p_in[c][0] = w[i + 1];
                if (pt == t_v[c] && pt) p_in[c][1] = w[i + 1];
            }
        }
        i += len;
    }
    if (!t_f32 || !t_v4f || !p_in_v4f) { snprintf(err, errn, "no vec4 float input type"); return 0; }
    uint32_t found = 0u; *nmoved = 0;
    for (size_t i = 5u; i < *fn_at; i += w[i] >> 16) {
        if ((w[i] & 0xffffu) == 59u && (w[i] >> 16) >= 4u && w[i + 3] == 1u &&
            w[i + 2] < b && w[i + 1] == p_in_v4f) {
            uint32_t id = w[i + 2], l = t->loc[id];
            if (l < 32u && (((umask | smask) >> l) & 1u)) {
                t->cls[id] = ((umask >> l & 1u) ? 1u : 2u) | 16u;
                if (*nmoved < 32) moved[(*nmoved)++] = id;
                found |= 1u << l;
            }
        }
    }
    if (found != (umask | smask)) {
        snprintf(err, errn, "inputs %#x are not vec4 float Input variables", (umask | smask) & ~found);
        return 0;
    }
    return 1;
}

static inline int vka_vin_emit_hdr(
    uint32_t **out, size_t *on, size_t *ocap, const int need_c[2],
    const uint32_t mk_s[2], const uint32_t mk_v[2], const uint32_t mk_p[2][2],
    const uint32_t t_s[2], const uint32_t t_v[2], const uint32_t p_in[2][2],
    const uint32_t *cls, const uint32_t moved[32], int nmoved) {
    for (int c = 0; c < 2; c++) {
        if (!need_c[c]) continue;
        if (mk_s[c]) { uint32_t x[4] = {(4u << 16) | 21u, mk_s[c], 32u, (uint32_t)c}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
        if (mk_v[c]) { uint32_t x[4] = {(4u << 16) | 23u, mk_v[c], t_s[c], 4u}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
        if (mk_p[c][0]) { uint32_t x[4] = {(4u << 16) | 32u, mk_p[c][0], 1u, t_s[c]}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
        if (mk_p[c][1]) { uint32_t x[4] = {(4u << 16) | 32u, mk_p[c][1], 1u, t_v[c]}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
    }
    for (int k = 0; k < nmoved; k++) {
        uint32_t x[4] = {(4u << 16) | 59u, p_in[(cls[moved[k]] & 3u) - 1u][1], moved[k], 1u};
        if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0;
    }
    return 1;
}

static inline int vka_vin_step_fast(
    const uint32_t *w, size_t i, uint32_t len, uint32_t op, size_t first_fn,
    vka_retype_tbl *tbl, const uint32_t t_s[2], const uint32_t t_v[2],
    const uint32_t p_in[2][2], uint32_t *nb, uint32_t **out, size_t *on,
    size_t *ocap, size_t *run_a, char *err, size_t errn) {
    uint32_t b = tbl->bound;
    if (op == 59u && len >= 4u && w[i + 2] < b && tbl->cls[w[i + 2]] && i < first_fn) {
        return vka_spv_flush_run(out, on, ocap, w, *run_a, i) && ((*run_a = i + len), 1);
    }
    if ((op == 65u || op == 66u) && len >= 4u && w[i + 3] < b && tbl->cls[w[i + 3]]) {
        uint32_t c = (tbl->cls[w[i + 3]] & 3u) - 1u, r = w[i + 2]; int vec = (len == 4u);
        if (r >= b || (!vec && len != 5u)) {
            snprintf(err, errn, r >= b ? "bad id" : "an access chain of %u indexes into a retyped input", len - 4u);
            return 0;
        }
        tbl->cls[r] = (c + 1u) | (vec ? 16u : 0u);
        if (!vka_spv_flush_run(out, on, ocap, w, *run_a, i) ||
            !vka_spv_put_words_w39(out, on, ocap, w + i, len)) return 0;
        (*out)[*on - len + 1u] = p_in[c][vec]; *run_a = i + len; return 1;
    }
    if (op == 61u && len >= 4u && w[i + 3] < b && tbl->cls[w[i + 3]]) {
        uint32_t c = (tbl->cls[w[i + 3]] & 3u) - 1u, vec = (tbl->cls[w[i + 3]] & 16u) != 0u;
        uint32_t ft = w[i + 1], r = w[i + 2], tmp = (*nb)++;
        uint32_t bc[4] = {(4u << 16) | 124u, ft, r, (vka_w39_fault() == 2) ? (tmp ^ 1u) : tmp};
        if (!vka_spv_flush_run(out, on, ocap, w, *run_a, i) ||
            !vka_spv_put_words_w39(out, on, ocap, w + i, len)) return 0;
        (*out)[*on - len + 1u] = vec ? t_v[c] : t_s[c]; (*out)[*on - len + 2u] = tmp;
        *run_a = i + len; return vka_spv_put_words_w39(out, on, ocap, bc, 4u);
    }
    if (!vka_vin_special_ok(op) && i >= first_fn) {
        for (uint32_t k = 1u; k < len; k++) {
            if (!(op == 12u && k == 4u) && w[i + k] < b && tbl->cls[w[i + k]] && op != 59u) {
                snprintf(err, errn, "opcode %u uses a retyped input (id %u)", op, w[i + k]);
                return 0;
            }
        }
    }
    return 1;
}

static inline uint32_t *vka_spv_vin_retype_fast(
    const uint32_t *w, size_t nw, uint32_t umask, uint32_t smask,
    size_t *out_nw, char *err, size_t errn) {
    if (nw < 5u || w[0] != 0x07230203u) { snprintf(err, errn, "not SPIR-V"); return NULL; }
    if ((umask | smask) == 0u || (umask & smask)) {
        snprintf(err, errn, "no input, or an input both uint and int"); return NULL;
    }
    vka_retype_tbl tbl;
    uint32_t b = w[3], t_s[2] = {0u, 0u}, t_v[2] = {0u, 0u}, p_in[2][2] = {{0u, 0u}, {0u, 0u}}, moved[32] = {0u};
    if (!vka_retype_tbl_init(&tbl, b, 0)) { vka_retype_tbl_free(&tbl); snprintf(err, errn, "out of memory"); return NULL; }
    int nmoved = 0; size_t first_fn = nw;
    if (!vka_vin_scan_fast(w, nw, umask, smask, &tbl, t_s, t_v, p_in, moved, &nmoved, &first_fn, err, errn)) {
        vka_retype_tbl_free(&tbl); return NULL;
    }
    uint32_t nb = b, mk_s[2] = {0u, 0u}, mk_v[2] = {0u, 0u}, mk_p[2][2] = {{0u, 0u}, {0u, 0u}};
    int need_c[2] = {(umask != 0u), (smask != 0u)};
    for (int c = 0; c < 2; c++) if (need_c[c]) {
        if (!t_s[c]) t_s[c] = mk_s[c] = nb++; if (!t_v[c]) t_v[c] = mk_v[c] = nb++;
        if (!p_in[c][0]) p_in[c][0] = mk_p[c][0] = nb++; if (!p_in[c][1]) p_in[c][1] = mk_p[c][1] = nb++;
    }
    uint32_t *out = NULL; size_t on = 0u, ocap = 0u, run_a = 5u;
    int ok = vka_spv_reserve_w39(&out, &ocap, nw + 64u) && vka_spv_put_words_w39(&out, &on, &ocap, w, 5u);
    for (size_t i = 5u; ok && i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) { snprintf(err, errn, "bad instruction at word %zu", i); ok = 0; break; }
        if (i == first_fn) {
            ok = vka_spv_flush_run(&out, &on, &ocap, w, run_a, i) &&
                 vka_vin_emit_hdr(&out, &on, &ocap, need_c, mk_s, mk_v, mk_p, t_s, t_v, p_in, tbl.cls, moved, nmoved);
            run_a = i;
        }
        if (ok) ok = vka_vin_step_fast(w, i, len, op, first_fn, &tbl, t_s, t_v, p_in, &nb, &out, &on, &ocap, &run_a, err, errn);
        i += len;
    }
    if (ok) ok = vka_spv_flush_run(&out, &on, &ocap, w, run_a, nw);
    vka_retype_tbl_free(&tbl);
    if (!ok) { free(out); return NULL; }
    out[3] = nb; *out_nw = on; return out;
}

static inline int vka_fout_scan_fast(
    const uint32_t *w, size_t nw, uint32_t umask, uint32_t smask,
    vka_retype_tbl *t, uint32_t t_ivec[2][5], uint32_t p_out[2][5],
    uint32_t moved[32], uint32_t mcomp[32], int *nmoved, uint32_t need[2],
    size_t *fn_at, char *err, size_t errn) {
    uint32_t b = t->bound, t_f32 = 0u, t_int[2] = {0u, 0u};
    *fn_at = nw;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) { snprintf(err, errn, "bad instruction at word %zu", i); return 0; }
        if (op == 54u && *fn_at == nw) { *fn_at = i; break; }
        if (op == 71u && len >= 4u && w[i + 2] == 30u && w[i + 1] < b) t->loc[w[i + 1]] = w[i + 3];
        else if (op == 22u && len >= 3u && w[i + 2] == 32u && w[i + 1] < b) { t_f32 = w[i + 1]; t->ty[t_f32] = 1u; }
        else if (op == 21u && len >= 4u && w[i + 2] == 32u) t_int[w[i + 3] ? 1 : 0] = w[i + 1];
        else if (op == 23u && len >= 4u && w[i + 3] >= 2u && w[i + 3] <= 4u && w[i + 1] < b) {
            if (w[i + 2] == t_f32 && t_f32) t->ty[w[i + 1]] = w[i + 3];
            for (int c = 0; c < 2; c++) if (w[i + 2] == t_int[c] && t_int[c]) t_ivec[c][w[i + 3]] = w[i + 1];
        } else if (op == 32u && len >= 4u && w[i + 2] == 3u && w[i + 1] < b) {
            t->ptee[w[i + 1]] = w[i + 3];
            for (int c = 0; c < 2; c++) {
                if (w[i + 3] == t_int[c] && t_int[c]) p_out[c][1] = w[i + 1];
                for (int n = 2; n <= 4; n++) if (w[i + 3] == t_ivec[c][n] && t_ivec[c][n]) p_out[c][n] = w[i + 1];
            }
        }
        i += len;
    }
    t_ivec[0][1] = t_int[0]; t_ivec[1][1] = t_int[1];
    uint32_t found = 0u; *nmoved = 0; need[0] = need[1] = 0u;
    for (size_t i = 5u; i < *fn_at; i += w[i] >> 16) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (op == 59u && len >= 4u && w[i + 3] == 3u && w[i + 1] < b && w[i + 2] < b) {
            uint32_t id = w[i + 2], l = t->loc[id], pt = t->ptee[w[i + 1]], n = pt < b ? t->ty[pt] : 0u;
            if (l < 32u && (((umask | smask) >> l) & 1u)) {
                if (!n || len > 4u || *nmoved == 32) {
                    snprintf(err, errn, !n ? "output %u is not a float or float vector" : len > 4u ? "output %u has an initializer" : "too many outputs", l);
                    return 0;
                }
                int c = ((umask >> l) & 1u) ? 0 : 1;
                t->cls[id] = (uint32_t)(c + 1) | 16u; mcomp[*nmoved] = n; moved[(*nmoved)++] = id;
                found |= 1u << l; need[c] |= (1u << n) | 2u;
            }
        }
    }
    if (found != (umask | smask)) {
        snprintf(err, errn, "outputs %#x are not float Output variables", (umask | smask) & ~found); return 0;
    }
    return 1;
}

static inline int vka_fout_emit_hdr(
    uint32_t **out, size_t *on, size_t *ocap,
    const uint32_t mk_t[2][5], const uint32_t mk_p[2][5],
    const uint32_t t_ivec[2][5], const uint32_t p_out[2][5],
    const uint32_t *cls, const uint32_t moved[32], const uint32_t mcomp[32], int nmoved) {
    for (int c = 0; c < 2; c++) {
        if (mk_t[c][1]) { uint32_t x[4] = {(4u << 16) | 21u, mk_t[c][1], 32u, (uint32_t)c}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
        for (int n = 2; n <= 4; n++) if (mk_t[c][n]) { uint32_t x[4] = {(4u << 16) | 23u, mk_t[c][n], t_ivec[c][1], (uint32_t)n}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
        for (int n = 1; n <= 4; n++) if (mk_p[c][n]) { uint32_t x[4] = {(4u << 16) | 32u, mk_p[c][n], 3u, t_ivec[c][n]}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
    }
    for (int k = 0; k < nmoved; k++) {
        uint32_t x[4] = {(4u << 16) | 59u, p_out[(cls[moved[k]] & 3u) - 1u][mcomp[k]], moved[k], 3u};
        if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0;
    }
    return 1;
}

static inline uint32_t vka_fout_ncomp(
    const uint32_t *cls, const uint32_t moved[32], const uint32_t mcomp[32], int nmoved, uint32_t p) {
    if (cls[p] & 16u) {
        for (int k = 0; k < nmoved; k++) if (moved[k] == p) return mcomp[k];
    }
    return 1u;
}

static inline int vka_fout_step_fast(
    const uint32_t *w, size_t i, uint32_t len, uint32_t op, size_t first_fn,
    vka_retype_tbl *tbl, const uint32_t t_ivec[2][5], const uint32_t p_out[2][5],
    const uint32_t moved[32], const uint32_t mcomp[32], int nmoved, uint32_t *nb,
    uint32_t **out, size_t *on, size_t *ocap, size_t *run_a, char *err, size_t errn) {
    uint32_t b = tbl->bound;
    if (op == 59u && len >= 4u && w[i + 2] < b && tbl->cls[w[i + 2]] && i < first_fn) {
        return vka_spv_flush_run(out, on, ocap, w, *run_a, i) && ((*run_a = i + len), 1);
    }
    if ((op == 65u || op == 66u) && len >= 4u && w[i + 3] < b && tbl->cls[w[i + 3]]) {
        uint32_t c = (tbl->cls[w[i + 3]] & 3u) - 1u, r = w[i + 2];
        if (r >= b || len != 5u || !(tbl->cls[w[i + 3]] & 16u)) {
            snprintf(err, errn, r >= b ? "bad id" : "an access chain of %u indexes into a retyped output", len - 4u); return 0;
        }
        tbl->cls[r] = c + 1u;
        if (!vka_spv_flush_run(out, on, ocap, w, *run_a, i) || !vka_spv_put_words_w39(out, on, ocap, w + i, len)) return 0;
        (*out)[*on - len + 1u] = p_out[c][1]; *run_a = i + len; return 1;
    }
    if (op == 62u && len >= 3u && w[i + 1] < b && tbl->cls[w[i + 1]]) {
        uint32_t p = w[i + 1], c = (tbl->cls[p] & 3u) - 1u, tmp = (*nb)++;
        uint32_t n = vka_fout_ncomp(tbl->cls, moved, mcomp, nmoved, p);
        uint32_t bc[4] = {(4u << 16) | 124u, (vka_w39_fault() == 3) ? t_ivec[c][1] : t_ivec[c][n], tmp, w[i + 2]};
        if (!vka_spv_flush_run(out, on, ocap, w, *run_a, i) ||
            !vka_spv_put_words_w39(out, on, ocap, bc, 4u) || !vka_spv_put_words_w39(out, on, ocap, w + i, len)) return 0;
        (*out)[*on - len + 2u] = (vka_w39_fault() == 3) ? (tmp ^ 1u) : tmp; *run_a = i + len; return 1;
    }
    if (op == 61u && len >= 4u && w[i + 3] < b && tbl->cls[w[i + 3]]) {
        uint32_t p = w[i + 3], c = (tbl->cls[p] & 3u) - 1u, ft = w[i + 1], r = w[i + 2], tmp = (*nb)++;
        uint32_t n = vka_fout_ncomp(tbl->cls, moved, mcomp, nmoved, p), bc[4] = {(4u << 16) | 124u, ft, r, tmp};
        if (!vka_spv_flush_run(out, on, ocap, w, *run_a, i) || !vka_spv_put_words_w39(out, on, ocap, w + i, len)) return 0;
        (*out)[*on - len + 1u] = t_ivec[c][n]; (*out)[*on - len + 2u] = tmp; *run_a = i + len;
        return vka_spv_put_words_w39(out, on, ocap, bc, 4u);
    }
    if (!vka_vin_special_ok(op) && i >= first_fn) {
        for (uint32_t k = 1u; k < len; k++) {
            if (!(op == 12u && k == 4u) && w[i + k] < b && tbl->cls[w[i + k]]) {
                snprintf(err, errn, "opcode %u uses a retyped output (id %u)", op, w[i + k]); return 0;
            }
        }
    }
    return 1;
}

static inline uint32_t *vka_spv_fout_retype_fast(
    const uint32_t *w, size_t nw, uint32_t umask, uint32_t smask,
    size_t *out_nw, char *err, size_t errn) {
    if (nw < 5u || w[0] != 0x07230203u) { snprintf(err, errn, "not SPIR-V"); return NULL; }
    if ((umask | smask) == 0u || (umask & smask)) {
        snprintf(err, errn, "no output, or an output both uint and int"); return NULL;
    }
    vka_retype_tbl tbl;
    if (!vka_retype_tbl_init(&tbl, w[3], 1)) { vka_retype_tbl_free(&tbl); snprintf(err, errn, "out of memory"); return NULL; }
    uint32_t t_ivec[2][5] = {{0}}, p_out[2][5] = {{0}}, moved[32] = {0}, mcomp[32] = {0}, need[2] = {0, 0};
    int nmoved = 0; size_t first_fn = nw;
    if (!vka_fout_scan_fast(w, nw, umask, smask, &tbl, t_ivec, p_out, moved, mcomp, &nmoved, need, &first_fn, err, errn)) {
        vka_retype_tbl_free(&tbl); return NULL;
    }
    uint32_t nb = w[3], mk_t[2][5] = {{0}}, mk_p[2][5] = {{0}};
    for (int c = 0; c < 2; c++) for (int n = 1; n <= 4; n++) if ((need[c] >> n) & 1u) {
        if (!t_ivec[c][n]) t_ivec[c][n] = mk_t[c][n] = nb++;
        if (!p_out[c][n]) p_out[c][n] = mk_p[c][n] = nb++;
    }
    if ((need[0] && !t_ivec[0][1]) || (need[1] && !t_ivec[1][1])) {
        vka_retype_tbl_free(&tbl); snprintf(err, errn, "no scalar type"); return NULL;
    }
    uint32_t *out = NULL; size_t on = 0u, ocap = 0u, run_a = 5u;
    int ok = vka_spv_reserve_w39(&out, &ocap, nw + 64u) && vka_spv_put_words_w39(&out, &on, &ocap, w, 5u);
    for (size_t i = 5u; ok && i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) { snprintf(err, errn, "bad instruction at word %zu", i); ok = 0; break; }
        if (i == first_fn) {
            ok = vka_spv_flush_run(&out, &on, &ocap, w, run_a, i) &&
                 vka_fout_emit_hdr(&out, &on, &ocap, mk_t, mk_p, t_ivec, p_out, tbl.cls, moved, mcomp, nmoved);
            run_a = i;
        }
        if (ok) ok = vka_fout_step_fast(w, i, len, op, first_fn, &tbl, t_ivec, p_out, moved, mcomp, nmoved, &nb, &out, &on, &ocap, &run_a, err, errn);
        i += len;
    }
    if (ok) ok = vka_spv_flush_run(&out, &on, &ocap, w, run_a, nw);
    vka_retype_tbl_free(&tbl);
    if (!ok) { free(out); return NULL; }
    out[3] = nb; *out_nw = on; return out;
}

typedef struct {
    int use_heap;
    uint32_t bound;
    uint8_t *bi, *chain;
    int32_t *cval;
    uint8_t bi_s[VKA_SPV_STACK_BOUND], ch_s[VKA_SPV_STACK_BOUND];
    int32_t cv_s[VKA_SPV_STACK_BOUND];
} vka_sg_tbl;

static inline int vka_sg_tbl_init(vka_sg_tbl *t, uint32_t bound) {
    uint32_t n = bound ? bound : 1u;
    t->bound = bound;
    if (n <= VKA_SPV_STACK_BOUND) {
        t->use_heap = 0; t->bi = t->bi_s; t->chain = t->ch_s; t->cval = t->cv_s;
        memset(t->bi, 0, n); memset(t->chain, 0, n);
    } else {
        t->use_heap = 1;
        t->bi = (uint8_t *)calloc(n, 1u); t->chain = (uint8_t *)calloc(n, 1u);
        t->cval = (int32_t *)malloc((size_t)n * 4u);
        if (!t->bi || !t->chain || !t->cval) return 0;
    }
    for (uint32_t i = 0; i < n; i++) t->cval[i] = -1;
    return 1;
}

static inline void vka_sg_tbl_free(vka_sg_tbl *t) {
    if (!t->use_heap) return;
    free(t->bi); free(t->chain); free(t->cval);
}

static inline int vka_sg_bi_kind(uint32_t b) {
    switch (b) {
    case 41u: case 4418u: case 4420u: return 1;
    case 36u: case 4416u: case 4417u: case 4419u: return 2;
    default: return 0;
    }
}

static inline int vka_sg_scan(
    const uint32_t *w, size_t nw, vka_sg_tbl *t,
    uint32_t *t_u32, uint32_t *t_v4u, uint32_t *t_bool,
    size_t *first_fn, int *any, char *err, size_t errn) {
    *first_fn = nw; *any = 0;
    for (size_t i = 5u; i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (!len || i + len > nw) { snprintf(err, errn, "bad instruction at word %zu", i); return 0; }
        if (op == 54u && *first_fn == nw) *first_fn = i;
        if (i < *first_fn) {
            if (op == 71u && len >= 4u && w[i + 2] == 11u && w[i + 1] < t->bound && vka_sg_bi_kind(w[i + 3])) {
                t->bi[w[i + 1]] = (uint8_t)vka_sg_bi_kind(w[i + 3]) | (w[i + 3] >= 4416u ? 4u : 0u);
                *any = 1;
            } else if (op == 21u && len >= 4u && w[i + 2] == 32u && !w[i + 3]) *t_u32 = w[i + 1];
            else if (op == 20u && len >= 2u) *t_bool = w[i + 1];
            else if (op == 23u && len >= 4u && w[i + 3] == 4u && w[i + 2] == *t_u32 && *t_u32) *t_v4u = w[i + 1];
            else if (op == 43u && len >= 4u && w[i + 2] < t->bound) t->cval[w[i + 2]] = (int32_t)w[i + 3];
        } else if (op >= 333u && op <= 348u) *any = 1;
        i += len;
    }
    return 1;
}

static inline int vka_sg_rewrite_op(
    uint32_t op, const uint32_t *ins, uint32_t len, uint32_t t_v4u, uint32_t t_u32,
    uint32_t c0, uint32_t c1, uint32_t ctrue, uint32_t *nb,
    uint32_t **out, size_t *on, size_t *ocap, char *err, size_t errn) {
    uint32_t rt = ins[1], r = ins[2], v = 0u;
    if (op == 333u || op == 336u) { v = ctrue; }
    else if (op == 334u || op == 335u || (op >= 337u && op <= 340u) || (op >= 345u && op <= 348u)) {
        if (len < 5u) { snprintf(err, errn, "short subgroup instruction %u", op); return 0; }
        v = ins[4];
    } else if (op == 341u) {
        if (len < 5u || rt != t_v4u) { snprintf(err, errn, "a ballot of an unexpected type"); return 0; }
        uint32_t tmp = (*nb)++;
        uint32_t x[13] = {(6u << 16) | 169u, t_u32, tmp, ins[4], c1, c0,
                          (7u << 16) | 80u, t_v4u, r, tmp, c0, c0, c0};
        return vka_spv_put_words_w39(out, on, ocap, x, 13u);
    } else { snprintf(err, errn, "subgroup instruction %u not rewritten", op); return 0; }
    uint32_t x[4] = {(4u << 16) | 83u, rt, r, v};
    return vka_spv_put_words_w39(out, on, ocap, x, 4u);
}

static inline int vka_sg_emit_consts(
    uint32_t **out, size_t *on, size_t *ocap, uint32_t mk_bool, uint32_t mk_v4,
    uint32_t t_u32, uint32_t t_v4u, uint32_t t_bool, uint32_t c0, uint32_t c1,
    uint32_t cv0, uint32_t cv1, uint32_t ctrue) {
    if (mk_bool) { uint32_t x[2] = {(2u << 16) | 20u, mk_bool}; if (!vka_spv_put_words_w39(out, on, ocap, x, 2u)) return 0; }
    if (mk_v4) { uint32_t x[4] = {(4u << 16) | 23u, mk_v4, t_u32, 4u}; if (!vka_spv_put_words_w39(out, on, ocap, x, 4u)) return 0; }
    uint32_t cdefs[25] = {
        (4u << 16) | 43u, t_u32, c0, 0u, (4u << 16) | 43u, t_u32, c1, 1u,
        (7u << 16) | 44u, t_v4u, cv0, c0, c0, c0, c0,
        (7u << 16) | 44u, t_v4u, cv1, c1, c0, c0, c0, (3u << 16) | 41u, t_bool, ctrue};
    return vka_spv_put_words_w39(out, on, ocap, cdefs, 25u);
}

static inline int vka_sg_step_fast(
    const uint32_t *w, size_t i, uint32_t len, uint32_t op, vka_sg_tbl *tbl,
    uint32_t t_v4u, uint32_t t_u32, uint32_t c0, uint32_t c1, uint32_t cv0,
    uint32_t cv1, uint32_t ctrue, uint32_t *nb, uint32_t **out, size_t *on,
    size_t *ocap, size_t *run_a, int *changed, char *err, size_t errn) {
    if ((op == 65u || op == 66u) && len >= 4u && w[i + 3] < tbl->bound && tbl->bi[w[i + 3]]) {
        if (len != 5u || w[i + 4] >= tbl->bound || tbl->cval[w[i + 4]] < 0) {
            snprintf(err, errn, "an access chain into a subgroup mask with a non-constant index"); return 0;
        }
        uint32_t k = tbl->bi[w[i + 3]] & 3u;
        tbl->chain[w[i + 2]] = (uint8_t)((k == 2u && tbl->cval[w[i + 4]] == 0) ? 2u : 1u);
        return vka_spv_flush_run(out, on, ocap, w, *run_a, i) && ((*run_a = i + len), 1);
    }
    if (op == 61u && len >= 4u && w[i + 3] < tbl->bound && (tbl->bi[w[i + 3]] || tbl->chain[w[i + 3]])) {
        uint32_t id = w[i + 3];
        uint32_t v = tbl->chain[id] ? (tbl->chain[id] == 2u ? c1 : c0)
                   : (tbl->bi[id] & 4u) ? ((tbl->bi[id] & 3u) == 2u ? cv1 : cv0)
                   : ((tbl->bi[id] & 3u) == 2u ? c1 : c0);
        if (vka_w39_fault() == 4) v = (v == c1 || v == cv1) ? c0 : c1;
        uint32_t x[4] = {(4u << 16) | 83u, w[i + 1], w[i + 2], v};
        *changed = 1;
        size_t prev = *run_a;
        *run_a = i + len;
        return vka_spv_flush_run(out, on, ocap, w, prev, i) && vka_spv_put_words_w39(out, on, ocap, x, 4u);
    }
    if (op >= 333u && op <= 348u) {
        *changed = 1;
        int ok = vka_spv_flush_run(out, on, ocap, w, *run_a, i) &&
                 vka_sg_rewrite_op(op, w + i, len, t_v4u, t_u32, c0, c1, ctrue, nb, out, on, ocap, err, errn);
        *run_a = i + len; return ok;
    }
    return 1;
}

static inline uint32_t *vka_spv_subgroup_single_fast(
    const uint32_t *w, size_t nw, size_t *out_nw, int *changed, char *err, size_t errn) {
    *changed = 0;
    if (nw < 5u || w[0] != 0x07230203u) { snprintf(err, errn, "not SPIR-V"); return NULL; }
    vka_sg_tbl tbl;
    if (!vka_sg_tbl_init(&tbl, w[3])) { vka_sg_tbl_free(&tbl); snprintf(err, errn, "out of memory"); return NULL; }
    uint32_t t_u32 = 0u, t_v4u = 0u, t_bool = 0u; size_t first_fn = nw; int any = 0;
    if (!vka_sg_scan(w, nw, &tbl, &t_u32, &t_v4u, &t_bool, &first_fn, &any, err, errn) || !any) {
        vka_sg_tbl_free(&tbl); return NULL;
    }
    if (!t_u32) { vka_sg_tbl_free(&tbl); snprintf(err, errn, "no 32-bit unsigned type"); return NULL; }
    uint32_t nb = tbl.bound, mk_v4 = t_v4u ? 0u : nb++, mk_bool = t_bool ? 0u : nb++;
    if (!t_v4u) t_v4u = mk_v4; if (!t_bool) t_bool = mk_bool;
    uint32_t c0 = nb++, c1 = nb++, cv0 = nb++, cv1 = nb++, ctrue = nb++;
    uint32_t *out = NULL; size_t on = 0u, ocap = 0u, run_a = 5u;
    int ok = vka_spv_reserve_w39(&out, &ocap, nw + 64u) && vka_spv_put_words_w39(&out, &on, &ocap, w, 5u);
    for (size_t i = 5u; ok && i < nw;) {
        uint32_t op = w[i] & 0xffffu, len = w[i] >> 16;
        if (i == first_fn) {
            ok = vka_spv_flush_run(&out, &on, &ocap, w, run_a, i) &&
                 vka_sg_emit_consts(&out, &on, &ocap, mk_bool, mk_v4, t_u32, t_v4u, t_bool, c0, c1, cv0, cv1, ctrue);
            run_a = i;
        }
        if (ok && i >= first_fn)
            ok = vka_sg_step_fast(w, i, len, op, &tbl, t_v4u, t_u32, c0, c1, cv0, cv1, ctrue, &nb, &out, &on, &ocap, &run_a, changed, err, errn);
        i += len;
    }
    if (ok) ok = vka_spv_flush_run(&out, &on, &ocap, w, run_a, nw);
    vka_sg_tbl_free(&tbl);
    if (!ok) { free(out); return NULL; }
    out[3] = nb; *out_nw = on; return out;
}
