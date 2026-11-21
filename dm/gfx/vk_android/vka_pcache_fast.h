// Fast pipeline-cache stamp I/O, prewarm key check & layout front-cache (DM_VK_PCACHE_FAST=1, default on; =0 disables).
#pragma once
#include "vka_gkey.h"
#include "vka_pwlist.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W34_TEST_HOOKS
extern _Atomic int g_vka_pcache_fast_state;
extern _Atomic int g_vka_w34_fault;
static inline void vka_pcache_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_pcache_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w34_set_fault(int mode) {
    atomic_store_explicit(&g_vka_w34_fault, mode, memory_order_release);
}
static inline int vka_w34_fault(void) {
    return atomic_load_explicit(&g_vka_w34_fault, memory_order_acquire);
}
#else
static _Atomic int g_vka_pcache_fast_state;
static inline int vka_w34_fault(void) { return 0; }
#endif

static inline int vka_pcache_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_pcache_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_PCACHE_FAST");
        int on = !(e && e[0] == '0' && !e[1]);
        s = on ? 2 : 1;
        atomic_store_explicit(&g_vka_pcache_fast_state, s, memory_order_release);
    }
    return s == 2;
}

typedef struct { uint64_t key, hash; } vka_pck_ent_w34;

static inline int vka_hex_nib(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline int vka_parse_hex64_tok(const char *s, size_t n, size_t *pos, uint64_t *out) {
    size_t i = *pos, d = 0;
    uint64_t v = 0;
    while (i < n) {
        int nib = vka_hex_nib((unsigned char)s[i]);
        if (nib < 0) break;
        if (++d > 16u) return 0;
        v = (v << 4) | (uint64_t)nib;
        i++;
    }
    if (!d) return 0;
    *pos = i; *out = v;
    return 1;
}

static inline int vka_parse_dec64_tok(const char *s, size_t n, size_t *pos, uint64_t *out) {
    size_t i = *pos, d = 0;
    uint64_t v = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        uint64_t dig = (uint64_t)(unsigned char)(s[i] - '0');
        if (v > (UINT64_MAX - dig) / 10u) return 0;
        v = v * 10u + dig;
        i++; d++;
    }
    if (!d) return 0;
    *pos = i; *out = v;
    return 1;
}

static inline int vka_match_lit(const char *s, size_t n, size_t *pos, const char *lit, size_t m) {
    if (*pos + m > n || memcmp(s + *pos, lit, m) != 0) return 0;
    *pos += m;
    return 1;
}

static inline int vka_parse_stamp_hdr2(const char *t, size_t n, size_t *pos,
                                       uint64_t *dead, uint64_t *cnt) {
    if (!vka_match_lit(t, n, pos, "keep 1 dead ", 12u)) return 0;
    if (!vka_parse_dec64_tok(t, n, pos, dead)) return 0;
    if (!vka_match_lit(t, n, pos, " shaders ", 9u)) return 0;
    if (!vka_parse_dec64_tok(t, n, pos, cnt) || *cnt > (UINT64_C(1) << 24)) return 0;
    if (*pos >= n || t[*pos] != '\n') return 0;
    (*pos)++;
    return 1;
}

static inline int vka_parse_stamp_line(const char *t, size_t n, size_t *pos,
                                       uint64_t *key, uint64_t *h) {
    if (!vka_parse_hex64_tok(t, n, pos, key)) return 0;
    if (*pos >= n || t[*pos] != ' ') return 0;
    (*pos)++;
    if (!vka_parse_hex64_tok(t, n, pos, h) || !*h) return 0;
    if (*pos < n) {
        if (t[*pos] != '\n') return 0;
        (*pos)++;
    }
    return 1;
}

