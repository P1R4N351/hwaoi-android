/* Wave 17 (Opt 19): Host tests & benchmarks for vka_scr_fast.h (DM_VK_SCR_FAST=1). */
#define _POSIX_C_SOURCE 200809L
#define W17_TEST_HOOKS 1
#include "../vk_android/vka_scr_fast.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

int g_w17_fault = 0;
static volatile uint64_t g_w17_sink;

static double now_ms(void) {
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(rc == 0);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static int test_align_and_tlab(void) {
    const uint64_t aligns[4] = {16u, 64u, 256u, 48u};
    for (int ai = 0; ai < 4; ai++) {
        uint64_t al = aligns[ai];
        for (uint64_t off = 0; off < 600u; off += 7u) {
            uint64_t got = vka_scr_align_up(off, al);
            uint64_t ref = (off + al - 1u) / al * al;
            if (got != ref) return 1;
        }
    }
    static uint8_t backing[4096];
    vka_scr_tlab tl = {0};
    vka_scr_tlab_fill(&tl, 1u, 0x9001u, backing, backing, 16u, sizeof backing);
    uint64_t bu = 0, off = 0;
    uint8_t *ptr = NULL;
    if (!vka_scr_tlab_try(&tl, 1u, 128u, 256u, &bu, &ptr, &off)) return 1;
    if (bu != 0x9001u || off != 256u || ptr != backing + 256u || tl.cur != 384u) return 1;
    if (vka_scr_tlab_try(&tl, 2u, 128u, 256u, &bu, &ptr, &off)) return 1;
    return 0;
}

static int test_ind_idx4(void) {
    static vka_ind_idx4 idx;
    memset(&idx, 0, sizeof idx);
    uint64_t addrs[6], base = 0x10000100ull;
    int got = 0;
    for (uint64_t k = 0; k < 4096u && got < 6; k++) {
        uint64_t cand = base + (k << 16);
        if (vka_ind_set_idx(cand) == vka_ind_set_idx(base)) addrs[got++] = cand;
    }
    assert(got == 6);
    assert(vka_ind_set_idx(addrs[0]) == vka_ind_set_idx(addrs[1]));
    assert((addrs[0] & 0xffffu) == (addrs[1] & 0xffffu));
    vka_ind_idx4_put(&idx, addrs[0], 10);
    int nl = 0;
    if (vka_ind_idx4_find(&idx, addrs[1], &nl) != -1 || nl != 0) return 1;
    for (int i = 1; i < 6; i++) vka_ind_idx4_put(&idx, addrs[i], 10 + i);
    for (int i = 0; i < 4; i++) {
        if (vka_ind_idx4_find(&idx, addrs[i], &nl) != 10 + i) return 1;
    }
    if (vka_ind_idx4_find(&idx, addrs[4], &nl) != -1 || nl != 1) return 1;
    vka_ind_idx4_clear(&idx);
    if (vka_ind_idx4_find(&idx, addrs[0], &nl) != -1 || nl != 0) return 1;
    return 0;
}

static void bench_scratch_tlab(void) {
    enum { NA = 200000 };
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    static uint8_t block[16u << 20];
    static struct { uint64_t used; } st;
    volatile uint64_t align_arg = 256u;
    double t0 = now_ms();
    uint64_t acc_b = 0;
    for (int i = 0; i < NA; i++) {
        uint64_t req = 64u + ((uint64_t)(i & 7) << 6), al = align_arg;
        pthread_mutex_lock(&mu);
        uint64_t o = (st.used + al - 1u) / al * al;
        if (o + req > sizeof block) o = 0;
        st.used = o + req;
        pthread_mutex_unlock(&mu);
        acc_b += o;
    }
    double ms_b = now_ms() - t0;
    static vka_scr_tlab tl;
    vka_scr_tlab_fill(&tl, 1u, 0x700u, block, block, 0u, VKA_SCR_TLAB_CHUNK);
    double t1 = now_ms();
    uint64_t acc_f = 0;
    for (int i = 0; i < NA; i++) {
        uint64_t req = 64u + ((uint64_t)(i & 7) << 6), bu = 0, o = 0;
        uint8_t *p = NULL;
        if (!vka_scr_tlab_try(&tl, 1u, req, align_arg, &bu, &p, &o)) {
            pthread_mutex_lock(&mu);
            vka_scr_tlab_fill(&tl, 1u, 0x700u, block, block, 0u, VKA_SCR_TLAB_CHUNK);
            pthread_mutex_unlock(&mu);
            int ok = vka_scr_tlab_try(&tl, 1u, req, align_arg, &bu, &p, &o);
            assert(ok);
        }
        acc_f += o;
    }
    double ms_f = now_ms() - t1;
    g_w17_sink += acc_b + acc_f;
    printf("aoi_w17_scratch_tlab: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms_b, ms_f, ms_b / (ms_f > 0.001 ? ms_f : 0.001));
}

typedef struct { uint32_t kind, sz; uint64_t ind; } sim_op;

static void bench_ind_scan(void) {
    enum { NOPS = 2048, NRUNS = 4000 };
    static sim_op ops[NOPS];
    static vka_ind_idx4 seen0, seen1;
    for (int i = 0; i < NOPS; i++) {
        ops[i].kind = (i & 31) == 0 ? 1u : (i & 31) == 16 ? 2u : 0u;
        ops[i].sz = 16u;
        ops[i].ind = 0x50000000ull + (uint64_t)((i >> 5) & 63) * 64u + ((uint64_t)(i & 31) << 12);
    }
    double t0 = now_ms();
    uint64_t acc_b = 0;
    uint64_t q0[256], q1[256];
    for (int r = 0; r < NRUNS; r++) {
        int n0 = 0, n1 = 0;
        uint32_t off = (uint32_t)(g_w17_sink & 0u);
        for (int pass = 0; pass < 2; pass++) {
            uint64_t *q = pass ? q1 : q0;
            int *nq = pass ? &n1 : &n0;
            for (int i = 0; i < NOPS; i++) {
                if (ops[i].kind != (uint32_t)(pass + 1)) continue;
                uint64_t a = ops[i].ind + off;
                int dup = 0;
                for (int k = 0; k < *nq && !dup; k++) dup = (q[k] == a);
                if (!dup && *nq < 256) q[(*nq)++] = a;
            }
        }
        for (int i = 0; i < NOPS; i++) {
            if (ops[i].kind == 1u) { for (int k = 0; k < n0; k++) if (q0[k] == ops[i].ind) { acc_b += (uint64_t)k; break; } }
            else if (ops[i].kind == 2u) { for (int k = 0; k < n1; k++) if (q1[k] == ops[i].ind) { acc_b += (uint64_t)k; break; } }
        }
    }
    double ms_b = now_ms() - t0;
    double t1 = now_ms();
    uint64_t acc_f = 0;
    for (int r = 0; r < NRUNS; r++) {
        vka_ind_idx4_clear(&seen0); vka_ind_idx4_clear(&seen1);
        int n0 = 0, n1 = 0, nl = 0;
        uint32_t off = (uint32_t)(g_w17_sink & 0u);
        for (int i = 0; i < NOPS; i++) {
            if (ops[i].kind == 1u) {
                uint64_t a = ops[i].ind + off;
                if (vka_ind_idx4_find(&seen0, a, &nl) < 0 && !nl && n0 < 256) vka_ind_idx4_put(&seen0, a, n0++);
            } else if (ops[i].kind == 2u) {
                uint64_t a = ops[i].ind + off;
                if (vka_ind_idx4_find(&seen1, a, &nl) < 0 && !nl && n1 < 256) vka_ind_idx4_put(&seen1, a, n1++);
            }
        }
        for (int i = 0; i < NOPS; i++) {
            if (ops[i].kind == 1u) acc_f += (uint64_t)vka_ind_idx4_find(&seen0, ops[i].ind, &nl);
            else if (ops[i].kind == 2u) acc_f += (uint64_t)vka_ind_idx4_find(&seen1, ops[i].ind, &nl);
        }
    }
    double ms_f = now_ms() - t1;
    g_w17_sink += acc_b + acc_f;
    printf("aoi_w17_ind_scan: base=%.3f ms fast=%.3f ms speedup=%.2fx\n", ms_b, ms_f, ms_b / (ms_f > 0.001 ? ms_f : 0.001));
}

int main(void) {
    const char *f = getenv("T_W17_FAULT");
    g_w17_fault = f ? atoi(f) : 0;
    if (test_align_and_tlab() != 0) { fprintf(stderr, "FAIL test_align_and_tlab\n"); return 1; }
    if (test_ind_idx4() != 0) { fprintf(stderr, "FAIL test_ind_idx4\n"); return 1; }
    if (!g_w17_fault) { bench_scratch_tlab(); bench_ind_scan(); }
    printf("t_scr_fast_w17: ok (fault=%d)\n", g_w17_fault);
    return 0;
}
