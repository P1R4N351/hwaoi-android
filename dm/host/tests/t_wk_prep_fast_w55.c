/* Wave 55 (Opt 57 / DM_VK_WK_PREP_FAST=1) unit test, fault injection & benchmark. */
#ifndef T_W55_TEST_HOOKS
#define T_W55_TEST_HOOKS
#endif
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vka_wk_prep_fast.h"

_Atomic int g_vka_wk_prep_fast_override = -1;
_Atomic int g_aoi_w55_fault = -1;
static int g_fail = 0;

static void check_u64(const char *msg, uint64_t want, uint64_t got) {
    if (want != got) {
        g_fail++;
        fprintf(stderr, "FAIL %s: want=%llu got=%llu\n", msg,
                (unsigned long long)want, (unsigned long long)got);
    }
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct { uint8_t what, dim; uint32_t tid; } t_res_t;
enum { TR_TEX = 1, TR_IMG = 2, TR_TBUF = 3, TR_BUF = 4, TR_SINT = 16, TR_UINT = 32 };

static void run_wk_mask_stage(const t_res_t *r, int n, uint64_t *acc, int fast, const vka_wk_masks_t *mk) {
    if (fast && mk->ready) {
        for (uint64_t m = mk->tex_tbuf_m; m; m &= m - 1u) {
            int i = __builtin_ctzll(m);
            *acc += (uint64_t)r[i].tid + (uint64_t)i;
        }
        for (uint64_t m = mk->buf_m; m; m &= m - 1u) *acc += (uint64_t)__builtin_ctzll(m);
        for (uint64_t m = mk->float_tex_m; m; m &= m - 1u) *acc += (uint64_t)r[__builtin_ctzll(m)].tid;
        return;
    }
    for (int i = 0; i < n; i++) {
        if ((r[i].what == TR_TEX || r[i].what == TR_TBUF) && r[i].tid)
            *acc += (uint64_t)r[i].tid + (uint64_t)i;
        if (r[i].what == TR_BUF) *acc += (uint64_t)i;
        if ((r[i].what == TR_TEX || r[i].what == TR_IMG) && r[i].tid && !(r[i].dim & (TR_SINT | TR_UINT)))
            *acc += (uint64_t)r[i].tid;
    }
}

static void test_wk_masks(void) {
    t_res_t r[32];
    vka_wk_masks_t mk = {0};
    for (int i = 0; i < 32; i++) {
        r[i] = (t_res_t){0, 0, 0u};
        if (i == 4 || i == 27) r[i] = (t_res_t){TR_TEX, 0, (uint32_t)(100 + i)};
        if (i == 9) r[i] = (t_res_t){TR_TBUF, TR_SINT, 77u};
        if (i == 19) r[i] = (t_res_t){TR_BUF, 0, 0u};
        vka_wk_masks_add(&mk, i, r[i].what == TR_TEX, r[i].what == TR_IMG, r[i].what == TR_TBUF,
                         r[i].what == TR_BUF, r[i].tid != 0u, !(r[i].dim & (TR_SINT | TR_UINT)));
    }
    vka_wk_masks_finish(&mk, 32);
    uint64_t a0 = 0u, a1 = 0u;
    run_wk_mask_stage(r, 32, &a0, 0, &mk);
    run_wk_mask_stage(r, 32, &a1, 1, &mk);
    check_u64("wk_masks_oracle", a0, a1);
}

static void test_vbuf_and_fb(void) {
    uint64_t vb[16] = {0};
    uint16_t mask = 0u;
    for (int step = 0; step < 32; step++) {
        int first = (step * 5) % 13, n = 1 + (step & 3);
        uint16_t nz = 0u;
        for (int i = 0; i < n; i++) {
            vb[first + i] = ((step + i) & 1) ? (uint64_t)(0x100000 + step * 64 + i) : 0u;
            if (vb[first + i]) nz |= (uint16_t)(1u << (first + i));
        }
        mask = vka_vbuf_mask_patch(mask, first, n, nz);
        uint64_t s0 = 0u, s1 = 0u;
        for (int s = 0; s < 16; s++) if (vb[s]) s0 += vb[s] ^ (uint64_t)s;
        for (uint32_t m = mask; m; m &= m - 1u) {
            int s = __builtin_ctz(m);
            s1 += vb[s] ^ (uint64_t)s;
        }
        check_u64("vbuf_nz_oracle", s0, s1);
    }
    uintptr_t fbs[4] = {0x1000u, 0x2000u, 0x3000u, 0x4000u};
    vka_fb_filter_t flt = {0};
    vka_fb_filter_set(&flt, fbs, 4);
    for (int i = 0; i < 4; i++) check_u64("fb_present", 1u, (uint64_t)vka_fb_may_contain(&flt, fbs[i]));
    check_u64("fb_zero", 0u, (uint64_t)vka_fb_may_contain(&flt, 0u));
}

typedef struct { uint64_t im; uint32_t lv, ly; int32_t x, y; uint32_t w, h; int copied; } t_regw_t;
static int regw_over(const t_regw_t *a, const t_regw_t *b) {
    if (!a->copied || a->im != b->im || a->lv != b->lv || a->ly != b->ly) return 0;
    return a->x < b->x + (int32_t)b->w && b->x < a->x + (int32_t)a->w &&
           a->y < b->y + (int32_t)b->h && b->y < a->y + (int32_t)a->h;
}

static uint64_t run_regw_pass(t_regw_t *rw, int n, int fast, vka_regw_idx_t *idx) {
    uint64_t barriers = 0u;
    if (fast) vka_regw_idx_reset(idx);
    for (int i = 0; i < n; i++) {
        rw[i].copied = 0;
        t_regw_t *w = &rw[i];
        if (fast) {
            uint64_t tm = vka_regw_tile_mask(w->x, w->y, w->w, w->h);
            int c = vka_regw_first_cand(idx, w->im, w->lv, w->ly, tm);
            for (int j = c; j >= 0; j = regw_over(&rw[j], w) ? -1 : vka_regw_next_cand(idx, j, tm)) {
                if (regw_over(&rw[j], w)) { barriers++; break; }
            }
            w->copied = 1;
            vka_regw_insert(idx, i, w->im, w->lv, w->ly, tm);
        } else {
            for (int j = 0; j < i; j++) if (regw_over(&rw[j], w)) { barriers++; break; }
            w->copied = 1;
        }
    }
    return barriers;
}

static void test_regw_and_wbf(void) {
    static t_regw_t rw[240];
    static vka_regw_idx_t idx;
    uint64_t rng = UINT64_C(0x9e3779b97f4a7c15);
    for (int i = 0; i < 240; i++) {
        rng = rng * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
        rw[i] = (t_regw_t){0x700000u + ((rng >> 32) % 24u) * 0x1000u, (uint32_t)((rng >> 40) & 3u),
                           (uint32_t)((rng >> 42) & 1u), (int32_t)((rng >> 16) % 256u),
                           (int32_t)((rng >> 24) % 256u), 32u, 32u, 0};
    }
    check_u64("regw_barriers", run_regw_pass(rw, 240, 0, &idx), run_regw_pass(rw, 240, 1, &idx));
    typedef struct { int live_ix; uint8_t wbf_ix; } t_wbf_im_t;
    t_wbf_im_t ims[8] = {0};
    vka_wbf_wait_list_t wl = {0};
    for (int i = 0; i < 8; i++) ims[i].live_ix = i + 1;
    vka_wbf_list_add(&wl, &ims[5], &ims[5].wbf_ix);
    vka_wbf_list_add(&wl, &ims[2], &ims[2].wbf_ix);
    vka_wbf_list_add(&wl, &ims[7], &ims[7].wbf_ix);
    check_u64("wbf_cnt_3", 3u, wl.n);
    vka_wbf_list_del(&wl, &ims[2].wbf_ix);
    check_u64("wbf_del_0", 0u, ims[2].wbf_ix);
    check_u64("wbf_moved", (uint64_t)&ims[7], (uint64_t)wl.ims[ims[7].wbf_ix - 1u]);
    vka_wbf_list_clear(&wl);
    check_u64("wbf_cleared", 0u, wl.n | ims[5].wbf_ix | ims[7].wbf_ix);
}

static void *tsan_worker(void *arg) {
    uint64_t acc = (uint64_t)(uintptr_t)arg;
    vka_fb_filter_t flt = {0};
    uintptr_t ims[3] = {0x1000u + acc, 0x2000u + acc, 0x3000u + acc};
    vka_fb_filter_set(&flt, ims, 3);
    for (int i = 0; i < 64; i++) acc += (uint64_t)vka_fb_may_contain(&flt, ims[i % 3]);
    return (void *)(uintptr_t)acc;
}

static uint64_t bench_one_pass(t_regw_t *rw, t_res_t *r, const vka_wk_masks_t *mk,
                               vka_regw_idx_t *idx, int fast) {
    uint64_t sink = 0u;
    for (int it = 0; it < 200; it++) {
        for (int d = 0; d < 128; d++) run_wk_mask_stage(r, 32, &sink, fast, mk);
        sink += run_regw_pass(rw, 160, fast, idx);
    }
    return sink;
}

int main(void) {
    test_wk_masks();
    test_vbuf_and_fb();
    test_regw_and_wbf();
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    static t_regw_t rw[160];
    static vka_regw_idx_t idx;
    t_res_t r[32];
    vka_wk_masks_t mk = {0};
    for (int i = 0; i < 32; i++) {
        r[i] = (t_res_t){0, 0, 0u};
        if (i == 5 || i == 22) r[i] = (t_res_t){TR_TEX, 0, (uint32_t)(30 + i)};
        if (i == 17) r[i] = (t_res_t){TR_BUF, 0, 0u};
        vka_wk_masks_add(&mk, i, r[i].what == TR_TEX, 0, 0, r[i].what == TR_BUF, r[i].tid != 0u, 1);
    }
    vka_wk_masks_finish(&mk, 32);
    for (int i = 0; i < 160; i++) {
        rw[i] = (t_regw_t){0x900000u + (uint64_t)(i % 64) * 0x1000u, (uint32_t)(i & 1),
                           0u, (int32_t)((i * 19) % 512), (int32_t)((i * 37) % 512), 16u, 16u, 0};
    }
    uint64_t t0 = now_ns();
    uint64_t s0 = bench_one_pass(rw, r, &mk, &idx, 0);
    uint64_t t1 = now_ns();
    uint64_t s1 = bench_one_pass(rw, r, &mk, &idx, 1);
    uint64_t t2 = now_ns();
    check_u64("bench_sink", s0, s1);
    double ms0 = (double)(t1 - t0) / 1e6, ms1 = (double)(t2 - t1) / 1e6;
    printf("t_wk_prep_fast_w55: baseline=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=%s\n",
           ms0, ms1, ms1 > 0.0 ? ms0 / ms1 : 0.0, (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