static inline int vka_pck_parse_fast(const char *t, size_t n, uint64_t *out_lib,
                                     int *out_table, uint64_t *out_dead,
                                     void **out_ent, size_t *out_n,
                                     int (*cmp_fn)(const void *, const void *), int fault_mode) {
    *out_lib = 0; *out_table = 0; *out_dead = 0; *out_ent = NULL; *out_n = 0;
    if (!n) return 1;
    size_t pos = 0;
    uint64_t lib = 0, dead = 0, cnt = 0;
    if (!vka_parse_hex64_tok(t, n, &pos, &lib)) return 1;
    if (pos < n && t[pos] != '\n') return 0;
    *out_lib = lib;
    if (pos == n) return 1;
    pos++;
    if (!vka_parse_stamp_hdr2(t, n, &pos, &dead, &cnt)) return 1;
    vka_pck_ent_w34 *e = (vka_pck_ent_w34 *)malloc((size_t)(cnt ? cnt : 1u) * sizeof *e);
    if (!e) return 1;
    size_t k = 0; int sorted = 1; uint64_t prev = 0;
    while (k < (size_t)cnt && pos < n) {
        uint64_t key = 0, h = 0;
        if (!vka_parse_stamp_line(t, n, &pos, &key, &h)) break;
        if (k && key <= prev) sorted = 0;
        prev = key; e[k++] = (vka_pck_ent_w34){key, h};
    }
    if (k != (size_t)cnt) { free(e); return 1; }
    if (fault_mode == 1) sorted = 1;
    size_t u = k;
    if (!sorted && k > 1u) {
        qsort(e, k, sizeof *e, cmp_fn);
        u = 0;
        for (size_t i = 0; i < k; i++) if (!u || e[u - 1u].key != e[i].key) e[u++] = e[i];
    }
    *out_table = 1; *out_dead = dead; *out_ent = e; *out_n = u;
    return 1;
}

static inline size_t vka_put_hex16(char *dst, uint64_t v) {
    static const char hx[] = "0123456789abcdef";
    for (int s = 60; s >= 0; s -= 4) *dst++ = hx[(v >> (unsigned)s) & 15u];
    return 16u;
}

static inline size_t vka_put_dec(char *dst, uint64_t v) {
    char tmp[24];
    size_t n = 0;
    do { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v);
    for (size_t i = 0; i < n; i++) dst[i] = tmp[n - 1u - i];
    return n;
}

static inline int vka_pck_is_strict_asc(const vka_pck_ent_w34 *e, size_t n) {
    for (size_t i = 1; i < n; i++) if (e[i].key <= e[i - 1u].key) return 0;
    return 1;
}

static inline size_t vka_pck_merge(const vka_pck_ent_w34 *k, size_t nk,
                                   const vka_pck_ent_w34 *r, size_t nr,
                                   vka_pck_ent_w34 *all, int fault_mode) {
    size_t i = 0, j = 0, u = 0;
    while (i < nk || j < nr) {
        vka_pck_ent_w34 e;
        if (fault_mode == 2 && i < nk && (j >= nr || k[i].key <= r[j].key)) {
            e = k[i++];
            while (j < nr && r[j].key == e.key) j++;
        } else if (j < nr && (i >= nk || r[j].key <= k[i].key)) {
            e = r[j++];
            while (i < nk && k[i].key == e.key) i++;
        } else {
            e = k[i++];
        }
        if (e.hash && (!u || all[u - 1u].key != e.key)) all[u++] = e;
    }
    return u;
}

static inline char *vka_pck_emit_text(uint64_t lib, uint64_t dead,
                                      const vka_pck_ent_w34 *all, size_t u, size_t *len) {
    if (u > (SIZE_MAX - 96u) / 34u) return NULL;
    char *t = (char *)malloc(96u + u * 34u);
    if (!t) return NULL;
    size_t o = vka_put_hex16(t, lib);
    t[o++] = '\n';
    memcpy(t + o, "keep 1 dead ", 12u); o += 12u;
    o += vka_put_dec(t + o, dead);
    memcpy(t + o, " shaders ", 9u); o += 9u;
    o += vka_put_dec(t + o, (uint64_t)u);
    t[o++] = '\n';
    for (size_t q = 0; q < u; q++) {
        o += vka_put_hex16(t + o, all[q].key);
        t[o++] = ' ';
        o += vka_put_hex16(t + o, all[q].hash);
        t[o++] = '\n';
    }
    t[o] = '\0'; *len = o;
    return t;
}

