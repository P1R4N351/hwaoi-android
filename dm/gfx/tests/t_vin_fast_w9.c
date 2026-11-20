#define _POSIX_C_SOURCE 200809L
#define W9_TEST_HOOKS 1
#include "../vk_android/vka_vin_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w9_fault = 0;
static volatile uint64_t g_w9_sink;
static pthread_rwlock_t g_w9_rw = PTHREAD_RWLOCK_INITIALIZER;

typedef struct { uint32_t a, b, c, d; } w9_attr;
typedef struct { w9_attr a[32]; uint32_t bind[16]; } w9_vk_slot;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static uint64_t find_vc_collider(uint64_t vs_u, uint64_t fmt0) {
    uint32_t b0 = vka_vin_mix32(vs_u ^ (fmt0 * 0x9e3779b97f4a7c15ull), VKA_VC_SETS);
    for (uint64_t c = 1; c <= 1024; c++) {
        uint64_t fmt = fmt0 + c;
        if (vka_vin_mix32(vs_u ^ (fmt * 0x9e3779b97f4a7c15ull), VKA_VC_SETS) == b0) return fmt;
    }
    return 0;
}

static uint64_t find_addr_collider(uint64_t a0, uint64_t req_len) {
    uint32_t b0 = vka_vin_mix32((a0 >> 4) ^ (req_len * 0x9e3779b97f4a7c15ull), VKA_ADDRM_SETS);
    for (uint64_t c = 1; c <= 8192; c++) {
        uint64_t a = a0 + c * 64ull;
        if (vka_vin_mix32((a >> 4) ^ (req_len * 0x9e3779b97f4a7c15ull), VKA_ADDRM_SETS) == b0) return a;
    }
    return 0;
}

static int test_vc_and_vbc(void) {
    vka_vinfast_vc_memo4 *m = calloc(1, sizeof *m);
    assert(m != NULL);
    uint64_t vs = 0x50000010ull, f0 = 100ull, f1 = find_vc_collider(vs, f0);
    assert(f1 != 0);
    vka_vinfast_vc_ent in = {.vs_u = vs, .s_vfmt = f0, .ser = 42u, .vin_u = 3u, .vin_s = 1u, .used = 0x5u, .nb = 2u, .na = 4u, .zb = 31u, .zero = 0u};
    uint32_t s_alloc = vka_vinfast_vc_alloc(m, &in), s_hit = 999u;
    const vka_vinfast_vc_ent *h0 = vka_vinfast_vc_find(m, vs, f0, &s_hit);
    if (!h0 || h0->ser != 42u || s_hit != s_alloc) { free(m); return 1; }
    if (vka_vinfast_vc_find(m, vs, f1, &s_hit) != NULL) { free(m); return 2; }
    free(m);

    vka_vinfast_vbc vbc;
    vka_vinfast_vbc_reset(&vbc);
    uint32_t nu = 0; int nz = 0;
    if (!vka_vinfast_need_streams(&vbc, 1, 7u, 1u, 0x7u, 1, 15u, &nu, &nz) || nu != 0x7u || !nz) return 3;
    vka_vinfast_note_streams(&vbc, 1, 7u, 1u, nu, nz, 15u);
    if (vka_vinfast_need_streams(&vbc, 1, 7u, 1u, 0x3u, 1, 15u, &nu, &nz)) return 4;
    if (!vka_vinfast_need_streams(&vbc, 1, 7u, 1u, (1u << 15) | 0x3u, 0, 15u, &nu, &nz) || nu != (1u << 15)) return 5;
    vka_vinfast_note_streams(&vbc, 1, 7u, 1u, nu, 0, 15u);
    if (!vka_vinfast_need_streams(&vbc, 1, 7u, 1u, 0x1u, 1, 15u, &nu, &nz) || !nz) return 6;
    vka_vinfast_note_streams(&vbc, 1, 7u, 1u, nu, nz, 15u);
    if (!vka_vinfast_need_streams(&vbc, 1, 7u, 2u, 0x1u, 0, 15u, &nu, &nz) || nu != 0x1u) return 7;
    return 0;
}

static int test_addrm4_correctness(void) {
    vka_addrm4 *am = calloc(1, sizeof *am);
    assert(am != NULL);
    uint64_t a0 = 0x80001000ull, a1 = find_addr_collider(a0, 256u);
    assert(a1 != 0);
    vka_addrm4_put(am, 11u, a0, 256u, 0xdead01ull, 0x400ull, 4096u);
    vka_addrm4_put(am, 11u, a1, 256u, 0xdead02ull, 0x800ull, 0u);
    uint64_t bu = 0, oo = 0, run = 0;
    if (!vka_addrm4_find(am, 11u, a0, 256u, &bu, &oo, &run) || bu != 0xdead01ull || run != 4096u) { free(am); return 1; }
    if (vka_addrm4_find(am, 11u, a1, 256u, &bu, &oo, &run)) { free(am); return 2; }
    if (vka_addrm4_find(am, 12u, a0, 256u, &bu, &oo, &run)) { free(am); return 3; }
    free(am);
    return 0;
}

