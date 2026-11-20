// Host unit, benchmark, and Delta #27 fault-injection test for vka_mem_lc.h.
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "../vk_android/vka_mem_lc.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { TEST_NSEGS = 32, N_LOOKUPS = 500000 };

static int g_fault;

static uint64_t lc_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static const seg_t *ref_find(const seg_t *segs, int n, uintptr_t a) {
    int lo = 0, hi = n - 1, hit = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (segs[m].lo <= a) { hit = m; lo = m + 1; } else hi = m - 1;
    }
    return (hit >= 0 && a < segs[hit].hi) ? &segs[hit] : NULL;
}

static void init_fixture(seg_t *segs, int n) {
    /* Interleave 32 MiB segments with 64 MiB stride aliases and unmapped gaps. */
    uintptr_t base = (uintptr_t)0x1000000000ull;
    for (int i = 0; i < n; i++) {
        uintptr_t lo = base + (uintptr_t)i * (uintptr_t)0x04000000ull; /* 64 MiB apart: collides in NLC=1024 */
        uintptr_t sz = (uintptr_t)0x02000000ull - (uintptr_t)0x20000ull; /* 32 MiB - 128 KiB */
        segs[i] = (seg_t){lo, lo + sz, {(uint64_t)(i + 1), lo ^ 0x55aaULL}, i + 1, 0};
    }
}

static void verify_exact(seg_t *segs) {
    vka_lc4 lc;
    memset(&lc, 0, sizeof lc);
    uint64_t gen = 1;
    for (int i = 0; i < 64000; i++) {
        if (i == 32000) {
            /* Shrink seg[1] and bump gen to verify stale MRU/set invalidation. */
            segs[1].hi = segs[1].lo + (uintptr_t)0x01000000ull;
            gen++;
        }
        int sidx = (i & 3) == 0 ? 0 : (i & 3) == 1 ? 1 : (i % TEST_NSEGS);
        uintptr_t off = (i == 32005) ? (segs[sidx].hi - segs[sidx].lo) : (((uintptr_t)i * 4096u) & 0x01ff0000ull);
        if ((i % 97) == 0) off = (segs[sidx].hi - segs[sidx].lo); /* probe exact hi boundary -> unmapped */
        uintptr_t a = segs[sidx].lo + off;
        const seg_t *got = vka_lc4_get(&lc, g_fault == 1 ? 1u : gen, a);
        if (g_fault == 2 && !got && sidx == 1 && off == segs[1].hi - segs[1].lo) got = &segs[1];
        if (!got) {
            const seg_t *ref = ref_find(segs, TEST_NSEGS, a);
            if (ref) {
                vka_lc4_put(&lc, gen, a, ref);
                if (g_fault == 3) lc.mru.lo += 4096;
                got = vka_lc4_get(&lc, gen, a);
            }
        }
        const seg_t *want = ref_find(segs, TEST_NSEGS, a);
        if (want) {
            assert(got != NULL);
            assert(got->lo == want->lo && got->hi == want->hi);
            assert(got->payload[0] == want->payload[0]);
        } else {
            assert(got == NULL);
        }
    }
}

static const seg_t *miss_locked(pthread_rwlock_t *rw, const seg_t *segs, uintptr_t a) {
    pthread_rwlock_rdlock(rw);
    const seg_t *r = ref_find(segs, TEST_NSEGS, a);
    pthread_rwlock_unlock(rw);
    return r;
}

static void bench_lc(const seg_t *segs) {
    static seg_t legacy[1024];
    pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
    uint64_t lgen = 0, lmiss = 0, fmiss = 0;
    vka_lc4 fast;
    memset(legacy, 0, sizeof legacy);
    memset(&fast, 0, sizeof fast);

    uint64_t t0 = lc_now_ns();
    uint64_t gen = 1, sink = 0;
    for (int i = 0; i < N_LOOKUPS; i++) {
        if ((i % 25000) == 24999) gen++;
        /* Realistic draw loop: alternating 64 MiB-aliased VB/UBO pools at same block offsets */
        int idx = i & 7;
        uintptr_t a = segs[idx].lo + (((uintptr_t)(i >> 3) * 0x10000u) & 0x00ff0000ull);
        seg_t *e = &legacy[(a >> 16) & 1023u];
        const seg_t *r = (lgen == gen && a >= e->lo && a < e->hi) ? e : NULL;
        if (!r) {
            lmiss++;
            r = miss_locked(&rw, segs, a);
            assert(r != NULL);
            if (lgen != gen) { memset(legacy, 0, sizeof legacy); lgen = gen; }
            *e = *r;
        }
        sink += r->payload[0];
    }
    uint64_t dt_leg = lc_now_ns() - t0;

    t0 = lc_now_ns();
    gen = 1;
    uint64_t sink2 = 0;
    for (int i = 0; i < N_LOOKUPS; i++) {
        if ((i % 25000) == 24999) gen++;
        /* Realistic draw loop: alternating 64 MiB-aliased VB/UBO pools at same block offsets */
        int idx = i & 7;
        uintptr_t a = segs[idx].lo + (((uintptr_t)(i >> 3) * 0x10000u) & 0x00ff0000ull);
        const seg_t *r = vka_lc4_get(&fast, gen, a);
        if (!r) {
            fmiss++;
            r = miss_locked(&rw, segs, a);
            assert(r != NULL);
            vka_lc4_put(&fast, gen, a, r);
        }
        sink2 += r->payload[0];
    }
    uint64_t dt_fast = lc_now_ns() - t0;
    assert(sink == sink2);
    double spd = dt_fast ? (double)dt_leg / (double)dt_fast : 0.0;
    printf("aoi_vka_lc: lookups=%d legacy_miss=%llu fast_miss=%llu legacy_ms=%.2f (%.2f ns/op) fast_ms=%.2f (%.2f ns/op) speedup=%.2fx\n",
           N_LOOKUPS, (unsigned long long)lmiss, (unsigned long long)fmiss,
           dt_leg / 1e6, (double)dt_leg / N_LOOKUPS,
           dt_fast / 1e6, (double)dt_fast / N_LOOKUPS, spd);
}

int main(void) {
    const char *f = getenv("T_VKA_LC_FAULT");
    g_fault = f ? atoi(f) : 0;
    seg_t segs[TEST_NSEGS];
    init_fixture(segs, TEST_NSEGS);
    verify_exact(segs);
    init_fixture(segs, TEST_NSEGS);
    bench_lc(segs);
    printf("t_vka_lc: fault=%d PASS\n", g_fault);
    return 0;
}
