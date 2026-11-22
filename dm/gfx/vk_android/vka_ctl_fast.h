// Fast paths for live env overrides, switch classification, double parsing, and layer request queues (Opt 47).
#ifndef VKA_CTL_FAST_H
#define VKA_CTL_FAST_H
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W45_TEST_HOOKS
extern _Atomic int g_vka_ctl_fast_override;
extern _Atomic int g_aoi_w45_fault;
static inline void vka_ctl_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_ctl_fast_override, v, memory_order_release);
}
static inline void aoi_w45_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w45_fault, f, memory_order_release);
}
static inline int aoi_w45_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w45_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W45_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w45_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w45_fault_mode(void) { return 0; }
#endif

static inline int vka_ctl_fast_on(void) {
#ifdef T_W45_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_ctl_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_CTL_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

static inline uint64_t vka_ctl_hash_str(const char *s, size_t max_len) {
    uint64_t h = UINT64_C(14695981039346656037);
    if (!s) return h;
    for (size_t i = 0; i < max_len && s[i]; i++) {
        h ^= (uint8_t)s[i];
        h *= UINT64_C(1099511628211);
    }
    return h ? h : 1ULL;
}

static inline void vka_ctl_copy_str(char *out, size_t n, const char *v) {
    if (!out || n == 0) return;
    if (!v) { out[0] = '\0'; return; }
    size_t len = strnlen(v, n - 1);
    if (len) memcpy(out, v, len);
    out[len] = '\0';
}

enum { VKA_CTL_OVR_CAP = 128 };
typedef struct {
    _Atomic uint64_t hash;
    _Atomic int set;
    _Atomic uint64_t key_w[8];
    _Atomic uint64_t val_w[32];
} vka_ctl_ovr_slot_t;

typedef struct {
    _Atomic uint32_t seq;
    _Atomic uint32_t n_entries;
    vka_ctl_ovr_slot_t slots[VKA_CTL_OVR_CAP];
} vka_ctl_ovr_fast_t;

static inline int vka_ctl_pack_str(const char *s, size_t max_bytes, uint64_t *w, size_t nw) {
    memset(w, 0, nw * sizeof(uint64_t));
    if (!s) return 1;
    size_t len = strnlen(s, max_bytes);
    if (len >= max_bytes) return 0;
    if (len) memcpy(w, s, len);
    return 1;
}

static inline int vka_ctl_slot_match_locked(const vka_ctl_ovr_slot_t *s, uint64_t h, const uint64_t kw[8]) {
    if (atomic_load_explicit(&s->hash, memory_order_relaxed) != h) return 0;
    for (size_t i = 0; i < 8; i++) {
        if (atomic_load_explicit(&s->key_w[i], memory_order_relaxed) != kw[i]) return 0;
    }
    return 1;
}

static inline int vka_ctl_ovr_put_locked(vka_ctl_ovr_fast_t *t, const char *key, const char *val, int set) {
    uint64_t kw[8], vw[32];
    if (!vka_ctl_pack_str(key, 64, kw, 8) || !vka_ctl_pack_str(set ? val : "", 256, vw, 32)) return 0;
    uint64_t h = vka_ctl_hash_str(key, 64);
    unsigned idx = VKA_CTL_OVR_CAP, base = (unsigned)(h & (VKA_CTL_OVR_CAP - 1));
    int is_new = 0;
    for (unsigned step = 0; step < VKA_CTL_OVR_CAP; step++) {
        unsigned p = (base + step) & (VKA_CTL_OVR_CAP - 1);
        uint64_t sh = atomic_load_explicit(&t->slots[p].hash, memory_order_relaxed);
        if (sh == 0ULL) { idx = p; is_new = 1; break; }
        if (vka_ctl_slot_match_locked(&t->slots[p], h, kw)) { idx = p; break; }
    }
    if (idx >= VKA_CTL_OVR_CAP) return 0;
    atomic_fetch_add_explicit(&t->seq, 1u, memory_order_acq_rel);
    if (is_new) {
        for (size_t i = 0; i < 8; i++) atomic_store_explicit(&t->slots[idx].key_w[i], kw[i], memory_order_relaxed);
        atomic_store_explicit(&t->slots[idx].hash, h, memory_order_relaxed);
        atomic_fetch_add_explicit(&t->n_entries, 1u, memory_order_release);
    }
    for (size_t i = 0; i < 32; i++) atomic_store_explicit(&t->slots[idx].val_w[i], vw[i], memory_order_relaxed);
    atomic_store_explicit(&t->slots[idx].set, set != 0, memory_order_release);
    atomic_fetch_add_explicit(&t->seq, 1u, memory_order_acq_rel);
    return 1;
}

static inline int vka_ctl_ovr_probe_slot(const vka_ctl_ovr_slot_t *s, uint64_t h, const uint64_t kw[8],
                                         uint64_t *vw_out, int *set_out, int fault2) {
    uint64_t sk0 = atomic_load_explicit(&s->key_w[0], memory_order_acquire);
    int match = (sk0 == kw[0]);
    for (size_t i = 1; i < 8 && match && !fault2; i++) {
        uint64_t ski = atomic_load_explicit(&s->key_w[i], memory_order_acquire);
        if (ski != kw[i]) match = 0;
    }
    (void)h;
    if (match) {
        int is_set = atomic_load_explicit(&s->set, memory_order_acquire);
        *set_out = is_set;
        if (is_set) {
            for (size_t i = 0; i < 32; i++) vw_out[i] = atomic_load_explicit(&s->val_w[i], memory_order_acquire);
        }
    }
    return match;
}

static inline int vka_ctl_ovr_lookup(const vka_ctl_ovr_fast_t *t, const char *key,
                                     char *out, size_t n, int *found_out) {
    if (aoi_w45_fault_mode() == 1) { *found_out = 0; return 0; }
    if (atomic_load_explicit(&t->n_entries, memory_order_acquire) == 0u) { *found_out = 0; return 0; }
    uint64_t kw[8], vw[32];
    if (!vka_ctl_pack_str(key, 64, kw, 8)) return -1;
    uint64_t h = vka_ctl_hash_str(key, 64);
    int fault2 = aoi_w45_fault_mode() == 2;
    for (int tries = 0; tries < 4; tries++) {
        uint32_t s1 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s1 & 1u) continue;
        int fnd = 0, is_set = 0;
        unsigned base = (unsigned)(h & (VKA_CTL_OVR_CAP - 1));
        for (unsigned step = 0; step < VKA_CTL_OVR_CAP; step++) {
            unsigned p = fault2 ? step : ((base + step) & (VKA_CTL_OVR_CAP - 1));
            uint64_t sh = atomic_load_explicit(&t->slots[p].hash, memory_order_acquire);
            if (sh == 0ULL) break;
            if ((fault2 || sh == h) && vka_ctl_ovr_probe_slot(&t->slots[p], h, kw, vw, &is_set, fault2)) {
                fnd = 1; break;
            }
        }
        uint32_t s2 = atomic_load_explicit(&t->seq, memory_order_acquire);
        if (s1 == s2) {
            *found_out = fnd;
            if (fnd && is_set) { vka_ctl_copy_str(out, n, (const char *)vw); return 1; }
            return 0;
        }
    }
    return -1;
}