static inline char *vka_pck_format_fast(uint64_t lib, uint64_t dead,
                                        const void *keep_raw, size_t nk,
                                        const void *run_raw, size_t nr, size_t *len,
                                        int (*cmp_fn)(const void *, const void *), int fault_mode) {
    const vka_pck_ent_w34 *keep = (const vka_pck_ent_w34 *)keep_raw;
    const vka_pck_ent_w34 *run = (const vka_pck_ent_w34 *)run_raw;
    vka_pck_ent_w34 *k_own = NULL;
    const vka_pck_ent_w34 *k_ref = keep;
    if (nk && keep && !vka_pck_is_strict_asc(keep, nk)) {
        k_own = (vka_pck_ent_w34 *)malloc(nk * sizeof *k_own);
        if (!k_own) return NULL;
        memcpy(k_own, keep, nk * sizeof *k_own);
        qsort(k_own, nk, sizeof *k_own, cmp_fn);
        k_ref = k_own;
    }
    vka_pck_ent_w34 *r = (vka_pck_ent_w34 *)malloc((nr ? nr : 1u) * sizeof *r);
    vka_pck_ent_w34 *all = (vka_pck_ent_w34 *)malloc((nk + nr ? nk + nr : 1u) * sizeof *all);
    if (!r || !all) { free(k_own); free(r); free(all); return NULL; }
    if (nr && run) { memcpy(r, run, nr * sizeof *r); qsort(r, nr, sizeof *r, cmp_fn); }
    size_t u = vka_pck_merge(k_ref, nk, r, nr, all, fault_mode);
    free(k_own); free(r);
    char *t = vka_pck_emit_text(lib, dead, all, u, len);
    free(all);
    return t;
}

static inline int vka_pw_rec_ok_fast(const pwrec *r, const uint8_t *kb, int fault_mode) {
    if (r->kind > VKA_PW_MESH || !r->key[0] || r->klen > sizeof(vka_gkey)) return 0;
    if (r->kind == VKA_PW_CS) return r->klen == 0;
    if (r->kind == VKA_PW_MESH && (!r->key[1] || r->key[3] || r->key[4] || r->inst[0] || r->inst[1])) return 0;
    size_t pfx = offsetof(vka_gkey, va);
    if (r->klen < pfx) return 0;
    vka_gkey k;
    size_t cpy = r->klen == pfx ? pfx : sizeof(vka_gkey);
    if (cpy > r->klen) memset(&k, 0, cpy);
    memcpy(&k, kb, r->klen);
    if (k.ncolor > 8 || k.fs_variant > VKA_PW_FS_VARIANT_MAX || (k.samples != 1 && k.samples != 2 && k.samples != 4 && k.samples != 8)) return 0;
    if (k.topo > VKA_PW_TOPO_MAX || k.poly_mode > VKA_PW_POLY_MAX || k.alpha_func > 8 || k.logic_op > VKA_PW_LOGIC_MAX) return 0;
    if (k.depth_clamp > 1 || k.a2c > 1 || k.logic_en > 1 || k.gl_depth > 1 || k.gs_fx > 1 || k.vin_static > 1) return 0;
    for (int i = 0; i < 8; i++) {
        int bad_op = (fault_mode != 3) && (k.att[i].op > VKA_PW_BLENDOP_MAX || k.att[i].opa > VKA_PW_BLENDOP_MAX);
        if (k.att[i].en > 1 || k.att[i].src > VKA_PW_BLENDF_MAX || k.att[i].dst > VKA_PW_BLENDF_MAX ||
            k.att[i].srca > VKA_PW_BLENDF_MAX || k.att[i].dsta > VKA_PW_BLENDF_MAX || bad_op) return 0;
    }
    if (k.vin_static && r->klen > pfx) {
        for (int i = 0; i < 32; i++) if (k.va[i].used > 1 || (k.va[i].used && k.va[i].binding > 32)) return 0;
        for (int i = 0; i < 33; i++) if (k.vb[i].used > 1) return 0;
    }
    return 1;
}

