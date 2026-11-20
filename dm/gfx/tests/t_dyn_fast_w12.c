#define _POSIX_C_SOURCE 200809L
#define W12_TEST_HOOKS 1
#include "../vk_android/vka_dyn_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w12_fault = 0;
static volatile uint64_t g_w12_sink;

typedef struct {
    int valid, rast_discard, has_vp, restart_on;
    float vp[4], dr[2], bc[4];
    uint32_t poly_cull, poly_front, poly_mode, poly_off;
    float po_u, po_c, po_f;
    uint32_t ds_dt, ds_dw, ds_df, ds_st, ds_sff, ds_sfb, ds_ff, ds_zpf, ds_zff, ds_fb, ds_zpb, ds_zfb;
    uint32_t smask[6];
} sim_dyn_in;

typedef struct {
    int valid;
    float vp[6], bc[4], db[3];
    int32_t sc[4];
    uint32_t cull, ff, topo, dt, dw, dc, dbt, stt, sop[8], smask[6], dbe, rd, pr;
} sim_dy_out;

static volatile sim_dyn_in g_vol_in;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static void init_sim_input(void) {
    sim_dyn_in v = {
        1, 0, 1, 1,
        {0.0f, 0.0f, 1920.0f, 1080.0f}, {0.0f, 1.0f}, {1.0f, 0.5f, 0.25f, 1.0f},
        1, 0, 2, 1, 1.5f, 0.0f, 2.0f,
        1, 1, 4, 1, 3, 7, 1, 2, 3, 1, 2, 3,
        {0xff, 0xff, 0xff, 0xff, 1, 2}
    };
    g_vol_in = v;
}

static uint64_t run_dyn_baseline_step(sim_dy_out *dy, int32_t scx, int32_t scy,
                                      int32_t scw, int32_t sch, uint32_t topo) {
    sim_dyn_in s = g_vol_in;
    float vp[6] = {s.vp[0], s.vp[1] + s.vp[3], s.vp[2], -s.vp[3], s.dr[0], s.dr[1]};
    int32_t sc[4] = {scx < 0 ? 0 : scx, scy < 0 ? 0 : scy, scw, sch};
    uint32_t sop[8] = {s.ds_ff, s.ds_zpf, s.ds_zff, s.ds_sff, s.ds_fb, s.ds_zpb, s.ds_zfb, s.ds_sfb};
    float db[3] = {s.poly_off ? s.po_u : 0.0f, s.poly_off ? s.po_c : 0.0f, s.poly_off ? s.po_f : 0.0f};
    uint64_t cmds = 0;
    if (!dy->valid || memcmp(dy->vp, vp, sizeof vp)) { memcpy(dy->vp, vp, sizeof vp); cmds++; }
    if (!dy->valid || memcmp(dy->sc, sc, sizeof sc)) { memcpy(dy->sc, sc, sizeof sc); cmds++; }
    if (!dy->valid || dy->cull != s.poly_cull) { dy->cull = s.poly_cull; cmds++; }
    if (!dy->valid || dy->ff != s.poly_front) { dy->ff = s.poly_front; cmds++; }
    if (!dy->valid || dy->topo != topo) { dy->topo = topo; cmds++; }
    if (!dy->valid || dy->dt != s.ds_dt) { dy->dt = s.ds_dt; cmds++; }
    if (!dy->valid || dy->dw != s.ds_dw) { dy->dw = s.ds_dw; cmds++; }
    if (!dy->valid || dy->dc != s.ds_df) { dy->dc = s.ds_df; cmds++; }
    if (!dy->valid || dy->dbt != 0u) { dy->dbt = 0u; cmds++; }
    if (!dy->valid || dy->stt != s.ds_st) { dy->stt = s.ds_st; cmds++; }
    if (!dy->valid || memcmp(dy->sop, sop, sizeof sop)) { memcpy(dy->sop, sop, sizeof sop); cmds++; }
    if (!dy->valid || memcmp(dy->smask, s.smask, sizeof s.smask)) { memcpy(dy->smask, s.smask, sizeof s.smask); cmds++; }
    if (!dy->valid || dy->dbe != s.poly_off) { dy->dbe = s.poly_off; cmds++; }
    if (!dy->valid || memcmp(dy->db, db, sizeof db)) { memcpy(dy->db, db, sizeof db); cmds++; }
    if (!dy->valid || dy->rd != (uint32_t)s.rast_discard) { dy->rd = (uint32_t)s.rast_discard; cmds++; }
    if (!dy->valid || memcmp(dy->bc, s.bc, sizeof s.bc)) { memcpy(dy->bc, s.bc, sizeof s.bc); cmds++; }
    if (!dy->valid || dy->pr != (uint32_t)s.restart_on) { dy->pr = (uint32_t)s.restart_on; cmds++; }
    dy->valid = 1;
    return cmds ^ (uint64_t)dy->sc[2] ^ (uint64_t)dy->cull;
}

