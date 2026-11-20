#define _POSIX_C_SOURCE 200809L
#define W13_TEST_HOOKS 1
#include "../vk_android/vka_cs_fast.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w13_fault = 0;
static volatile uint64_t g_w13_sink;

typedef struct {
    uint64_t lay_words[3];
    uint32_t push, ndesc;
} sim_layout;

typedef struct {
    uintptr_t cprog_u;
    uint64_t serial;
    uint32_t layer_gen;
    uintptr_t sh5;
    sim_layout lay;
    uintptr_t pipe;
} sim_cs_item;

static volatile uint64_t g_w13_salt = 0x517cc1b727220a95ull;
static uint64_t g_w13_ring[64][2];
static volatile uint64_t g_w13_binds;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static void sim_cmd_bind(uintptr_t p) {
    uint32_t idx = (uint32_t)g_w13_binds & 63u;
    g_w13_ring[idx][0] = 0x43534244u;
    g_w13_ring[idx][1] = (uint64_t)p;
    g_w13_binds = g_w13_binds + 1u;
}

static int sim_resolve_base(const sim_cs_item *pool, uint32_t n, uintptr_t cprog_u,
                            uint64_t serial, uint32_t lgen, uintptr_t *sh_out,
                            sim_layout *lay_out, uintptr_t *pipe_out) {
    uint64_t acc = (serial ^ g_w13_salt) & 15u;
    int found = 0;
    for (uint32_t i = 0; i < n; i++) {
        const sim_cs_item *it = &pool[i];
        acc += ((uint64_t)it->cprog_u ^ it->layer_gen) & 3u;
        if (it->cprog_u == cprog_u && it->serial == serial && it->layer_gen == lgen) {
            *sh_out = it->sh5 ^ (uintptr_t)(acc & 0u);
            *lay_out = it->lay;
            *pipe_out = it->pipe;
            found = 1;
        }
    }
    return found && *sh_out != 0u && *pipe_out != 0u;
}

static int sim_resolve_opt(vka_cs_disp_memo4 *m, const sim_cs_item *pool, uint32_t n,
                           uintptr_t cprog_u, uint64_t serial, uint32_t lgen,
                           uintptr_t *sh_out, sim_layout *lay_out, uintptr_t *pipe_out) {
    if (vka_cs_disp_memo4_find(m, cprog_u, serial, lgen, sh_out, lay_out,
                               sizeof *lay_out, pipe_out)) {
        return 1;
    }
    if (sim_resolve_base(pool, n, cprog_u, serial, lgen, sh_out, lay_out, pipe_out)) {
        vka_cs_disp_memo4_put(m, cprog_u, serial, lgen, *sh_out, lay_out,
                              sizeof *lay_out, *pipe_out);
        return 1;
    }
    return 0;
}

static void test_correctness_and_faults(void) {
    vka_cs_pipe_state ps = {0};
    assert(vka_cs_pipe_bind_needed(&ps, 1, 0x100u, 0xaaa1u) == 1);
    assert(vka_cs_pipe_bind_needed(&ps, 1, 0x100u, 0xaaa1u) == 0);
    if (vka_cs_pipe_bind_needed(&ps, 1, 0x100u, 0xaaa2u) != 1) {
        fprintf(stderr, "FAIL: pipe change ignored\n");
        exit(1);
    }
    assert(vka_cs_pipe_bind_needed(&ps, 1, 0x200u, 0xaaa2u) == 1);
    vka_cs_pipe_reset(&ps);
    assert(vka_cs_pipe_bind_needed(&ps, 1, 0x200u, 0xaaa2u) == 1);

    sim_cs_item pool[10];
    memset(pool, 0, sizeof pool);
    for (uint32_t i = 0; i < 8u; i++) {
        pool[i] = (sim_cs_item){
            (uintptr_t)(0x400000u + i * 64u), 10u + i, 1u,
            (uintptr_t)(0x800000u + i * 32u),
            {{0x100u + i, 0x200u + i, 0x300u + i}, 1u, 6u},
            (uintptr_t)(0xc00000u + i * 16u)
        };
    }
    pool[8] = (sim_cs_item){
        (uintptr_t)0x400000u, 999u, 1u, (uintptr_t)0x899999u,
        {{0x999u, 0x888u, 0x777u}, 1u, 8u}, (uintptr_t)0xc99999u
    };
    pool[9] = (sim_cs_item){
        (uintptr_t)0x500000u, 200u, 1u, (uintptr_t)0x855555u,
        {{0x555u, 0u, 0u}, 1u, 4u}, (uintptr_t)0u
    };

    vka_cs_disp_memo4 dm;
    memset(&dm, 0, sizeof dm);
    uintptr_t sh = 0, pp = 0;
    sim_layout lay = {0};

    assert(sim_resolve_opt(&dm, pool, 10u, (uintptr_t)0x400000u, 10u, 1u, &sh, &lay, &pp) == 1);
    assert(pp == (uintptr_t)0xc00000u);

    assert(sim_resolve_opt(&dm, pool, 10u, (uintptr_t)0x500000u, 200u, 1u, &sh, &lay, &pp) == 0);
    pool[9].pipe = (uintptr_t)0xc55555u;
    assert(sim_resolve_opt(&dm, pool, 10u, (uintptr_t)0x500000u, 200u, 1u, &sh, &lay, &pp) == 1);
    assert(pp == (uintptr_t)0xc55555u);

    if (!sim_resolve_opt(&dm, pool, 10u, (uintptr_t)0x400000u, 999u, 1u, &sh, &lay, &pp) ||
        pp != (uintptr_t)0xc99999u) {
        fprintf(stderr, "FAIL: serial mismatch returned stale pipe=%llu\n", (unsigned long long)pp);
        exit(1);
    }

    vka_diag_keys dk = {0};
    if (vka_diag_need_reload(dk.ready, dk.gen, 1u)) vka_diag_keys_parse(&dk, 1u, "14a8aaf3e0a9edbd");
    assert(vka_diag_keys_match(&dk, 0x14a8aaf3e0a9edbdull) == 1);
    if (vka_diag_need_reload(dk.ready, dk.gen, 2u)) vka_diag_keys_parse(&dk, 2u, "deadbeefcafebabe");
    if (vka_diag_keys_match(&dk, 0xdeadbeefcafebabeull) != 1 ||
        vka_diag_keys_match(&dk, 0x14a8aaf3e0a9edbdull) != 0) {
        fprintf(stderr, "FAIL: diag_keys failed to reload on env_gen=2\n");
        exit(1);
    }

    vka_diag_range dr = {0};
    if (vka_diag_need_reload(dr.ready, dr.gen, 2u)) vka_diag_range_parse(&dr, 2u, "5,10");
    assert(vka_diag_range_hit(&dr, 5u) == 1 && vka_diag_range_hit(&dr, 4u) == 0 &&
           vka_diag_range_hit(&dr, 10u) == 1 && vka_diag_range_hit(&dr, 11u) == 0);
}