static inline int vka_pw_next_fast(const uint8_t *d, size_t len, size_t *o,
                                   pwrec *r, const uint8_t **kb, int *ok, int fault_mode) {
    if (!d || *o + sizeof(pwrec) > len) return 0;
    memcpy(r, d + *o, sizeof *r);
    if (r->klen > sizeof(vka_gkey) || *o + vka_pw_size(r) > len) return 0;
    *kb = d + *o + sizeof *r;
    *ok = vka_pw_rec_ok_fast(r, *kb, fault_mode);
    *o += vka_pw_size(r);
    return 1;
}

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t compute;
    _Atomic uint64_t h;
    _Atomic uintptr_t sh[8];
    _Atomic uint64_t val_w[4];
} vka_lshort_slot;

typedef struct { vka_lshort_slot s[256]; } vka_lshort_cache;

static inline int vka_lshort_sh_eq(const vka_lshort_slot *s, const uintptr_t b[8]) {
    for (int i = 0; i < 8; i++) {
        if (atomic_load_explicit(&s->sh[i], memory_order_relaxed) != b[i]) return 0;
    }
    return 1;
}

static inline int vka_lshort_get(const vka_lshort_cache *c, uint64_t h,
                                 const uintptr_t sh[8], uint32_t compute,
                                 void *out_val, size_t val_sz, int fault_mode) {
    const vka_lshort_slot *s = &c->s[(uint32_t)(h ^ (h >> 32)) & 255u];
    uint32_t s1 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (!s1 || (s1 & 1u)) return 0;
    if (atomic_load_explicit(&s->h, memory_order_relaxed) != h) return 0;
    if ((fault_mode != 4 && atomic_load_explicit(&s->compute, memory_order_relaxed) != compute) ||
        !vka_lshort_sh_eq(s, sh)) return 0;
    uint64_t w[4];
    for (int i = 0; i < 4; i++) w[i] = atomic_load_explicit(&s->val_w[i], memory_order_relaxed);
    uint32_t s2 = atomic_load_explicit(&s->seq, memory_order_acquire);
    if (s1 != s2) return 0;
    memcpy(out_val, w, val_sz);
    return 1;
}

static inline void vka_lshort_put(vka_lshort_cache *c, uint64_t h,
                                  const uintptr_t sh[8], uint32_t compute,
                                  const void *val, size_t val_sz) {
    vka_lshort_slot *s = &c->s[(uint32_t)(h ^ (h >> 32)) & 255u];
    uint64_t w[4] = {0};
    memcpy(w, val, val_sz);
    uint32_t v = atomic_load_explicit(&s->seq, memory_order_relaxed);
    atomic_store_explicit(&s->seq, v + 1u, memory_order_release);
    atomic_store_explicit(&s->compute, compute, memory_order_relaxed);
    atomic_store_explicit(&s->h, h, memory_order_relaxed);
    for (int i = 0; i < 8; i++) atomic_store_explicit(&s->sh[i], sh[i], memory_order_relaxed);
    for (int i = 0; i < 4; i++) atomic_store_explicit(&s->val_w[i], w[i], memory_order_relaxed);
    atomic_store_explicit(&s->seq, v + 2u, memory_order_release);
}

static inline int vka_lkey_eq_fast(const void *a, const void *b, uint32_t n,
                                   size_t active_bytes, int fault_mode) {
    if (fault_mode == 5) {
        uint32_t an = 0;
        memcpy(&an, a, sizeof an);
        return an == n;
    }
    return memcmp(a, b, active_bytes) == 0;
}
