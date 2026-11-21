// Unit, parity, TSan, fault-injection, and benchmark suite for DM_VK_IMGMEM_FAST (Wave 32 / Opt 34).
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/vk_android/vka_imgmem_fast.h"
#include "../../gfx/vk_android/vka_extents.h"
#include "../../gfx/vk_android/vka_zshare.h"

enum { W32_UNITS = 262144u, W32_NBLK = 8, W32_LIVE = 2048, W32_ZKEYS = 512, W32_SLAB_CAP = 4096 };

static uint64_t w32_ns(void) {
    struct timespec t = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) abort();
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static inline uint64_t w32_rng(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * UINT64_C(0x2545f4914f6cdd1d);
}

static void w32_setup_blocks(vke_list blk[W32_NBLK], vke_ext *slab, uint32_t (*kept_a)[600], uint32_t (*kept_n)[600],
                             int kept_cnt[W32_NBLK], uint64_t *rng) {
    uint8_t fast = (uint8_t)vka_imgmem_fast_on();
    for (int b = 0; b < W32_NBLK; b++) {
        blk[b] = (vke_list){ .f = slab + (size_t)b * W32_SLAB_CAP, .nf = 1u, .cap = W32_SLAB_CAP,
                             .units = W32_UNITS, .used = 0u, .max_n = W32_UNITS, .max_valid = 1u, .fast_on = fast };
        blk[b].f[0] = (vke_ext){0u, W32_UNITS};
        kept_cnt[b] = 0;
        if (b < W32_NBLK - 1) {
            uint32_t aa[320], nn[320];
            int k = 0;
            for (int step = 0; step < 320; step++) {
                uint32_t hsz = b >= 5 ? (16u + (uint32_t)(w32_rng(rng) % 17u)) : (1u + (uint32_t)(w32_rng(rng) % 7u));
                uint32_t sz = (step & 1) ? 8u : hsz;
                int64_t u = vke_alloc(&blk[b], sz, 1u);
                if (u < 0) break;
                aa[k] = (uint32_t)u; nn[k] = sz; k++;
            }
            for (int step = 0; step < k; step += 2) {
                assert(vke_free(&blk[b], aa[step], nn[step]) == 0);
            }
            for (int step = 1; step < k; step += 2) {
                int idx = kept_cnt[b]++;
                kept_a[b][idx] = aa[step]; kept_n[b][idx] = nn[step];
            }
            uint32_t tail = blk[b].nf ? blk[b].f[blk[b].nf - 1u].n : 0u;
            if (tail > 16u) {
                uint32_t csz = tail - 8u;
                int64_t cap = vke_alloc(&blk[b], csz, 1u);
                if (cap >= 0) {
                    int idx = kept_cnt[b]++;
                    kept_a[b][idx] = (uint32_t)cap; kept_n[b][idx] = csz;
                }
            }
        }
    }
}

static void w32_cleanup_blocks(vke_list blk[W32_NBLK], uint32_t (*kept_a)[600], uint32_t (*kept_n)[600],
                               int kept_cnt[W32_NBLK]) {
    for (int b = 0; b < W32_NBLK; b++) {
        for (int k = 0; k < kept_cnt[b]; k++) assert(vke_free(&blk[b], kept_a[b][k], kept_n[b][k]) == 0);
        assert(vke_empty(&blk[b]) && blk[b].nf == 1u && blk[b].f[0].a == 0u && blk[b].f[0].n == W32_UNITS);
    }
}

typedef struct { int blk; uint32_t a, n; } w32_alloc_rec;

static uint64_t w32_run_extents_churn(int fast_on, int rounds) {
    vka_imgmem_fast_reset_for_test(fast_on);
    vke_list blk[W32_NBLK];
    vke_ext *slab = calloc((size_t)W32_NBLK * W32_SLAB_CAP, sizeof *slab);
    uint32_t (*kept_a)[600] = calloc(W32_NBLK, sizeof *kept_a);
    uint32_t (*kept_n)[600] = calloc(W32_NBLK, sizeof *kept_n);
    assert(slab != NULL && kept_a != NULL && kept_n != NULL);
    int kept_cnt[W32_NBLK] = {0};
    w32_alloc_rec *live = calloc(W32_LIVE, sizeof *live);
    assert(live != NULL);
    uint64_t rng = UINT64_C(0x13579bdf2468ace1), sink = 0u;
    w32_setup_blocks(blk, slab, kept_a, kept_n, kept_cnt, &rng);
    int nlive = 0;
    for (int r = 0; r < rounds; r++) {
        if (nlive >= W32_LIVE - 4 || (nlive > 128 && (w32_rng(&rng) & 3u) == 0u)) {
            int idx = (int)(w32_rng(&rng) % (uint64_t)nlive);
            w32_alloc_rec rec = live[idx];
            live[idx] = live[--nlive];
            assert(vke_free(&blk[rec.blk], rec.a, rec.n) == 0);
            sink ^= (uint64_t)rec.a + (uint64_t)rec.n;
            continue;
        }
        uint32_t sz = 16u + (uint32_t)(w32_rng(&rng) % 48u);
        uint32_t al = 1u << (w32_rng(&rng) % 5u);
        int64_t u = -1;
        int chosen = -1;
        for (int b = 0; b < W32_NBLK; b++) {
            u = vke_alloc(&blk[b], sz, al);
            if (u >= 0) { chosen = b; break; }
        }
        if (u >= 0) {
            assert((u & (int64_t)(al - 1u)) == 0);
            live[nlive++] = (w32_alloc_rec){chosen, (uint32_t)u, sz};
            sink += (uint64_t)chosen * 131u + (uint64_t)u;
        } else {
            sink += 7u;
        }
    }
    for (int i = 0; i < nlive; i++) assert(vke_free(&blk[live[i].blk], live[i].a, live[i].n) == 0);
    w32_cleanup_blocks(blk, kept_a, kept_n, kept_cnt);
    free(live); free(kept_a); free(kept_n); free(slab);
    return sink;
}

