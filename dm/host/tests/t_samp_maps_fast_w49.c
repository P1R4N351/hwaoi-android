// Host unit test, TSan stress test, fault injection & benchmark for Wave 49 (Opt 51: DM_VK_SAMP_MAPS_FAST).
#ifndef T_W49_TEST_HOOKS
#define T_W49_TEST_HOOKS 1
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx/vk_android/vka_samp_maps_fast.h"

_Atomic int g_vka_samp_maps_fast_override = -1;
_Atomic int g_aoi_w49_fault = -1;
static int g_fail = 0;

typedef struct {
    uint8_t minf, magf, ws, wt, wr, cmp_mode, cmp_func, reduction;
    float lod_min, lod_max, lod_bias, aniso, border[4];
} w49_samp_par;
_Static_assert(sizeof(w49_samp_par) == 40u, "samp_par 40B");

typedef struct {
    w49_samp_par p;
    uint64_t handle;
} w49_samp_ent;

#define EXPECT_EQ(msg, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w49_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int ref_wrap(int w) {
    switch (w) {
    case 1: return 0;
    case 6: return 1;
    case 5: return 3;
    case 2: case 3: case 4: return 4;
    default: return 2;
    }
}

static void ref_astc_zero(uint8_t *p, size_t n) {
    static const uint8_t z[16] = {0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (size_t i = 0u; i + 16u <= n; i += 16u) {
        uint64_t a = 0, b = 0;
        memcpy(&a, p + i, 8);
        memcpy(&b, p + i + 8u, 8);
        if ((a | b) == 0u) memcpy(p + i, z, 16);
    }
}

static int ref_parse_line(const char *s, uintptr_t *lo, uintptr_t *hi, int *prot) {
    unsigned long a = 0, b = 0;
    char pm[8] = {0};
    if (sscanf(s, "%lx-%lx %7s", &a, &b, pm) != 3) return 0;
    *lo = (uintptr_t)a;
    *hi = (uintptr_t)b;
    *prot = (pm[0] == 'r' ? 1 : 0) | (pm[1] == 'w' ? 2 : 0) | (pm[2] == 'x' ? 4 : 0);
    return 1;
}

static w49_samp_par make_samp(uint32_t i) {
    w49_samp_par p;
    memset(&p, 0, sizeof p);
    p.minf = (uint8_t)(i & 5u);
    p.magf = (uint8_t)((i >> 3) & 1u);
    p.ws = (uint8_t)((i >> 4) & 7u);
    p.wt = (uint8_t)((i >> 7) & 7u);
    p.wr = (uint8_t)((i >> 10) & 7u);
    p.cmp_mode = (uint8_t)((i >> 1) & 1u);
    p.cmp_func = (uint8_t)((i & 7u) + 1u);
    p.reduction = (uint8_t)(i % 3u);
    p.lod_min = (float)(i & 15u);
    p.lod_max = (float)((i & 15u) + 8u);
    p.aniso = (float)((i & 3u) + 1u);
    p.border[3] = (i & 1u) ? 1.0f : 0.0f;
    return p;
}

static void test_samp_map_and_eq(void) {
    w49_samp_par a = make_samp(17u), b = a;
    EXPECT_EQ("samp_eq_same", 1, vka_samp_par_eq_fast(&a, &b));
    uint8_t *bp = (uint8_t *)&b;
    for (size_t off = 0u; off < 40u; off++) {
        bp[off] ^= 0x04u;
        EXPECT_EQ("samp_eq_flip", 0, vka_samp_par_eq_fast(&a, &b));
        bp[off] ^= 0x04u;
    }
    w49_samp_ent *ents = calloc(512u, sizeof *ents);
    vka_samp_map_t *map = calloc(1u, sizeof *map);
    if (!ents || !map) { g_fail++; free(ents); free(map); return; }
    for (uint32_t i = 0u; i < 512u; i++) {
        ents[i].p = make_samp(i + 1u);
        ents[i].handle = UINT64_C(0x1000) + i;
        vka_samp_map_insert(map, vka_samp_par_hash_fast(&ents[i].p), (uint16_t)(i + 1u));
    }
    for (uint32_t i = 0u; i < 512u; i++) {
        w49_samp_par q = make_samp(i + 1u);
        int idx = vka_samp_map_find(map, ents, sizeof *ents, &q, vka_samp_par_hash_fast(&q));
        EXPECT_EQ("samp_find_hit", (int)i, idx);
    }
    w49_samp_par miss = make_samp(9999u);
    EXPECT_EQ("samp_find_miss", -1, vka_samp_map_find(map, ents, sizeof *ents, &miss, vka_samp_par_hash_fast(&miss)));
    free(ents);
    free(map);
}

static void test_wrap_astc_and_maps(void) {
    for (int w = -4; w <= 16; w++) EXPECT_EQ("wrap_parity", ref_wrap(w), vka_wrap_fast(w));
    uint8_t b_ref[4096], b_fast[4096];
    for (size_t i = 0u; i < sizeof b_ref; i += 16u) {
        if ((i / 16u) & 3u) memset(b_ref + i, (int)(((i >> 4) & 0x7fu) + 1u), 16u);
        else memset(b_ref + i, 0, 16u);
    }
    memcpy(b_fast, b_ref, sizeof b_ref);
    ref_astc_zero(b_ref, sizeof b_ref);
    vka_astc_zero_blocks_fast(b_fast, sizeof b_fast);
    EXPECT_EQ("astc_zero_cmp", 0, memcmp(b_ref, b_fast, sizeof b_ref));

    static const char *const k_lines[16] = {
        "7f12340000-7f12345000 r-xp 00000000 08:01 12345 /system/lib64/libc.so\n",
        "7f12345000-7f12347000 rw-p 00005000 00:00 0 [anon:dalvik]\n",
        "  00400000-00401000   rwxp 00000000 00:00 0\n",
        "0000000000100000-0000000000200000 ---p 00000000 00:00 0\n",
        "deadbeef-faceb00c rwxs 00001000 00:04 99 /dev/ashmem\n",
        "ABCD0000-ABCE0000 r--p\n",
        "1000-2000 -w-p tail",
        "1000-2000 rw",
        "1000-2000 --xp 0",
        "invalid line here\n",
        "1000-2000\n",
        "1000-2000   \n",
        "-2000 rw-p\n",
        "1000- rw-p\n",
        "1000-2000 \t rwx\n",
        ""
    };
    for (size_t k = 0u; k < 16u; k++) {
        uintptr_t lr = 0, hr = 0, lf = 0, hf = 0;
        int pr = 0, pf = 0;
        int rr = ref_parse_line(k_lines[k], &lr, &hr, &pr);
        int rf = vka_maps_parse_line_fast(k_lines[k], strlen(k_lines[k]), &lf, &hf, &pf);
        EXPECT_EQ("maps_ok", rr, rf);
        if (rr && rf) {
            EXPECT_EQ("maps_lo", lr, lf);
            EXPECT_EQ("maps_hi", hr, hf);
            EXPECT_EQ("maps_prot", pr, pf);
        }
    }
}

typedef struct {
    const w49_samp_ent *ents;
    const vka_samp_map_t *map;
    uint32_t tid;
    uint32_t errs;
} w49_thr_arg;

static void *w49_worker(void *arg) {
    w49_thr_arg *t = (w49_thr_arg *)arg;
    uint8_t blk[256];
    for (uint32_t it = 0u; it < 200u; it++) {
        uint32_t i = (it + t->tid * 37u) & 255u;
        w49_samp_par q = make_samp(i + 1u);
        if (vka_samp_map_find(t->map, t->ents, sizeof *t->ents, &q, vka_samp_par_hash_fast(&q)) != (int)i) t->errs++;
        memset(blk, 0, sizeof blk);
        vka_astc_zero_blocks_fast(blk, sizeof blk);
        if (blk[0] != 0xFCu || blk[15] != 0u) t->errs++;
        uintptr_t lo = 0, hi = 0;
        int prot = 0;
        if (!vka_maps_parse_line_fast("7f001000-7f002000 rw-p 0", 23u, &lo, &hi, &prot) || prot != 3) t->errs++;
    }
    return NULL;
}

static void test_tsan(void) {
    w49_samp_ent *ents = calloc(256u, sizeof *ents);
    vka_samp_map_t *map = calloc(1u, sizeof *map);
    if (!ents || !map) { g_fail++; free(ents); free(map); return; }
    for (uint32_t i = 0u; i < 256u; i++) {
        ents[i].p = make_samp(i + 1u);
        vka_samp_map_insert(map, vka_samp_par_hash_fast(&ents[i].p), (uint16_t)(i + 1u));
    }
    pthread_t th[4];
    w49_thr_arg args[4];
    for (uint32_t t = 0u; t < 4u; t++) {
        args[t] = (w49_thr_arg){ents, map, t, 0u};
        pthread_create(&th[t], NULL, w49_worker, &args[t]);
    }
    for (uint32_t t = 0u; t < 4u; t++) {
        pthread_join(th[t], NULL);
        EXPECT_EQ("tsan_worker", 0u, args[t].errs);
    }
    free(ents);
    free(map);
}

static int ref_samp_eq(const w49_samp_par *a, const w49_samp_par *b) {
    return a->minf == b->minf && a->magf == b->magf && a->ws == b->ws && a->wt == b->wt &&
           a->wr == b->wr && a->cmp_mode == b->cmp_mode && a->cmp_func == b->cmp_func &&
           a->reduction == b->reduction && a->lod_min == b->lod_min && a->lod_max == b->lod_max &&
           a->lod_bias == b->lod_bias && a->aniso == b->aniso && a->border[0] == b->border[0] &&
           a->border[1] == b->border[1] && a->border[2] == b->border[2] && a->border[3] == b->border[3];
}

static uint64_t run_bench_pass(int fast, uint32_t iters) {
    vka_samp_maps_fast_set_for_test(fast);
    w49_samp_ent *ents = calloc(256u, sizeof *ents);
    vka_samp_map_t *map = calloc(1u, sizeof *map);
    if (!ents || !map) { g_fail++; free(ents); free(map); return 0u; }
    for (uint32_t i = 0u; i < 256u; i++) {
        ents[i].p = make_samp(i + 1u);
        ents[i].handle = i + 100u;
        vka_samp_map_insert(map, vka_samp_par_hash_fast(&ents[i].p), (uint16_t)(i + 1u));
    }
    static const char *const k_map_ln = "7f88123400-7f88567800 rw-p 00010000 08:01 54321 /data/app/lib.so\n";
    size_t map_len = strlen(k_map_ln);
    uint8_t blk[512];
    memset(blk, 0, sizeof blk);
    uint64_t sink = 0u;
    for (uint32_t it = 0u; it < iters; it++) {
        w49_samp_par q = make_samp((it & 255u) + 1u);
        uintptr_t lo = 0, hi = 0;
        int prot = 0;
        if (fast) {
            int idx = vka_samp_map_find(map, ents, sizeof *ents, &q, vka_samp_par_hash_fast(&q));
            sink += (uint64_t)(idx >= 0 ? ents[idx].handle : 0u);
            sink += (uint64_t)vka_wrap_fast((int)(it & 7u));
            vka_astc_zero_blocks_fast(blk, 128u);
            sink += blk[0];
            vka_maps_parse_line_fast(k_map_ln, map_len, &lo, &hi, &prot);
            sink += (uint64_t)((lo >> 12) + (hi >> 12) + (uintptr_t)prot);
        } else {
            int hit = -1;
            for (int i = 0; i < 256; i++) {
                if (ref_samp_eq(&ents[i].p, &q)) { hit = i; break; }
            }
            sink += (uint64_t)(hit >= 0 ? ents[hit].handle : 0u);
            sink += (uint64_t)ref_wrap((int)(it & 7u));
            ref_astc_zero(blk, 128u);
            sink += blk[0];
            ref_parse_line(k_map_ln, &lo, &hi, &prot);
            sink += (uint64_t)((lo >> 12) + (hi >> 12) + (uintptr_t)prot);
        }
        blk[(it * 16u) & 112u] = 0u;
    }
    free(ents);
    free(map);
    return sink;
}

int main(void) {
    vka_samp_maps_fast_set_for_test(1);
    test_samp_map_and_eq();
    test_wrap_astc_and_maps();
    test_tsan();
    uint32_t iters = 60000u;
    uint64_t t0 = w49_now_ns();
    uint64_t s_slow = run_bench_pass(0, iters);
    uint64_t dt_slow = w49_now_ns() - t0;
    uint64_t t1 = w49_now_ns();
    uint64_t s_fast = run_bench_pass(1, iters);
    uint64_t dt_fast = w49_now_ns() - t1;
    EXPECT_EQ("bench_sink", s_slow, s_fast);
    printf("t_samp_maps_fast_w49: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu %s\n",
           (double)dt_slow / 1e6, (double)dt_fast / 1e6,
           dt_fast ? (double)dt_slow / (double)dt_fast : 0.0,
           (unsigned long long)s_fast, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
