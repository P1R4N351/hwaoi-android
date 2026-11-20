// Standalone host unit, benchmark, and Delta #27 fault-injection harness for hle_sync_fast.h (DM_SYNC_FAST=1).
#define _POSIX_C_SOURCE 200809L
#include "../hle_sync_fast.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int g_sync_fault = 0;
static volatile uint64_t g_sink;

typedef struct sim_thr {
    int prio;
    int wkind;
    uint64_t waddr, wkey, wseq;
    struct sim_thr *next, *bnext;
} sim_thr;

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) abort();
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static unsigned next_rng(uint64_t *s) {
    *s = *s * 6364136223846793005ull + 1442695040888963407ull;
    return (unsigned)(*s >> 32);
}

static int test_hmap_parity(void) {
    hle_hmap *m = calloc(1, sizeof *m);
    uint8_t *ref = calloc(4096, 1);
    assert(m != NULL && ref != NULL);
    uint64_t rng = 0x123456789abcdef0ull;
    unsigned handles[4096];
    int n_live = 0;
    for (int step = 0; step < 8000; step++) {
        if (n_live == 0 || (n_live < 4095 && (next_rng(&rng) & 3u) != 0)) {
            int want = 0;
            for (int i = 1; i < 4096; i++) if (!ref[i]) { want = i; break; }
            unsigned got = hle_hmap_alloc(m);
            if ((int)got != want || ref[got]) { free(m); free(ref); return 1; }
            ref[want] = 1;
            handles[n_live++] = got;
        } else {
            int pos = (int)(next_rng(&rng) % (unsigned)n_live);
            unsigned idx = handles[pos];
            handles[pos] = handles[--n_live];
            ref[idx] = 0;
            hle_hmap_free(m, idx);
        }
        if (hle_hmap_count(m) != n_live) { free(m); free(ref); return 2; }
    }
    while (n_live < 4095) {
        unsigned got = hle_hmap_alloc(m);
        if (!got || ref[got]) { free(m); free(ref); return 3; }
        ref[got] = 1;
        n_live++;
    }
    if (hle_hmap_alloc(m) != 0 || hle_hmap_count(m) != 4095) { free(m); free(ref); return 4; }
    free(m); free(ref);
    return 0;
}

static int test_wait_buckets_prio(void) {
    sim_thr th[48];
    memset(th, 0, sizeof th);
    sim_thr *g_wait = NULL, *buckets[HLE_WB_BUCKETS];
    memset(buckets, 0, sizeof buckets);
    uint64_t rng = 0xcafebabe01020304ull;
    for (int i = 0; i < 48; i++) {
        th[i].wkind = 1 + (i % 3);
        th[i].prio = 28 + (int)(next_rng(&rng) % 24u);
        th[i].wseq = (uint64_t)i + 1u;
        uint64_t addr = 0x70000000ull + (unsigned)(i & 7) * 64u;
        th[i].waddr = th[i].wkind == 2 ? 0 : addr;
        th[i].wkey = th[i].wkind == 2 ? addr : 0;
        th[i].next = g_wait; g_wait = &th[i];
        unsigned b = hle_wb_hash(th[i].wkind, addr);
        th[i].bnext = buckets[b]; buckets[b] = &th[i];
    }
    for (int k = 1; k <= 3; k++) {
        for (int a = 0; a < 8; a++) {
            uint64_t addr = 0x70000000ull + (unsigned)a * 64u;
            sim_thr *ref = NULL, *fast = NULL;
            for (sim_thr *t = g_wait; t; t = t->next) {
                if (t->wkind != k || (k == 2 ? t->wkey : t->waddr) != addr) continue;
                if (!ref || t->prio < ref->prio || (t->prio == ref->prio && t->wseq < ref->wseq)) ref = t;
            }
            for (sim_thr *t = buckets[hle_wb_hash(k, addr)]; t; t = t->bnext) {
                if (t->wkind != k || (k == 2 ? t->wkey : t->waddr) != addr) continue;
                if (!fast || hle_wb_prio_better(t->prio, t->wseq, fast->prio, fast->wseq)) fast = t;
            }
            if (ref != fast) return 1;
        }
    }
    return 0;
}

static int test_bloom_and_se_idx(void) {
    uint64_t objs[256];
    uint64_t rng = 0xdeadbeef87654321ull;
    for (int i = 0; i < 256; i++) objs[i] = 0x550000000000ull + ((uint64_t)(next_rng(&rng) & 0xFFFFu) << 4);
    for (int r = 0; r < 10000; r++) {
        uint64_t bm = 0;
        int n = 1 + (int)(next_rng(&rng) % 64u);
        uint64_t chosen[64];
        for (int i = 0; i < n; i++) {
            chosen[i] = objs[next_rng(&rng) % 256u];
            bm |= hle_obj_bloom_ins((const void *)(uintptr_t)chosen[i]);
        }
        for (int i = 0; i < n; i++) {
            if (!(bm & hle_obj_bloom_qry((const void *)(uintptr_t)chosen[i]))) return 1;
        }
    }
    hle_se_idx *ix = calloc(1, sizeof *ix);
    assert(ix != NULL);
    for (uint32_t r = 1; r <= 64; r++) {
        void *ev = (void *)(uintptr_t)(0x40000000ull + (uint64_t)r * 64ull);
        uint32_t wh = 0x1000u + r;
        hle_se_put(ix, ev, wh, r);
        uint64_t id = 0;
        if (hle_se_find_ev(ix, ev, &id) != wh || id != r) { free(ix); return 2; }
        if (hle_se_find_id(ix, r) != wh) { free(ix); return 3; }
        hle_se_del_wh(ix, wh);
        if (hle_se_find_ev(ix, ev, &id) != 0) { free(ix); return 4; }
    }
    free(ix);
    return 0;
}