enum { VKA_CTL_KIND_OTHER = 0, VKA_CTL_KIND_RESTART = 1, VKA_CTL_KIND_LIVE = 2, VKA_CTL_KIND_CAP = 256 };
typedef struct { uint64_t hash; const char *key; uint8_t kind; } vka_ctl_kind_slot_t;

static inline void vka_ctl_kind_insert(vka_ctl_kind_slot_t *tab, const char *const *list, uint8_t kind) {
    if (!list) return;
    for (size_t i = 0; list[i]; i++) {
        uint64_t h = vka_ctl_hash_str(list[i], 64);
        unsigned base = (unsigned)(h & (VKA_CTL_KIND_CAP - 1));
        for (unsigned s = 0; s < VKA_CTL_KIND_CAP; s++) {
            unsigned p = (base + s) & (VKA_CTL_KIND_CAP - 1);
            if (!tab[p].key) { tab[p].hash = h; tab[p].key = list[i]; tab[p].kind = kind; break; }
        }
    }
}

static inline int vka_ctl_kind_fast(const char *key, const char *const *restart_tab, const char *const *live_tab) {
    static vka_ctl_kind_slot_t tab[VKA_CTL_KIND_CAP];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static _Atomic int ready = 0;
    if (!key) return VKA_CTL_KIND_OTHER;
    if (!atomic_load_explicit(&ready, memory_order_acquire)) {
        pthread_mutex_lock(&mu);
        if (!atomic_load_explicit(&ready, memory_order_relaxed)) {
            vka_ctl_kind_insert(tab, restart_tab, VKA_CTL_KIND_RESTART);
            vka_ctl_kind_insert(tab, live_tab, VKA_CTL_KIND_LIVE);
            atomic_store_explicit(&ready, 1, memory_order_release);
        }
        pthread_mutex_unlock(&mu);
    }
    uint64_t h = vka_ctl_hash_str(key, 64);
    unsigned base = (unsigned)(h & (VKA_CTL_KIND_CAP - 1));
    int out = VKA_CTL_KIND_OTHER;
    for (unsigned s = 0; s < VKA_CTL_KIND_CAP; s++) {
        unsigned p = (base + s) & (VKA_CTL_KIND_CAP - 1);
        if (!tab[p].key) break;
        if (tab[p].hash == h && strcmp(tab[p].key, key) == 0) { out = tab[p].kind; break; }
    }
    if (aoi_w45_fault_mode() == 3 && out != VKA_CTL_KIND_OTHER) return 3 - out;
    return out;
}