static void run_benchmarks(void) {
    const uint32_t N = 400000u;
    sim_cs_item pool[16];
    memset(pool, 0, sizeof pool);
    for (uint32_t i = 0; i < 16u; i++) {
        pool[i] = (sim_cs_item){
            (uintptr_t)(0x600000u + i * 64u), 500u + i, 3u,
            (uintptr_t)(0x900000u + i * 32u),
            {{0x10u + i, 0x20u, 0x30u}, 1u, 6u},
            (uintptr_t)(0xd00000u + i * 16u)
        };
    }
    vka_cs_disp_memo4 dm;
    memset(&dm, 0, sizeof dm);
    vka_cs_pipe_state ps = {0};

    uint64_t sb = 0, so = 0, binds_b = 0, binds_o = 0;
    double t0 = now_ms();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t idx = (i >> 14) & 7u;
        uintptr_t sh = 0, pp = 0; sim_layout lay = {0};
        if (sim_resolve_base(pool, 16u, pool[idx].cprog_u, pool[idx].serial, 3u, &sh, &lay, &pp)) {
            sim_cmd_bind(pp);
            binds_b++;
            sb += (uint64_t)pp + (uint64_t)lay.lay_words[0];
        }
    }
    double ms_b = now_ms() - t0;

    double t1 = now_ms();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t idx = (i >> 14) & 7u;
        uintptr_t sh = 0, pp = 0; sim_layout lay = {0};
        if (sim_resolve_opt(&dm, pool, 16u, pool[idx].cprog_u, pool[idx].serial, 3u, &sh, &lay, &pp)) {
            if (vka_cs_pipe_bind_needed(&ps, 1, 0x2000u, (uint64_t)pp)) {
                sim_cmd_bind(pp);
                binds_o++;
            }
            so += (uint64_t)pp + (uint64_t)lay.lay_words[0];
        }
    }
    double ms_o = now_ms() - t1;
    assert(sb == so && binds_o < binds_b);

    uint64_t dsum_b = 0, dsum_o = 0;
    vka_diag_keys dk = {0};
    double t2 = now_ms();
    for (uint32_t i = 0; i < N; i++) {
        char hex[17];
        snprintf(hex, sizeof hex, "%016llx", (unsigned long long)(0x12340000ull + (i & 3u)));
        if (strstr("aaaa,bbbb", hex)) dsum_b++;
    }
    double ms_db = now_ms() - t2;

    double t3 = now_ms();
    for (uint32_t i = 0; i < N; i++) {
        uint32_t g = 1u;
        if (vka_diag_need_reload(dk.ready, dk.gen, g)) vka_diag_keys_parse(&dk, g, "aaaa,bbbb");
        if (dk.nk && vka_diag_keys_match(&dk, 0x12340000ull + (i & 3u))) dsum_o++;
    }
    double ms_do = now_ms() - t3;
    assert(dsum_b == dsum_o);

    g_w13_sink = sb ^ so ^ binds_o ^ dsum_o ^ g_w13_binds ^ g_w13_ring[0][1];
    printf("aoi_w13_cs_fast: binds=%llu->%llu base=%.2fms opt=%.2fms speedup=%.2fx\n",
           (unsigned long long)binds_b, (unsigned long long)binds_o,
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
    printf("aoi_w13_diag_env: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_db, ms_do, ms_db / (ms_do > 0.01 ? ms_do : 0.01));
}

int main(void) {
    const char *f = getenv("T_W13_FAULT");
    g_w13_fault = f ? atoi(f) : 0;
    assert(vka_cs_fast_on() == 0 || vka_cs_fast_on() == 1);
    test_correctness_and_faults();
    run_benchmarks();
    printf("t_cs_fast_w13: fault=%d PASS\n", g_w13_fault);
    return 0;
}