static uint64_t bench_legacy_ops(void) {
    static int ltype[4096];
    static void *sev[128];
    static uint32_t swh[128];
    sim_thr th[48];
    memset(ltype, 0, sizeof ltype);
    memset(th, 0, sizeof th);
    for (int i = 1; i <= 1500; i++) ltype[i] = 1;
    sim_thr *g_wait = NULL;
    for (int i = 0; i < 48; i++) {
        th[i].wkind = 2; th[i].wkey = 0x70000000ull + (unsigned)(i & 15) * 64u;
        th[i].prio = 44 - (i & 3); th[i].wseq = (uint64_t)i + 1u;
        th[i].next = g_wait; g_wait = &th[i];
    }
    for (int i = 0; i < 128; i++) { sev[i] = (void *)(uintptr_t)(0x40000000ull + (unsigned)i * 64u); swh[i] = (uint32_t)(0x1001 + i); }
    uint64_t t0 = now_ns(), acc = 0;
    for (int it = 0; it < 200000; it++) {
        int slot = 0;
        for (int i = 1; i < 4096; i++) if (!ltype[i]) { ltype[i] = 2; slot = i; break; }
        ltype[slot] = 0;
        uint64_t k = 0x70000000ull + (unsigned)(it & 15) * 64u;
        sim_thr *best = NULL;
        for (sim_thr *t = g_wait; t; t = t->next) {
            if (t->wkind == 2 && t->wkey == k && (!best || t->prio < best->prio || (t->prio == best->prio && t->wseq < best->wseq))) best = t;
        }
        void *ev = sev[it & 127];
        uint32_t wh = 0;
        for (int i = 0; i < 128; i++) if (sev[i] == ev) { wh = swh[i]; break; }
        acc += (unsigned)slot ^ wh ^ (unsigned)(best ? best->wseq : 0);
    }
    g_sink ^= acc;
    return now_ns() - t0;
}

static uint64_t bench_fast_ops(void) {
    hle_hmap *m = calloc(1, sizeof *m);
    hle_se_idx *ix = calloc(1, sizeof *ix);
    sim_thr th[48];
    sim_thr *buckets[HLE_WB_BUCKETS];
    memset(th, 0, sizeof th);
    memset(buckets, 0, sizeof buckets);
    assert(m != NULL && ix != NULL);
    for (unsigned i = 1; i <= 1500; i++) hle_hmap_mark(m, i);
    for (int i = 0; i < 48; i++) {
        th[i].wkind = 2; th[i].wkey = 0x70000000ull + (unsigned)(i & 15) * 64u;
        th[i].prio = 44 - (i & 3); th[i].wseq = (uint64_t)i + 1u;
        unsigned b = hle_wb_hash(2, th[i].wkey);
        th[i].bnext = buckets[b]; buckets[b] = &th[i];
    }
    void *sev[128];
    for (int i = 0; i < 128; i++) {
        sev[i] = (void *)(uintptr_t)(0x40000000ull + (unsigned)i * 64u);
        hle_se_put(ix, sev[i], (uint32_t)(0x1001 + i), (uint64_t)i + 1u);
    }
    uint64_t t0 = now_ns(), acc = 0;
    for (int it = 0; it < 200000; it++) {
        unsigned slot = hle_hmap_alloc(m);
        hle_hmap_free(m, slot);
        uint64_t k = 0x70000000ull + (unsigned)(it & 15) * 64u;
        sim_thr *best = NULL;
        for (sim_thr *t = buckets[hle_wb_hash(2, k)]; t; t = t->bnext) {
            if (t->wkind == 2 && t->wkey == k && (!best || hle_wb_prio_better(t->prio, t->wseq, best->prio, best->wseq))) best = t;
        }
        uint32_t wh = hle_se_find_ev(ix, sev[it & 127], NULL);
        acc += slot ^ wh ^ (unsigned)(best ? best->wseq : 0);
    }
    g_sink ^= acc;
    free(m); free(ix);
    return now_ns() - t0;
}

int main(void) {
    const char *fe = getenv("T_SYNC_FAST_FAULT");
    g_sync_fault = fe ? atoi(fe) : 0;
    int r1 = test_hmap_parity();
    if (r1) { fprintf(stderr, "FAIL test_hmap_parity rc=%d fault=%d\n", r1, g_sync_fault); return 1; }
    int r2 = test_wait_buckets_prio();
    if (r2) { fprintf(stderr, "FAIL test_wait_buckets_prio rc=%d fault=%d\n", r2, g_sync_fault); return 1; }
    int r3 = test_bloom_and_se_idx();
    if (r3) { fprintf(stderr, "FAIL test_bloom_and_se_idx rc=%d fault=%d\n", r3, g_sync_fault); return 1; }
    uint64_t lns = bench_legacy_ops();
    uint64_t fns = bench_fast_ops();
    double lms = (double)lns / 1e6, fms = (double)fns / 1e6;
    printf("t_sync_fast: fault=%d 200k_ops legacy=%.2fms fast=%.2fms speedup=%.2fx sink=%llu\n",
           g_sync_fault, lms, fms, fms > 0 ? lms / fms : 0.0, (unsigned long long)g_sink);
    return 0;
}
