/* Lock-free DM_FS_LOOKUP_FAST mount table, path splitter, and monotonic RomFS/NUP cache (Opt 29). */
#pragma once
#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FS_LOOKUP_MNT_WAYS 1024u
#define FS_LOOKUP_MNT_PROBE 32u
#define FS_LOOKUP_MNT_NAME_CAP 32u
#define FS_LOOKUP_PATH_WAYS 2048u
#define FS_LOOKUP_PATH_PROBE 8u
#define FS_LOOKUP_PATH_CAP 128u

static inline int fs_lookup_fast_enabled(void) {
    static _Atomic int on = -1;
    int v = atomic_load_explicit(&on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_FS_LOOKUP_FAST");
        v = (e && e[0] == '0' && e[1] == '\0') ? 0 : 1;
        atomic_store_explicit(&on, v, memory_order_release);
    }
    return v;
}

#ifdef T_W27_FS_TEST_HOOKS
static _Atomic int g_fs_lookup_override = -1;

static inline void fs_lookup_reset_for_test(int en) {
    atomic_store_explicit(&g_fs_lookup_override, en, memory_order_release);
}

static inline int fs_lookup_fast_active(void) {
    int ov = atomic_load_explicit(&g_fs_lookup_override, memory_order_acquire);
    return ov >= 0 ? ov : fs_lookup_fast_enabled();
}

static inline int fs_lookup_w27_fault(void) {
    static _Atomic int gf = -1;
    int v = atomic_load_explicit(&gf, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("T_W27_FAULT");
        v = e ? atoi(e) : 0;
        atomic_store_explicit(&gf, v, memory_order_release);
    }
    return v;
}
#else
static inline int fs_lookup_fast_active(void) { return fs_lookup_fast_enabled(); }
static inline int fs_lookup_w27_fault(void) { return 0; }
#endif