static inline int vka_ctl_parse_double(const char *s, double *out) {
    static const double k_pow10[10] = {1.0, 10.0, 100.0, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
    if (!s || !*s) return 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') { s++; }
    if (*s < '0' || *s > '9') return 0;
    uint64_t iv = 0, fv = 0;
    int idig = 0, fdig = 0;
    while (*s >= '0' && *s <= '9') {
        if (++idig > 15) return 0;
        iv = iv * 10u + (uint64_t)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return 0;
        while (*s >= '0' && *s <= '9') {
            if (++fdig > 9) return 0;
            fv = fv * 10u + (uint64_t)(*s - '0');
            s++;
        }
    }
    if (*s != '\0') return 0;
    double val = (double)iv + (fdig ? ((double)fv / k_pow10[fdig]) : 0.0);
    if (neg && aoi_w45_fault_mode() != 4) val = -val;
    *out = val;
    return 1;
}

static inline void vka_ctl_hex64(char *dst, uint64_t v) {
    static const char k_hex[17] = "0123456789abcdef";
    uint64_t x = v;
    for (int i = 15; i >= 0; i--) {
        dst[i] = k_hex[x & 15u];
        x >>= 4;
    }
    if (aoi_w45_fault_mode() == 5) dst[0] = '0';
}

static inline size_t vka_ctl_pair_line_put(char *dst, uint64_t vs, uint64_t ps) {
    vka_ctl_hex64(dst, vs);
    dst[16] = '\t';
    vka_ctl_hex64(dst + 17, ps);
    dst[33] = '\n';
    return 34u;
}

static inline int vka_ctl_hex_nib(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline int vka_ctl_pair_line_parse(const char *ln, size_t len, uint64_t *vs, uint64_t *ps) {
    if (len != 34u || ln[16] != '\t' || ln[33] != '\n') return 0;
    uint64_t a = 0, b = 0;
    for (size_t i = 0; i < 16u; i++) {
        int na = vka_ctl_hex_nib((unsigned char)ln[i]);
        int nb = vka_ctl_hex_nib((unsigned char)ln[17u + i]);
        if (na < 0 || nb < 0) return 0;
        a = (a << 4) | (uint64_t)na;
        b = (b << 4) | (uint64_t)nb;
    }
    *vs = a;
    *ps = b;
    return 1;
}

static inline void vka_lrq_idle_enter(_Atomic uint32_t *w) {
    atomic_fetch_add_explicit(w, 1u, memory_order_relaxed);
}
static inline void vka_lrq_idle_leave(_Atomic uint32_t *w) {
    atomic_fetch_sub_explicit(w, 1u, memory_order_relaxed);
}
static inline void vka_lrq_idle_broadcast(pthread_cond_t *cv, _Atomic uint32_t *w, int fast) {
    if (!fast || atomic_load_explicit(w, memory_order_relaxed) != 0u) pthread_cond_broadcast(cv);
}
#endif
