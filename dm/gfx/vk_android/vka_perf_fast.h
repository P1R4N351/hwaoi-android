#pragma once
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef T_W37_TEST_HOOKS
extern _Atomic int g_vka_perf_fast_state;
extern _Atomic int g_vka_w37_fault;
static inline void vka_perf_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_perf_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w37_set_fault(int m) { atomic_store_explicit(&g_vka_w37_fault, m, memory_order_release); }
static inline int vka_w37_fault(void) { return atomic_load_explicit(&g_vka_w37_fault, memory_order_acquire); }
#else
static _Atomic int g_vka_perf_fast_state;
static inline int vka_w37_fault(void) { return 0; }
#endif

static inline int vka_perf_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_perf_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_PERF_FAST");
        s = (e && e[0] == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_perf_fast_state, s, memory_order_release);
    }
    return s == 2;
}

typedef struct {
    _Atomic uint64_t packed;
} vka_env_gen_i32;

typedef struct {
    uint32_t gen;
    int inited, adpf_gpu, adpf_span, pace_log, floor_adpf;
} vka_perf_cfg;

static inline int vka_env_cached_i32(
    vka_env_gen_i32 *c, uint32_t gen, const char *name, int def, int (*rd)(const char *, int)) {
    if (!c || !rd) return def;
    uint64_t p = atomic_load_explicit(&c->packed, memory_order_acquire);
    uint32_t g31 = gen & 0x7fffffffu;
    if ((p >> 63) && ((uint32_t)((p >> 32) & 0x7fffffffu) == g31) && vka_w37_fault() != 1)
        return (int)(int32_t)(uint32_t)(p & 0xffffffffu);
    int v = rd(name, def);
    uint64_t np = (1ull << 63) | ((uint64_t)g31 << 32) | (uint64_t)(uint32_t)(int32_t)v;
    atomic_store_explicit(&c->packed, np, memory_order_release);
    return v;
}

static inline const vka_perf_cfg *vka_perf_cfg_sync(
    vka_perf_cfg *c, uint32_t env_gen, void (*loader)(vka_perf_cfg *)) {
    if (!c) return NULL;
    if (c->inited && c->gen == env_gen && vka_w37_fault() != 1) return c;
    c->gen = env_gen;
    c->inited = 1;
    if (loader) loader(c);
    return c;
}