static inline uint64_t fs_lookup_hash64(const char *s, size_t n) {
    uint64_t h = 14695981039346656037ull ^ (uint64_t)n;
    size_t i = 0;
    for (; i + 8u <= n && i < 4096u; i += 8u) {
        uint64_t w = 0;
        memcpy(&w, s + i, 8u);
        h = (h ^ w) * 0xbf58476d1ce4e5b9ull;
        h ^= h >> 31;
    }
    if (i < n && i < 4096u) {
        uint64_t tail = 0;
        memcpy(&tail, s + i, n - i < 8u ? n - i : 8u);
        h = (h ^ tail) * 0x94d049bb133111ebull;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}

static inline int fs_lookup_split_path(const char *p, char *fwd, size_t cap,
                                       const char **mname, size_t *mlen,
                                       const char **rest, int *has_dot) {
    if (!p) return 0;
    size_t len = 0, colon = (size_t)-1;
    int bs = 0, dot = 0;
    uint8_t prev = 0;
    for (; len < 16384u; len++) {
        uint8_t ch = (uint8_t)p[len];
        if (!ch) break;
        if (ch == '\\') bs = 1;
        else if (ch == ':' && colon == (size_t)-1) { colon = len; prev = 0; continue; }
        if (colon != (size_t)-1) {
            uint8_t norm = ch == '\\' ? (uint8_t)'/' : ch;
            if (prev == '/' && norm == '.') dot = 1;
            prev = norm;
        }
    }
    if (p[len] || colon == (size_t)-1) return 0;
    if (bs && len < cap) {
        for (size_t i = 0; i < len; i++) fwd[i] = p[i] == '\\' ? '/' : p[i];
        fwd[len] = 0;
        *mname = fwd;
        *rest = fwd + colon + 1u;
    } else {
        *mname = p;
        *rest = p + colon + 1u;
        dot = bs ? (strstr(*rest, "/.") != NULL) : dot;
    }
    *mlen = colon;
    *has_dot = dot;
    return 1;
}

typedef struct {
    _Atomic uint64_t state;
    uint64_t hash;
    uint16_t len;
    int16_t idx;
    char name[FS_LOOKUP_MNT_NAME_CAP];
} fs_lookup_mnt_slot;

typedef struct {
    _Atomic uint64_t epoch;
    fs_lookup_mnt_slot s[FS_LOOKUP_MNT_WAYS];
} fs_lookup_mnt_table;

static inline uint64_t fs_lookup_mnt_begin(fs_lookup_mnt_table *t) {
    return atomic_fetch_add_explicit(&t->epoch, 1ull, memory_order_acq_rel) + 1ull;
}

static inline void fs_lookup_mnt_insert(fs_lookup_mnt_table *t, uint64_t ep, const char *name, size_t ln, int idx) {
    if (!ep || !ln || ln >= FS_LOOKUP_MNT_NAME_CAP || idx < 0) return;
    uint64_t h = fs_lookup_hash64(name, ln);
    uint32_t pos = (uint32_t)(h & (FS_LOOKUP_MNT_WAYS - 1u));
    uint64_t pub = (ep << 2) | 2ull;
    for (uint32_t probe = 0; probe < FS_LOOKUP_MNT_PROBE; probe++) {
        fs_lookup_mnt_slot *s = &t->s[(pos + probe) & (FS_LOOKUP_MNT_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == pub && s->hash == h && s->len == (uint16_t)ln && !memcmp(s->name, name, ln)) return;
        if (st != 0ull) continue;
        uint64_t expected = 0ull;
        if (!atomic_compare_exchange_strong_explicit(&s->state, &expected, 1ull, memory_order_acq_rel, memory_order_relaxed))
            continue;
        s->hash = h;
        s->len = (uint16_t)ln;
        s->idx = (int16_t)idx;
        memcpy(s->name, name, ln);
        s->name[ln] = 0;
        atomic_store_explicit(&s->state, pub, memory_order_release);
        return;
    }
}

static inline int fs_lookup_mnt_find(const fs_lookup_mnt_table *t, const char *name, size_t ln, int *idx_out) {
    if (!fs_lookup_fast_active() || !ln || ln >= FS_LOOKUP_MNT_NAME_CAP) return 0;
    uint64_t ep = atomic_load_explicit(&t->epoch, memory_order_acquire);
    if (!ep) return 0;
    uint64_t h = fs_lookup_hash64(name, ln);
    uint32_t pos = (uint32_t)(h & (FS_LOOKUP_MNT_WAYS - 1u));
    int f = fs_lookup_w27_fault();
    for (uint32_t probe = 0; probe < FS_LOOKUP_MNT_PROBE; probe++) {
        const fs_lookup_mnt_slot *s = &t->s[(pos + probe) & (FS_LOOKUP_MNT_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == 0ull) return 0;
        if ((st & 3ull) != 2ull) continue;
        if (f != 2 && (st >> 2) != ep) continue;
        if (s->hash == h && s->len == (uint16_t)ln && !memcmp(s->name, name, ln)) {
            int idx = (int)s->idx;
            *idx_out = (f == 5) ? (idx + 1) : idx;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    _Atomic uint64_t state;
    uint64_t hash;
    uintptr_t img_ptr;
    int64_t entry;
    uint16_t mi;
    uint16_t len;
    int16_t err_no;
    char path[FS_LOOKUP_PATH_CAP];
} fs_lookup_path_slot;

typedef struct {
    _Atomic uint64_t epoch;
    fs_lookup_path_slot s[FS_LOOKUP_PATH_WAYS];
} fs_lookup_path_cache;

static inline void fs_lookup_path_bump(fs_lookup_path_cache *c) {
    atomic_fetch_add_explicit(&c->epoch, 1ull, memory_order_acq_rel);
}

static inline uint32_t fs_lookup_path_idx(uint32_t mi, uint64_t h) {
    return (uint32_t)((h ^ ((uint64_t)mi * 0x9e3779b97f4a7c15ull)) & (FS_LOOKUP_PATH_WAYS - 1u));
}

static inline int fs_lookup_path_get(const fs_lookup_path_cache *c, uint32_t mi, uintptr_t img_ptr,
                                       const char *rest, size_t len, int64_t *k_out, int *err_out) {
    if (!fs_lookup_fast_active() || len >= FS_LOOKUP_PATH_CAP) return 0;
    uint64_t ep = atomic_load_explicit(&c->epoch, memory_order_acquire);
    if (!ep) return 0;
    uint64_t h = fs_lookup_hash64(rest, len);
    uint32_t pos = fs_lookup_path_idx(mi, h);
    int f = fs_lookup_w27_fault();
    for (uint32_t probe = 0; probe < FS_LOOKUP_PATH_PROBE; probe++) {
        const fs_lookup_path_slot *s = &c->s[(pos + probe) & (FS_LOOKUP_PATH_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == 0ull) return 0;
        if ((st & 3ull) != 2ull) continue;
        if (f != 2 && (st >> 2) != ep) continue;
        if (s->img_ptr != img_ptr || s->mi != (uint16_t)mi) continue;
        int key_ok = (f == 4) ? 1 : (s->hash == h && s->len == (uint16_t)len && !memcmp(s->path, rest, len));
        if (key_ok) {
            int64_t sv = s->entry;
            int serr = (int)s->err_no;
            if (f == 1 && sv >= 0) sv += 1;
            if (f == 3 && sv < 0) { sv = 0; serr = 0; }
            *k_out = sv;
            if (err_out) *err_out = serr;
            return 1;
        }
    }
    return 0;
}

static inline void fs_lookup_path_put(fs_lookup_path_cache *c, uint32_t mi, uintptr_t img_ptr,
                                        const char *rest, size_t len, int64_t k, int err_no) {
    if (!fs_lookup_fast_active() || len >= FS_LOOKUP_PATH_CAP) return;
    uint64_t ep = atomic_load_explicit(&c->epoch, memory_order_acquire);
    if (!ep) ep = 1ull;
    uint64_t h = fs_lookup_hash64(rest, len);
    uint32_t pos = fs_lookup_path_idx(mi, h);
    uint64_t pub = (ep << 2) | 2ull;
    for (uint32_t probe = 0; probe < FS_LOOKUP_PATH_PROBE; probe++) {
        fs_lookup_path_slot *s = &c->s[(pos + probe) & (FS_LOOKUP_PATH_WAYS - 1u)];
        uint64_t st = atomic_load_explicit(&s->state, memory_order_acquire);
        if (st == pub && s->img_ptr == img_ptr && s->mi == (uint16_t)mi &&
            s->hash == h && s->len == (uint16_t)len && !memcmp(s->path, rest, len))
            return;
        if (st != 0ull) continue;
        uint64_t expected = 0ull;
        if (!atomic_compare_exchange_strong_explicit(&s->state, &expected, 1ull, memory_order_acq_rel, memory_order_relaxed))
            continue;
        s->hash = h;
        s->img_ptr = img_ptr;
        s->entry = k;
        s->mi = (uint16_t)mi;
        s->len = (uint16_t)len;
        s->err_no = (int16_t)err_no;
        memcpy(s->path, rest, len);
        s->path[len] = 0;
        atomic_store_explicit(&s->state, pub, memory_order_release);
        return;
    }
}