static uint64_t run_dyn_opt_step(vka_dyn_guard *gd, sim_dy_out *dy, uint64_t svfmt,
                                 uint32_t bgen, int32_t scx, int32_t scy,
                                 int32_t scw, int32_t sch, uint32_t topo) {
    vka_dyn_key k;
    uint32_t fl = vka_dyn_pack_flags(1, 0, 0, 1, 1, 1, 1);
    vka_dyn_key_fill(&k, (const void *)(uintptr_t)0x1000u, svfmt, bgen, 1920u, 1080u,
                     0, 0, 1920u, 1080u, 100u, 100u, scx, scy, scw, sch, topo, fl, 0);
    if (dy->valid && vka_dyn_guard_hit(gd, &k)) {
        return (uint64_t)dy->sc[2] ^ (uint64_t)dy->cull;
    }
    uint64_t res = run_dyn_baseline_step(dy, scx, scy, scw, sch, topo);
    vka_dyn_guard_put(gd, &k);
    return res;
}

typedef struct {
    uint64_t w0, w1, w2, w3;
    uint32_t count;
} sim_write;

static volatile uint64_t g_vol_handle = 0xfeedface1234ull;

static void make_writes(sim_write *ws, uint32_t n, uint64_t seed, int bad_last) {
    for (uint32_t i = 0; i < n; i++) {
        uint64_t h = g_vol_handle ^ seed ^ (uint64_t)i * 1315423911ull;
        ws[i].w0 = (uint64_t)i | (2ull << 32) | (1ull << 48);
        ws[i].w1 = h;
        ws[i].w2 = (seed & 0xfffu) * 256u + i;
        ws[i].w3 = 256u;
        ws[i].count = (bad_last && i + 1u == n) ? 2u : 1u;
    }
}

typedef struct { uint64_t *w; size_t n, cap; } sim_pd;

static int push_baseline(sim_pd *pd, const sim_write *ws, uint32_t n, uint64_t layout) {
    static uint64_t *buf;
    static size_t cap;
    size_t need = 2u + (size_t)n * 5u, k = 0;
    if (need > cap) {
        cap = need * 2u;
        buf = (uint64_t *)realloc(buf, cap * sizeof *buf);
        if (!buf) { cap = 0; return 0; }
    }
    buf[k++] = layout;
    buf[k++] = n;
    for (uint32_t i = 0; i < n; i++) {
        if (ws[i].count != 1u) { pd->n = 0; return 0; }
        buf[k++] = ws[i].w0; buf[k++] = ws[i].w1; buf[k++] = ws[i].w2; buf[k++] = ws[i].w3;
    }
    if (pd->n == k && !memcmp(pd->w, buf, k * sizeof *buf)) return 1;
    if (!vka_pd_grow(&pd->w, &pd->cap, k)) return 0;
    memcpy(pd->w, buf, k * sizeof *buf);
    pd->n = k;
    return 0;
}

static int push_fill(sim_pd *pd, const sim_write *ws, uint32_t n, uint32_t from) {
    for (uint32_t i = from; i < n; i++) {
        if (ws[i].count != 1u) { pd->n = 0; return -1; }
        uint64_t *dst = pd->w + 2u + 4u * (size_t)i;
        dst[0] = ws[i].w0; dst[1] = ws[i].w1; dst[2] = ws[i].w2; dst[3] = ws[i].w3;
    }
    return 0;
}

static int push_fast(sim_pd *pd, const sim_write *ws, uint32_t n, uint64_t layout) {
    size_t need = 2u + 4u * (size_t)n;
    if (!vka_pd_grow(&pd->w, &pd->cap, need)) { pd->n = 0; return 0; }
    if (pd->n == need && pd->w[0] == layout && pd->w[1] == n) {
        for (uint32_t i = 0; i < n; i++) {
            if (ws[i].count != 1u) { pd->n = 0; return 0; }
            uint64_t row[4] = {ws[i].w0, ws[i].w1, ws[i].w2, ws[i].w3};
            if (!vka_pd_eq4(pd->w + 2u + 4u * (size_t)i, row)) {
                if (push_fill(pd, ws, n, i) < 0) return 0;
                return 0;
            }
        }
        return 1;
    }
    pd->w[0] = layout;
    pd->w[1] = n;
    if (push_fill(pd, ws, n, 0u) < 0) return 0;
    pd->n = need;
    return 0;
}