typedef struct { uint32_t kind, w, h, fmt, pad[28]; } w32_zkey;
_Static_assert(sizeof(w32_zkey) == 128, "128B key size");

static uint64_t w32_run_zshare_churn(int fast_on, int ops) {
    vka_imgmem_fast_reset_for_test(fast_on);
    vkz_tab t = {0};
    w32_zkey *keys = calloc(W32_ZKEYS, sizeof *keys);
    uint64_t *hs = calloc(ops, sizeof *hs);
    assert(keys != NULL && hs != NULL);
    for (int i = 0; i < W32_ZKEYS; i++) {
        keys[i].kind = (uint32_t)(i % 7);
        keys[i].w = 512u + (uint32_t)i * 4u;
        keys[i].h = 256u + (uint32_t)(i % 31);
        keys[i].fmt = (uint32_t)(i & 15);
    }
    uint64_t rng = UINT64_C(0x8877665544332211), nh = 1000u, sink = 0u;
    int nhs = 0;
    for (int step = 0; step < ops; step++) {
        if (nhs > 256 && (w32_rng(&rng) & 1u)) {
            int pos = (int)(w32_rng(&rng) % (uint64_t)nhs);
            uint64_t h = hs[pos];
            hs[pos] = hs[--nhs];
            int un = vkz_unref(&t, h);
            assert(un == 0 || un == 1);
            sink += (uint64_t)un * 19u + (h & 63u);
        } else {
            int ki = (int)(w32_rng(&rng) % W32_ZKEYS);
            int hit = vkz_ref(&t, &keys[ki], sizeof keys[ki]);
            if (hit >= 0) {
                hs[nhs++] = t.e[hit].h;
                sink ^= t.e[hit].h;
            } else {
                uint64_t h = nh++;
                int added = vkz_add(&t, &keys[ki], sizeof keys[ki], h, UINT64_C(4194304) + (uint64_t)ki * 4096u);
                assert(added >= 0);
                hs[nhs++] = h;
                sink += h;
            }
        }
        if ((step & 127) == 0) {
            uint64_t im = 0, al = 0, by = 0, sv = 0;
            vkz_stats(&t, &im, &al, &by, &sv);
            sink ^= im * 3u + al * 5u + by + sv;
        }
    }
    for (int i = 0; i < nhs; i++) assert(vkz_unref(&t, hs[i]) >= 0);
    uint64_t im = 99, al = 99, by = 99, sv = 99;
    vkz_stats(&t, &im, &al, &by, &sv);
    assert(im == 0u && al == 0u && by == 0u && sv == 0u);
    free(t.e); free(keys); free(hs);
    return sink;
}

static uint32_t w32_ref_mem_type(uint32_t count, const uint32_t *flags, uint32_t bits, uint32_t want, uint32_t avoid) {
    for (uint32_t i = 0u; i < count; i++) {
        if ((bits >> i & 1u) && (flags[i] & want) == want && !(flags[i] & avoid)) return i;
    }
    return UINT32_MAX;
}