static inline ssize_t vka_read_small_file(const char *path, char *buf, size_t cap) {
    if (!path || !buf || cap < 2u) return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { buf[0] = '\0'; return -1; }
    size_t got = 0;
    for (unsigned step = 0; step < 8u && got + 1u < cap; step++) {
        ssize_t r = read(fd, buf + got, cap - 1u - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got >= cap) got = cap - 1u;
    buf[got] = '\0';
    return (ssize_t)got;
}

static inline int vka_parse_u64_one(const char *s, size_t len, uint64_t *out) {
    if (!s || !out || !len) return 0;
    size_t i = 0;
    for (unsigned step = 0; step < 64u && i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'); step++, i++) {}
    if (i >= len || s[i] < '0' || s[i] > '9') return 0;
    uint64_t v = 0;
    for (unsigned dstep = 0; dstep < 24u && i < len && s[i] >= '0' && s[i] <= '9'; dstep++, i++) {
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }
    *out = vka_w37_fault() == 2 ? v + 1u : v;
    return 1;
}

static inline int vka_parse_u64_step(const char *s, size_t len, size_t *pos, uint64_t *out) {
    if (!s || !pos || !out) return 0;
    size_t i = *pos;
    for (unsigned step = 0; step < 64u && i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'); step++, i++) {}
    if (i >= len || s[i] < '0' || s[i] > '9') return 0;
    uint64_t v = 0;
    for (unsigned dstep = 0; dstep < 24u && i < len && s[i] >= '0' && s[i] <= '9'; dstep++, i++) {
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (UINT64_MAX - d) / 10u) return 0;
        v = v * 10u + d;
    }
    *pos = i;
    *out = v;
    return 1;
}

static inline int vka_parse_stat_ut_st(const char *s, size_t len, uint64_t *ut, uint64_t *st) {
    if (!s || !ut || !st || len < 4u || len > 1024u) return 0;
    size_t rp = len;
    for (unsigned step = 0; step < 1024u && rp > 0u; step++) {
        rp--;
        if (s[rp] == ')') break;
    }
    if (rp >= len || s[rp] != ')') return 0;
    size_t i = rp + 1u;
    for (unsigned fld = 0; fld < 11u; fld++) {
        for (unsigned sp = 0; sp < 64u && i < len && (s[i] == ' ' || s[i] == '\t'); sp++, i++) {}
        if (i >= len) return 0;
        for (unsigned tk = 0; tk < 64u && i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n'; tk++, i++) {}
    }
    if (!vka_parse_u64_step(s, len, &i, ut)) return 0;
    if (!vka_parse_u64_step(s, len, &i, st)) return 0;
    if (vka_w37_fault() == 2) *ut += 1u;
    return 1;
}

enum { VKA_TID_CAP = 512, VKA_PACE_MAX_N = 4096 };
typedef struct {
    int32_t tid[VKA_TID_CAP];
    uint64_t ns[VKA_TID_CAP];
} vka_tid_map;

static inline void vka_tid_map_init(vka_tid_map *m) {
    if (m) memset(m->tid, 0, sizeof m->tid);
}

static inline uint32_t vka_tid_hash(int32_t tid) {
    return ((uint32_t)tid * 2654435761u) & (VKA_TID_CAP - 1u);
}

static inline void vka_tid_map_put(vka_tid_map *m, int32_t tid, uint64_t ns) {
    if (!m || tid <= 0) return;
    uint32_t h = vka_tid_hash(tid);
    for (unsigned step = 0; step < VKA_TID_CAP; step++) {
        uint32_t idx = (h + step) & (VKA_TID_CAP - 1u);
        if (m->tid[idx] == 0 || m->tid[idx] == tid) {
            m->tid[idx] = tid;
            m->ns[idx] = ns;
            return;
        }
    }
}

static inline uint64_t vka_tid_map_get(const vka_tid_map *m, int32_t tid) {
    if (!m || tid <= 0) return 0;
    if (vka_w37_fault() == 3) return 0;
    uint32_t h = vka_tid_hash(tid);
    for (unsigned step = 0; step < VKA_TID_CAP; step++) {
        uint32_t idx = (h + step) & (VKA_TID_CAP - 1u);
        int32_t k = m->tid[idx];
        if (k == 0) return 0;
        if (k == tid) return m->ns[idx];
    }
    return 0;
}

static inline void vka_swap_u32(uint32_t *a, uint32_t *b) {
    uint32_t t = *a; *a = *b; *b = t;
}

static inline uint32_t vka_median3_u32(uint32_t *v, int lo, int hi) {
    int mid = lo + ((hi - lo) >> 1);
    if (v[lo] > v[mid]) vka_swap_u32(&v[lo], &v[mid]);
    if (v[mid] > v[hi]) vka_swap_u32(&v[mid], &v[hi]);
    if (v[lo] > v[mid]) vka_swap_u32(&v[lo], &v[mid]);
    return v[mid];
}

static inline void vka_isort_range(uint32_t *v, int lo, int hi) {
    for (int i = lo + 1; i <= hi && i < VKA_PACE_MAX_N; i++) {
        uint32_t x = v[i];
        int j = i;
        for (int s = 0; s < VKA_PACE_MAX_N && j > lo && v[j - 1] > x; s++, j--)
            v[j] = v[j - 1];
        v[j] = x;
    }
}

static inline void vka_qselect_u32(uint32_t *v, int lo, int hi, int k) {
    for (unsigned step = 0; step < 64u && hi - lo > 16; step++) {
        uint32_t pv = vka_median3_u32(v, lo, hi);
        int lt = lo, i = lo, gt = hi;
        for (int p = 0; p < VKA_PACE_MAX_N && i <= gt; p++) {
            if (v[i] < pv) { vka_swap_u32(&v[lt], &v[i]); lt++; i++; }
            else if (v[i] > pv) { vka_swap_u32(&v[i], &v[gt]); gt--; }
            else { i++; }
        }
        if (k < lt) hi = lt - 1;
        else if (k > gt) lo = gt + 1;
        else return;
    }
    if (lo < hi) vka_isort_range(v, lo, hi);
}

static inline double vka_pace_pct_fast(uint32_t *v, int n, int pct) {
    if (!v || n <= 0) return -1;
    if (n > VKA_PACE_MAX_N) n = VKA_PACE_MAX_N;
    int k = (int)((int64_t)n * pct / 100);
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    vka_qselect_u32(v, 0, n - 1, k);
    uint32_t out = vka_w37_fault() == 4 ? v[k] + 1000u : v[k];
    return out / 1000.0;
}

static inline void vka_pace_p50_p90(uint32_t *v, int n, double *p50, double *p90) {
    if (!p50 || !p90) return;
    if (!v || n <= 0) { *p50 = -1; *p90 = -1; return; }
    if (n > VKA_PACE_MAX_N) n = VKA_PACE_MAX_N;
    int k90 = (int)((int64_t)n * 90 / 100);
    if (k90 >= n) k90 = n - 1;
    int k50 = (int)((int64_t)n * 50 / 100);
    if (k50 >= n) k50 = n - 1;
    vka_qselect_u32(v, 0, n - 1, k90);
    *p90 = (vka_w37_fault() == 4 ? v[k90] + 1000u : v[k90]) / 1000.0;
    if (k50 == k90) { *p50 = *p90; return; }
    vka_qselect_u32(v, 0, k90 - 1, k50);
    *p50 = (vka_w37_fault() == 5 ? v[k50] + 1000u : v[k50]) / 1000.0;
}