static void test_correctness_and_faults(void) {
    sim_dy_out d_base = {0}, d_opt = {0};
    vka_dyn_guard gd = {0};
    uint32_t bgen = 1u;
    uint64_t b0 = run_dyn_baseline_step(&d_base, 0, 0, 1920, 1080, 3u);
    uint64_t o0 = run_dyn_opt_step(&gd, &d_opt, 100u, bgen, 0, 0, 1920, 1080, 3u);
    assert(b0 == o0);

    /* Fault 1 target: scissor width changes while x/y/h match. */
    uint64_t b1 = run_dyn_baseline_step(&d_base, 0, 0, 960, 1080, 3u);
    uint64_t o1 = run_dyn_opt_step(&gd, &d_opt, 100u, bgen, 0, 0, 960, 1080, 3u);
    if (b1 != o1 || d_base.sc[2] != d_opt.sc[2]) {
        fprintf(stderr, "FAIL: dyn_guard scissor width mismatch base=%d opt=%d\n",
                d_base.sc[2], d_opt.sc[2]);
        exit(1);
    }

    /* Fault 2 target: bind_gen bumps after state change. */
    sim_dyn_in mutated = g_vol_in;
    mutated.poly_cull = 2u;
    g_vol_in = mutated;
    bgen++;
    uint64_t b2 = run_dyn_baseline_step(&d_base, 0, 0, 960, 1080, 3u);
    uint64_t o2 = run_dyn_opt_step(&gd, &d_opt, 100u, bgen, 0, 0, 960, 1080, 3u);
    if (b2 != o2 || d_base.cull != d_opt.cull) {
        fprintf(stderr, "FAIL: dyn_guard bind_gen mismatch base_cull=%u opt_cull=%u\n",
                d_base.cull, d_opt.cull);
        exit(1);
    }
    init_sim_input();

    /* Fault 3 target: descriptor offset (w2) changes with same binding & layout. */
    sim_pd pa = {0}, pb = {0};
    sim_write ws[16];
    make_writes(ws, 16u, 1u, 0);
    assert(push_baseline(&pa, ws, 16u, 0xaaaa01ull) == 0);
    assert(push_fast(&pb, ws, 16u, 0xaaaa01ull) == 0);
    assert(push_baseline(&pa, ws, 16u, 0xaaaa01ull) == 1);
    assert(push_fast(&pb, ws, 16u, 0xaaaa01ull) == 1);

    /* Change offset only on descriptor 7 while keeping layout, n, binding, buffer unchanged. */
    ws[7].w2 += 512u;
    int rb = push_baseline(&pa, ws, 16u, 0xaaaa01ull);
    int ro = push_fast(&pb, ws, 16u, 0xaaaa01ull);
    if (rb != ro || pa.n != pb.n || memcmp(pa.w, pb.w, pa.n * sizeof *pa.w) != 0) {
        fprintf(stderr, "FAIL: push_dedup w2 mismatch rb=%d ro=%d\n", rb, ro);
        exit(1);
    }

    /* Late descriptorCount != 1 after early mismatch must clear g_pd just like baseline. */
    ws[0].w1 += 1u;
    ws[15].count = 2u;
    int rbc = push_baseline(&pa, ws, 16u, 0xaaaa01ull);
    int roc = push_fast(&pb, ws, 16u, 0xaaaa01ull);
    assert(rbc == 0 && roc == 0 && pa.n == 0 && pb.n == 0);

    free(pa.w);
    free(pb.w);
}

static void run_benchmarks(void) {
    const int N = 400000;
    sim_dy_out db = {0}, do_ = {0};
    vka_dyn_guard gd = {0};
    uint64_t sb = 0, so = 0;

    double t0 = now_ms();
    for (int i = 0; i < N; i++) {
        sb += run_dyn_baseline_step(&db, 0, 0, 1920, 1080, (uint32_t)(3 + (i >> 18)));
    }
    double ms_db = now_ms() - t0;

    double t1 = now_ms();
    for (int i = 0; i < N; i++) {
        so += run_dyn_opt_step(&gd, &do_, 100u, 5u, 0, 0, 1920, 1080, (uint32_t)(3 + (i >> 18)));
    }
    double ms_do = now_ms() - t1;
    assert(sb == so);

    sim_pd pa = {0}, pb = {0};
    sim_write ws[2][16];
    make_writes(ws[0], 16u, 7u, 0);
    make_writes(ws[1], 16u, 7u, 0);
    ws[1][15].w2 += 256u;

    uint64_t pb_sum = 0, po_sum = 0;
    double t2 = now_ms();
    for (int i = 0; i < N; i++) {
        pb_sum += (uint64_t)push_baseline(&pa, ws[i >> 18], 16u, 0x555501ull);
    }
    double ms_pb = now_ms() - t2;

    double t3 = now_ms();
    for (int i = 0; i < N; i++) {
        po_sum += (uint64_t)push_fast(&pb, ws[i >> 18], 16u, 0x555501ull);
    }
    double ms_po = now_ms() - t3;
    assert(pb_sum == po_sum && pa.n == pb.n);

    g_w12_sink = sb ^ so ^ pb_sum ^ po_sum;
    free(pa.w);
    free(pb.w);

    printf("aoi_w12_dyn_guard: baseline=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_db, ms_do, ms_db / (ms_do > 0.01 ? ms_do : 0.01));
    printf("aoi_w12_push_dedup: baseline=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_pb, ms_po, ms_pb / (ms_po > 0.01 ? ms_po : 0.01));
}

int main(void) {
    const char *f = getenv("T_W12_FAULT");
    g_w12_fault = f ? atoi(f) : 0;
    assert(vka_dyn_memo_on() == 0 || vka_dyn_memo_on() == 1);
    init_sim_input();
    test_correctness_and_faults();
    run_benchmarks();
    printf("t_dyn_fast_w12: fault=%d PASS\n", g_w12_fault);
    return 0;
}