static void sim_vin_build(uint64_t mat, w9_vk_slot *s, vka_vinfast_vc_ent *ent) {
    uint32_t mask = 0xffu, na = 0, nb = 0, used = 0;
    for (uint32_t loc = 0; loc < 32u; loc++) if ((mask >> loc) & 1u) {
        uint32_t st = loc & 3u, fmt = 95u + ((mat + loc) & 7u);
        s->a[na++] = (w9_attr){loc, st, fmt, loc * 4u};
        used |= 1u << st;
    }
    for (uint32_t st = 0; st < 16u; st++) if ((used >> st) & 1u) s->bind[nb++] = 32u + (uint32_t)mat + st;
    ent->na = (uint8_t)na; ent->nb = (uint8_t)nb; ent->used = (uint16_t)used; ent->ser = (uint32_t)(mat + 1u);
}

static double bench_vc_materials(int fast, uint64_t *misses_o) {
    vka_vinfast_vc_memo4 *m = calloc(1, sizeof *m);
    w9_vk_slot *slots = calloc(VKA_VC_SLOTS, sizeof *slots), cur = {0};
    assert(m && slots);
    struct { uint64_t vs, fmt; uint32_t ser; int valid; } last = {0};
    double t0 = now_ms(); uint64_t misses = 0, sum = 0;
    for (int it = 0; it < 400000; it++) {
        uint64_t mat = (uint64_t)(it & 15), vs = 0x1000000ull + mat * 0x100ull, fmt = 10u + mat;
        if (last.valid && last.vs == vs && last.fmt == fmt) { sum += last.ser; continue; }
        uint32_t sl = 0;
        const vka_vinfast_vc_ent *hit = fast ? vka_vinfast_vc_find(m, vs, fmt, &sl) : NULL;
        if (hit) {
            last.valid = 1; last.vs = vs; last.fmt = fmt; last.ser = hit->ser;
            sum += slots[sl].a[0].c + hit->ser; continue;
        }
        misses++;
        vka_vinfast_vc_ent built = {.vs_u = vs, .s_vfmt = fmt};
        sim_vin_build(mat, &cur, &built);
        if (fast) { sl = vka_vinfast_vc_alloc(m, &built); slots[sl] = cur; sum += slots[sl].a[0].c + built.ser; }
        else sum += cur.a[0].c + built.ser;
        last.valid = 1; last.vs = vs; last.fmt = fmt; last.ser = built.ser;
    }
    double dt = now_ms() - t0;
    g_w9_sink ^= sum + 1u; *misses_o = misses; free(m); free(slots);
    return dt;
}

static double bench_addrm4(int fast, uint64_t *misses_o) {
    vka_addrm4 *am = calloc(1, sizeof *am);
    assert(am != NULL);
    uint64_t seg_lo[64], seg_hi[64], seg_buf[64];
    for (int i = 0; i < 64; i++) { seg_lo[i] = 0x20000000ull + (uint64_t)i * 0x100000ull; seg_hi[i] = seg_lo[i] + 0x100000ull; seg_buf[i] = 0x9000u + (uint64_t)i; }
    double t0 = now_ms(); uint64_t misses = 0, sum = 0;
    for (int it = 0; it < 400000; it++) {
        uint64_t k = (uint64_t)(it & 31), addr = seg_lo[k] + 0x1000ull, bu = 0, oo = 0, run = 0;
        if (fast && vka_addrm4_find(am, 1u, addr, 256u, &bu, &oo, &run)) { sum += bu ^ oo ^ run; continue; }
        misses++;
        pthread_rwlock_rdlock(&g_w9_rw);
        int lo = 0, hi = 64;
        while (lo < hi) { int mid = (lo + hi) / 2; if (seg_hi[mid] <= addr) lo = mid + 1; else hi = mid; }
        bu = seg_buf[lo]; oo = addr - seg_lo[lo]; run = seg_hi[lo] - addr;
        pthread_rwlock_unlock(&g_w9_rw);
        if (fast) vka_addrm4_put(am, 1u, addr, 256u, bu, oo, run);
        sum += bu ^ oo ^ run;
    }
    double dt = now_ms() - t0;
    g_w9_sink ^= sum; *misses_o = misses; free(am);
    return dt;
}

int main(void) {
    const char *e = getenv("T_W9_FAULT");
    g_w9_fault = e ? atoi(e) : 0;
    (void)vka_vin_fast_on();
    if (test_vc_and_vbc() != 0) { fprintf(stderr, "FAIL test_vc_and_vbc fault=%d\n", g_w9_fault); return 1; }
    if (test_addrm4_correctness() != 0) { fprintf(stderr, "FAIL test_addrm4 fault=%d\n", g_w9_fault); return 1; }
    uint64_t m0 = 0, m1 = 0, a0 = 0, a1 = 0;
    double vc_b = bench_vc_materials(0, &m0), vc_f = bench_vc_materials(1, &m1);
    double am_b = bench_addrm4(0, &a0), am_f = bench_addrm4(1, &a1);
    printf("aoi_w9_vc_fast: ops=400000 baseline_ms=%.2f fast_ms=%.2f speedup=%.2fx misses=%llu->%llu\n",
           vc_b, vc_f, vc_f > 0 ? vc_b / vc_f : 0.0, (unsigned long long)m0, (unsigned long long)m1);
    printf("aoi_w9_addr_fast: ops=400000 baseline_ms=%.2f fast_ms=%.2f speedup=%.2fx misses=%llu->%llu\n",
           am_b, am_f, am_f > 0 ? am_b / am_f : 0.0, (unsigned long long)a0, (unsigned long long)a1);
    if (g_w9_fault == 0 && (m0 != 400000 || m1 != 16 || a0 != 400000 || a1 != 32)) return 1;
    printf("t_vin_fast_w9: fault=%d status=PASS sink=%llu\n", g_w9_fault, (unsigned long long)g_w9_sink);
    return 0;
}
