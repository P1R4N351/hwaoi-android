// Unit benchmark & Delta #27 fault-injection harness for vka_shader_fast.h (DM_VK_SHADER_FAST=1) and vka_dset_fast.h (DM_VK_DSET_FAST=1).
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../vk_android/vka_shader_fast.h"
#include "../vk_android/vka_dset_fast.h"

int g_w8_fault = 0;

static uint64_t now_ns(void) {
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(rc == 0);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct sh_node { struct sh_node *next; uint64_t k0, k1, val; } sh_node;
typedef struct { uint64_t early, late; int kind; } pfb_row;

static uint64_t base_list_find(pthread_mutex_t *mu, sh_node *const *heads, uint32_t obj, uint64_t k0, uint64_t k1, uint64_t *misses) {
    pthread_mutex_lock(mu);
    for (const sh_node *n = heads[obj]; n; n = n->next) {
        if (n->k0 == k0 && n->k1 == k1) { uint64_t v = n->val; pthread_mutex_unlock(mu); return v; }
    }
    (*misses)++;
    pthread_mutex_unlock(mu);
    return 0;
}

static uint64_t base_pfb_find(pthread_mutex_t *mu, const pfb_row *rows, size_t n, uint64_t early, int a2c, uint64_t *misses) {
    pthread_mutex_lock(mu);
    for (size_t i = 0; i < n; i++) {
        if (rows[i].early == early) {
            uint64_t r = a2c ? rows[i].late : early;
            pthread_mutex_unlock(mu);
            return r;
        }
    }
    (*misses)++;
    pthread_mutex_unlock(mu);
    return early;
}

static int verify_shader_correctness(vka_sh_pair64_memo4 *pm, vka_sh_tex_memo4 *tm, vka_pfb_memo4 *pf) {
    memset(pm, 0, sizeof *pm); memset(tm, 0, sizeof *tm); memset(pf, 0, sizeof *pf);
    uint64_t k1_a = 0x22u, k1_b = 0;
    uint32_t b0 = vka_sh_pair_idx(2, 0x1000u, 0x11u, k1_a);
    for (uint64_t x = 0x100u; x < 0x10000u; x++) {
        if (vka_sh_pair_idx(2, 0x1000u, 0x11u, x) == b0) { k1_b = x; break; }
    }
    assert(k1_b != 0 && k1_b != k1_a);
    vka_sh_pair64_put(pm, 2, 0x1000u, 0x11u, k1_a, 0xAAAAu);
    vka_sh_pair64_put(pm, 2, 0x1000u, 0x11u, k1_b, 0xBBBBu);
    vka_sh_pair64_put(pm, 2, 0x1000u, 0x44u, 0x55u, 0u);   // cached negative miss
    uint64_t v = 99;
    if (!vka_sh_pair64_find(pm, 2, 0x1000u, 0x11u, k1_a, &v) || v != 0xAAAAu) { fprintf(stderr, "FAIL pair64 k1_a got %llu\n", (unsigned long long)v); return 1; }
    if (!vka_sh_pair64_find(pm, 2, 0x1000u, 0x11u, k1_b, &v) || v != 0xBBBBu) { fprintf(stderr, "FAIL pair64 k1_b got %llu\n", (unsigned long long)v); return 1; }
    if (!vka_sh_pair64_find(pm, 2, 0x1000u, 0x44u, 0x55u, &v) || v != 0u) { fprintf(stderr, "FAIL negative cache miss\n"); return 1; }

    vka_sh_tex_ent k1 = {0x2000u, 0x1u, 0x2u, 0, 1, 2, 3, 4, 0, 1, 1, {0}};
    vka_sh_tex_ent k2 = {0x2000u, 0x1u, 0x4u, 0, 1, 2, 3, 4, 0, 1, 1, {0}};
    vka_sh_tex_put(tm, &k1, 0xCC01u); vka_sh_tex_put(tm, &k2, 0xCC02u);
    if (!vka_sh_tex_find(tm, &k1, &v) || v != 0xCC01u) return 1;
    if (!vka_sh_tex_find(tm, &k2, &v) || v != 0xCC02u) return 1;

    vka_pfb_memo4_put(pf, 1, 0x9000u, 0x9001u, 2);
    uint64_t late = 0; int kind = 0;
    if (!vka_pfb_memo4_find(pf, 1, 0x9000u, &late, &kind) || late != 0x9001u || kind != 2) return 1;
    if (vka_pfb_memo4_find(pf, 2, 0x9000u, &late, &kind)) { fprintf(stderr, "FAIL pfb gen invalidation leaked old twin\n"); return 1; }
    return 0;
}

static int bench_shader_fast(void) {
    enum { NSH = 16, NW = 4, NPFB = 128, OPS = 400000 };
    static vka_sh_pair64_memo4 pm;
    static vka_sh_tex_memo4 tm;
    static vka_pfb_memo4 pf;
    if (verify_shader_correctness(&pm, &tm, &pf)) return 1;
    memset(&pm, 0, sizeof pm); memset(&tm, 0, sizeof tm); memset(&pf, 0, sizeof pf);

    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    sh_node nodes[NSH * NW];
    sh_node *heads[NSH] = {0};
    pfb_row pfb_rows[NPFB];
    for (int s = 0; s < NSH; s++) {
        for (int w = 0; w < NW; w++) {
            sh_node *nd = &nodes[s * NW + w];
            nd->k0 = (uint64_t)(w + 1);
            nd->k1 = (uint64_t)(s * 17 + w);
            nd->val = 0x100000ull + (uint64_t)(s * 16 + w);
            nd->next = heads[s];
            heads[s] = nd;
        }
    }
    for (int i = 0; i < NPFB; i++) pfb_rows[i] = (pfb_row){0x800000ull + (uint64_t)i, 0x900000ull + (uint64_t)i, 1 + (i & 1)};

    uint64_t base_sum = 0, base_miss = 0, t0 = now_ns();
    for (int i = 0; i < OPS; i++) {
        uint32_t s = (uint32_t)(i & (NSH - 1)), w = (uint32_t)((i >> 4) & (NW - 1));
        base_sum += base_list_find(&mu, heads, s, (uint64_t)(w + 1), (uint64_t)(s * 17 + w), &base_miss);
        base_sum += base_pfb_find(&mu, pfb_rows, NPFB, pfb_rows[i & (NPFB - 1)].early, 1, &base_miss);
        base_miss++;   // unmemoized mutex lookup counted as baseline miss
    }
    uint64_t dt_base = now_ns() - t0;

    uint64_t fast_sum = 0, fast_miss = 0, t1 = now_ns();
    for (int i = 0; i < OPS; i++) {
        uint32_t s = (uint32_t)(i & (NSH - 1)), w = (uint32_t)((i >> 4) & (NW - 1));
        uint64_t val = 0, late = 0; int kind = 0;
        if (!vka_sh_pair64_find(&pm, 2, (uintptr_t)&heads[s], (uint64_t)(w + 1), (uint64_t)(s * 17 + w), &val)) {
            val = base_list_find(&mu, heads, s, (uint64_t)(w + 1), (uint64_t)(s * 17 + w), &fast_miss);
            vka_sh_pair64_put(&pm, 2, (uintptr_t)&heads[s], (uint64_t)(w + 1), (uint64_t)(s * 17 + w), val);
            fast_miss++;
        }
        uint64_t e = pfb_rows[i & (NPFB - 1)].early;
        if (!vka_pfb_memo4_find(&pf, 7, e, &late, &kind)) {
            late = base_pfb_find(&mu, pfb_rows, NPFB, e, 1, &fast_miss);
            vka_pfb_memo4_put(&pf, 7, e, late, 1);
            fast_miss++;
        }
        fast_sum += val + late;
    }
    uint64_t dt_fast = now_ns() - t1;
    if (base_sum != fast_sum) { fprintf(stderr, "FAIL shader sum mismatch %llu != %llu\n", (unsigned long long)base_sum, (unsigned long long)fast_sum); return 1; }
    if (fast_miss != (uint64_t)(NSH * NW + NPFB)) { fprintf(stderr, "FAIL expected %d fast misses, got %llu\n", NSH * NW + NPFB, (unsigned long long)fast_miss); return 1; }
    printf("aoi_w8_shader_fast: ops=%d baseline_ms=%.2f fast_ms=%.2f speedup=%.2fx misses=%llu->%llu\n",
           OPS, dt_base / 1e6, dt_fast / 1e6, dt_fast ? (double)dt_base / (double)dt_fast : 0.0,
           (unsigned long long)base_miss, (unsigned long long)fast_miss);
    return 0;
}

static int verify_dset_correctness(vka_drv_key4 *km, vka_drv_ring4 *ring, vka_aref_memo4 *am, vka_dset_memo4 *dm, uint8_t scratch[][2560]) {
    memset(km, 0, sizeof *km); memset(ring, 0, sizeof *ring); memset(am, 0, sizeof *am); memset(dm, 0, sizeof *dm);
    uint64_t sk0[4] = {10, 1, 2, 0}, sk1[4] = {20, 1, 2, 0};
    uint8_t b0[2560], b1[2560];
    memset(b0, 0x11, sizeof b0); memset(b1, 0x22, sizeof b1);
    memcpy(scratch[0], b0, sizeof b0); memcpy(scratch[1], b1, sizeof b1);
    uint64_t h0[2], h1[2], rbuf = 0, roff = 0; uint8_t *rptr = NULL;
    vka_drv_key4_put(km, 1, 2, sk0, sizeof b0, 0xB0, 0);
    vka_drv_key4_put(km, 1, 2, sk1, sizeof b1, 0xB1, 2560);
    if (!vka_drv_key4_find(km, 1, 2, sk0, sizeof b0, &rbuf, &roff) || rbuf != 0xB0 || roff != 0) return 1;
    if (vka_drv_key4_find(km, 2, 2, sk0, sizeof b0, &rbuf, &roff)) {
        fprintf(stderr, "FAIL drv_key4 hit stale sub_gen=1 span\n"); return 1;
    }
    vka_hash_dual64(b0, sizeof b0, h0); vka_hash_dual64(b1, sizeof b1, h1);
    vka_drv_ring4_put(ring, 1, 2, sizeof b0, h0, 0xB0, scratch[0], 0);
    vka_drv_ring4_put(ring, 1, 2, sizeof b1, h1, 0xB1, scratch[1], 2560);
    if (!vka_drv_ring4_find(ring, 1, 2, b0, sizeof b0, &rbuf, &rptr, &roff, h0) || rbuf != 0xB0 || roff != 0) return 1;
    if (!vka_drv_ring4_find(ring, 1, 2, b1, sizeof b1, &rbuf, &rptr, &roff, h1) || rbuf != 0xB1 || roff != 2560) return 1;
    if (vka_drv_ring4_find(ring, 2, 2, b0, sizeof b0, &rbuf, &rptr, &roff, h0)) {
        fprintf(stderr, "FAIL drv_ring4 hit stale sub_gen=1 span\n"); return 1;
    }

    uint64_t dh0[2] = {0x1111111111111111ull, 0x2222222222222222ull};
    uint64_t dh1[2] = {0x1111111111111111ull, 0x9999999999999999ull};
    uint64_t sout = 0;
    vka_dset_memo4_put(dm, 1, 0x5555ull, 8, dh0, 0xD001ull);
    vka_dset_memo4_put(dm, 1, 0x5555ull, 8, dh1, 0xD002ull);
    if (!vka_dset_memo4_find(dm, 1, 0x5555ull, 8, dh0, &sout) || sout != 0xD001ull) { fprintf(stderr, "FAIL dset h0 got %llu\n", (unsigned long long)sout); return 1; }
    if (!vka_dset_memo4_find(dm, 1, 0x5555ull, 8, dh1, &sout) || sout != 0xD002ull) { fprintf(stderr, "FAIL dset h1 got %llu\n", (unsigned long long)sout); return 1; }
    return 0;
}

static uint64_t bench_dset_base(const uint64_t keys[][4], const uint8_t mats[][2560], uint8_t arena[][2560],
                                int nmat, int ops, uint64_t *allocs_out, uint64_t *dt_out) {
    uint64_t base_allocs = 0, base_sum = 0, last_off = UINT64_MAX;
    uint64_t last_k[4] = {0}; uint8_t last_bytes[2560] = {0};
    uint64_t t0 = now_ns();
    for (int i = 0; i < ops; i++) {
        int m = i & (nmat - 1);
        uint64_t off;
        if (last_off != UINT64_MAX && !memcmp(last_k, keys[m], sizeof last_k)) {
            off = last_off;
        } else {
            memcpy(last_bytes, mats[m], sizeof last_bytes);
            memcpy(arena[m], last_bytes, sizeof last_bytes);
            memcpy(last_k, keys[m], sizeof last_k);
            off = (uint64_t)m * 2560u;
            last_off = off;
            base_allocs += 2;
        }
        uint64_t words[2] = {(uint64_t)m, off};
        uint64_t h[2] = {0xcbf29ce484222325ull, 0x6a09e667f3bcc909ull};
        vka_hash_mix64(h, words, 2);
        base_sum += off + h[0] + (0xD100ull + (uint64_t)m);
    }
    *dt_out = now_ns() - t0;
    *allocs_out = base_allocs;
    return base_sum;
}

static uint64_t bench_dset_opt(vka_drv_key4 *km, vka_drv_ring4 *ring, vka_dset_memo4 *dm,
                               const uint64_t keys[][4], const uint8_t mats[][2560], uint8_t arena[][2560],
                               int nmat, int ops, uint64_t *allocs_out, uint64_t *dt_out) {
    uint64_t fast_allocs = 0, fast_sum = 0, prev_off = UINT64_MAX;
    uint64_t prev_k[4] = {0}; uint8_t tmp[2560];
    uint64_t t1 = now_ns();
    for (int i = 0; i < ops; i++) {
        int m = i & (nmat - 1);
        uint64_t rbuf = 0, roff = 0, dh_drv[2] = {0, 0}; uint8_t *rptr = NULL;
        if (prev_off != UINT64_MAX && !memcmp(prev_k, keys[m], sizeof prev_k)) {
            roff = prev_off;
        } else if (vka_drv_key4_find(km, 1, 2, keys[m], sizeof tmp, &rbuf, &roff)) {
            memcpy(prev_k, keys[m], sizeof prev_k); prev_off = roff;
        } else {
            memcpy(tmp, mats[m], sizeof tmp);
            if (!vka_drv_ring4_find(ring, 1, 2, tmp, sizeof tmp, &rbuf, &rptr, &roff, dh_drv)) {
                memcpy(arena[m], tmp, sizeof tmp);
                roff = (uint64_t)m * 2560u;
                vka_drv_ring4_put(ring, 1, 2, sizeof tmp, dh_drv, 0xA000ull, arena[m], roff);
                fast_allocs++;
            }
            vka_drv_key4_put(km, 1, 2, keys[m], sizeof tmp, 0xA000ull, roff);
            memcpy(prev_k, keys[m], sizeof prev_k); prev_off = roff;
        }
        uint64_t dwords[2] = {(uint64_t)m, roff}, dh[2] = {0xcbf29ce484222325ull, 0x6a09e667f3bcc909ull}, set = 0;
        vka_hash_mix64(dh, dwords, 2);
        if (!vka_dset_memo4_find(dm, 1, 0x100ull, 8, dh, &set)) {
            set = 0xD100ull + (uint64_t)m;
            vka_dset_memo4_put(dm, 1, 0x100ull, 8, dh, set);
            fast_allocs++;
        }
        fast_sum += roff + dh[0] + set;
    }
    *dt_out = now_ns() - t1;
    *allocs_out = fast_allocs;
    return fast_sum;
}

static int bench_dset_fast(void) {
    enum { NMAT = 16, OPS = 200000 };
    static vka_drv_key4 km;
    static vka_drv_ring4 ring;
    static vka_aref_memo4 am;
    static vka_dset_memo4 dm;
    static uint8_t mats[NMAT][2560];
    static uint8_t arena[NMAT + 4][2560];
    uint64_t keys[NMAT][4];
    for (int m = 0; m < NMAT; m++) {
        memset(mats[m], (m * 19 + 3) & 0xff, sizeof mats[m]);
        mats[m][0] = (uint8_t)m; mats[m][512] = (uint8_t)(m ^ 0x5a);
        keys[m][0] = 0x1000ull + (uint64_t)m * 7u; keys[m][1] = 1; keys[m][2] = 0; keys[m][3] = 0;
    }
    if (verify_dset_correctness(&km, &ring, &am, &dm, arena)) return 1;
    memset(&km, 0, sizeof km); memset(&ring, 0, sizeof ring); memset(&am, 0, sizeof am); memset(&dm, 0, sizeof dm);
    uint64_t base_allocs = 0, dt_base = 0, fast_allocs = 0, dt_fast = 0;
    uint64_t base_sum = bench_dset_base(keys, mats, arena, NMAT, OPS, &base_allocs, &dt_base);
    uint64_t fast_sum = bench_dset_opt(&km, &ring, &dm, keys, mats, arena, NMAT, OPS, &fast_allocs, &dt_fast);
    if (base_sum != fast_sum) { fprintf(stderr, "FAIL dset sum mismatch %llu != %llu\n", (unsigned long long)base_sum, (unsigned long long)fast_sum); return 1; }
    if (fast_allocs != (uint64_t)(NMAT * 2)) { fprintf(stderr, "FAIL expected %d fast allocs, got %llu\n", NMAT * 2, (unsigned long long)fast_allocs); return 1; }
    printf("aoi_w8_dset_fast: ops=%d baseline_ms=%.2f fast_ms=%.2f speedup=%.2fx allocs=%llu->%llu\n",
           OPS, dt_base / 1e6, dt_fast / 1e6, dt_fast ? (double)dt_base / (double)dt_fast : 0.0,
           (unsigned long long)base_allocs, (unsigned long long)fast_allocs);
    return 0;
}

int main(void) {
    const char *f = getenv("T_W8_FAULT");
    g_w8_fault = f ? atoi(f) : 0;
    int rc = bench_shader_fast() | bench_dset_fast();
    printf("t_shader_dset_w8: fault=%d status=%s\n", g_w8_fault, rc ? "FAIL" : "PASS");
    return rc;
}