static uint64_t w32_run_memtype_check(int iters, int fault_mode) {
    static const uint32_t flags[11] = {
        0x01u, 0x01u, 0x07u, 0x0fu, 0x06u, 0x0bu, 0x11u, 0x21u, 0x41u, 0x02u, 0x0eu
    };
    vka_memtype_lut lut;
    vka_memtype_lut_build(&lut, 11u, flags);
    uint64_t sink = 0u, rng = UINT64_C(0x55aa55aa11223344);
    for (int i = 0; i < iters; i++) {
        uint32_t bits = (uint32_t)(w32_rng(&rng) & 0x7ffu);
        uint32_t want = (uint32_t)((w32_rng(&rng) & 0x4fu));
        uint32_t avoid = (uint32_t)((w32_rng(&rng) & 0x08u));
        uint32_t ref = w32_ref_mem_type(11u, flags, bits, want, avoid);
        uint32_t got = vka_memtype_pick_fast(&lut, bits, want, avoid, fault_mode);
        assert(ref == got);
        sink += (uint64_t)(got & 31u);
    }
    assert(vka_imem_empty_should_free(0u, fault_mode) == 0);
    assert(vka_imem_empty_should_free(1u, fault_mode) == 0);
    assert(vka_imem_empty_should_free(2u, fault_mode) == 1);
    return sink;
}

typedef struct { int tid; } w32_thr_arg;

static void *w32_tsan_worker(void *p) {
    w32_thr_arg *arg = (w32_thr_arg *)p;
    vke_ext fbuf[64] = {{0u, 4096u}};
    vke_list l = { .f = fbuf, .nf = 1u, .cap = 64u, .units = 4096u, .used = 0u,
                   .max_n = 4096u, .max_valid = 1u, .fast_on = (uint8_t)vka_imgmem_fast_on() };
    uint64_t s = 0u;
    for (int i = 0; i < 400; i++) {
        int64_t u = vke_alloc(&l, 16u + (uint32_t)(i & 15), 4u);
        assert(u >= 0);
        s += (uint64_t)u;
        assert(vke_free(&l, (uint32_t)u, 16u + (uint32_t)(i & 15)) == 0);
    }
    return (void *)(uintptr_t)(s + (uint64_t)arg->tid);
}

static void w32_test_init_grow_fini(void) {
    vke_list l = {0};
    int ok = vke_init(&l, 1024u);
    assert(ok == 1);
    if (!ok) return;
    int64_t a[36] = {0};
    for (int i = 0; i < 36; i++) {
        a[i] = vke_alloc(&l, 4u, 1u);
        if (a[i] < 0) ok = 0;
    }
    for (int i = 0; i < 36; i += 2) {
        if (a[i] >= 0 && vke_free(&l, (uint32_t)a[i], 4u) != 0) ok = 0;
    }
    if (l.cap < 32u) ok = 0;
    for (int i = 1; i < 36; i += 2) {
        if (a[i] >= 0 && vke_free(&l, (uint32_t)a[i], 4u) != 0) ok = 0;
    }
    if (!vke_empty(&l) || l.nf != 1u || l.f[0].a != 0u || l.f[0].n != 1024u) ok = 0;
    vke_fini(&l);
    assert(ok == 1);
}

static void w32_run_tsan(void) {
    pthread_t th[4];
    w32_thr_arg args[4];
    for (int i = 0; i < 4; i++) {
        args[i].tid = i;
        assert(pthread_create(&th[i], NULL, w32_tsan_worker, &args[i]) == 0);
    }
    for (int i = 0; i < 4; i++) {
        void *ret = NULL;
        assert(pthread_join(th[i], &ret) == 0);
    }
}

__attribute__((noinline)) static uint64_t w32_bench_once(int fast_on) {
    uint64_t s = w32_run_extents_churn(fast_on, 24000);
    s ^= w32_run_zshare_churn(fast_on, 20000);
    return s;
}

int main(void) {
    const char *env = getenv("DM_VK_IMGMEM_FAST");
    const char *flt = getenv("T_W32_FAULT");
    int fault_mode = flt ? atoi(flt) : 0;
    vka_w32_set_fault(fault_mode);

    w32_test_init_grow_fini();
    uint64_t s0 = w32_run_extents_churn(0, 6000);
    uint64_t s1 = w32_run_extents_churn(1, 6000);
    assert(s0 == s1);
    uint64_t z0 = w32_run_zshare_churn(0, 8000);
    uint64_t z1 = w32_run_zshare_churn(1, 8000);
    assert(z0 == z1);
    (void)w32_run_memtype_check(10000, fault_mode);
    w32_run_tsan();

    vka_w32_set_fault(0);
    uint64_t t0 = w32_ns();
    uint64_t b0 = w32_bench_once(0);
    uint64_t off_ns = w32_ns() - t0;
    uint64_t t1 = w32_ns();
    uint64_t b1 = w32_bench_once(1);
    uint64_t on_ns = w32_ns() - t1;
    assert(b0 == b1);

    int restore = (env && *env == '0' && !env[1]) ? 0 : 1;
    vka_imgmem_fast_reset_for_test(restore);
    printf("BENCH aoi_w32_off imem_ops=%.3f ms sink=%llu\n", off_ns / 1e6, (unsigned long long)b0);
    printf("BENCH aoi_w32_on imem_ops=%.3f ms sink=%llu\n", on_ns / 1e6, (unsigned long long)b1);
    return 0;
}
