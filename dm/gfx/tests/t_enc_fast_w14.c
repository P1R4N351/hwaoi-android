#define _POSIX_C_SOURCE 200809L
#define W14_TEST_HOOKS 1
#include "../vk_android/vka_enc_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_w14_fault = 0;
static volatile uint64_t g_w14_sink;

typedef struct { const void *pool; int base, cap; } sim_dpool_ent;
typedef struct { sim_dpool_ent e[16]; int n, next; } sim_dpool_tab;

typedef struct { uint32_t op, size; } sim_hdr;
typedef struct { int32_t stage, index; uint64_t addr, size; } sim_bind_buf;
typedef struct { uint16_t stage, index; uint32_t size; uint64_t addr; } sim_ubo_small;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static void test_spsc_and_fault1(void) {
    _Atomic uint64_t tail = 0;
    uint64_t shadow = 0, cap = 4096u;
    if (vka_spsc_has_room(4096u, 64u, cap, &shadow, &tail) != 0) {
        fprintf(stderr, "FAIL: spsc room overflow ignored\n");
        exit(1);
    }
    assert(vka_spsc_has_room(1000u, 64u, cap, &shadow, &tail) == 1);
    atomic_store_explicit(&tail, 256u, memory_order_release);
    assert(vka_spsc_has_room(4096u, 128u, cap, &shadow, &tail) == 1);
    assert(shadow == 256u);
}

static void test_dpool_and_fault2(void) {
    _Atomic uint32_t seq = 0u;
    sim_dpool_tab tab = {0};
    int a = 1, b = 2;
    tab.e[0] = (sim_dpool_ent){&a, 0, 1024};
    tab.e[1] = (sim_dpool_ent){&b, 1024, 512};
    tab.n = 2;

    int base = -1;
    assert(vka_dpool_seq_find(&seq, tab.e, sizeof tab.e[0], tab.n, &b, &base) == 1);
    assert(base == 1024);

    atomic_store_explicit(&seq, 1u, memory_order_release);
    base = 777;
    int got = vka_dpool_seq_find(&seq, tab.e, sizeof tab.e[0], tab.n, &b, &base);
    if (got != 0) {
        fprintf(stderr, "FAIL: odd seqlock accepted torn read\n");
        exit(1);
    }
}

static void test_pack_and_fault3(void) {
    uint64_t pairs[8] = {0x1000u, 256u, 0x2000u, 512u, 0x3000u, 1024u, 0x4000u, 2048u};
    uint8_t fast_u[96], ref_u[96], fast_s[128], ref_s[128];
    memset(fast_u, 0, sizeof fast_u);
    memset(ref_u, 0, sizeof ref_u);
    memset(fast_s, 0, sizeof fast_s);
    memset(ref_s, 0, sizeof ref_s);

    vka_pack_ubo_small_batch(fast_u, 45u, 2u, 3u, 4, pairs);
    for (int i = 0; i < 4; i++) {
        sim_hdr h = {45u, 24u};
        sim_ubo_small u = {2u, (uint16_t)(3 + i), (uint32_t)pairs[2 * i + 1], pairs[2 * i]};
        memcpy(ref_u + (size_t)i * 24u, &h, 8);
        memcpy(ref_u + (size_t)i * 24u + 8, &u, sizeof u);
    }
    if (memcmp(fast_u, ref_u, sizeof ref_u) != 0) {
        fprintf(stderr, "FAIL: ubo_small batch mismatch\n");
        exit(1);
    }

    vka_pack_ssbo_batch(fast_s, 20u, 1, 2, 4, pairs);
    for (int i = 0; i < 4; i++) {
        sim_hdr h = {20u, 32u};
        sim_bind_buf s = {1, 2 + i, pairs[2 * i], pairs[2 * i + 1]};
        memcpy(ref_s + (size_t)i * 32u, &h, 8);
        memcpy(ref_s + (size_t)i * 32u + 8, &s, sizeof s);
    }
    if (memcmp(fast_s, ref_s, sizeof ref_s) != 0) {
        fprintf(stderr, "FAIL: ssbo batch mismatch\n");
        exit(1);
    }
}

__attribute__((noinline)) static int sim_dpool_mutex_find(
    pthread_mutex_t *mu, const sim_dpool_tab *tab, const void *pool) {
    pthread_mutex_lock(mu);
    int b = 0;
    for (int i = 0; i < tab->n; i++) {
        if (tab->e[i].pool == pool) { b = tab->e[i].base; break; }
    }
    pthread_mutex_unlock(mu);
    return b;
}

__attribute__((noinline)) static int sim_dpool_seq_find(
    const _Atomic uint32_t *seq, const sim_dpool_tab *tab, const void *pool) {
    int b = 0;
    return vka_dpool_seq_find(seq, tab->e, sizeof tab->e[0], tab->n, pool, &b) ? b : 0;
}

static void bench_dpool_seq(void) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    _Atomic uint32_t seq = 0u;
    int p0 = 0, p1 = 1, p2 = 2, p3 = 3;
    const void *ptrs[4] = {&p0, &p1, &p2, &p3};
    sim_dpool_tab tab = {0};
    for (int i = 0; i < 4; i++) tab.e[i] = (sim_dpool_ent){ptrs[i], i * 1024, 1024};
    tab.n = 4;

    uint64_t sb = 0, so = 0;
    double t0 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        sb += (uint64_t)sim_dpool_mutex_find(&mu, &tab, ptrs[i & 3u]);
    }
    double ms_b = now_ms() - t0;

    double t1 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        so += (uint64_t)sim_dpool_seq_find(&seq, &tab, ptrs[i & 3u]);
    }
    double ms_o = now_ms() - t1;
    assert(sb == so);
    g_w14_sink ^= sb ^ so;
    printf("aoi_w14_dpool_seq: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
}

__attribute__((noinline)) static void *sim_rec_alloc_zero(
    uint8_t *ring, size_t *off, uint32_t op, size_t body) {
    size_t tot = 8u + body;
    if (*off + tot > 4096u) *off = 0u;
    uint8_t *p = ring + *off;
    *off += tot;
    uint32_t h[2] = {op, (uint32_t)tot};
    memcpy(p, h, 8);
    memset(p + 8, 0, body);
    return p + 8;
}

__attribute__((noinline)) static void sim_pack_ubo_batch(
    uint8_t *ring, size_t *off, const uint64_t *pairs) {
    if (*off + 96u > 4096u) *off = 0u;
    vka_pack_ubo_small_batch(ring + *off, 45u, 0u, 0u, 4, pairs);
    *off += 96u;
}

static void bench_pack_ubo(void) {
    uint64_t pairs[8] = {0x100000u, 256u, 0x100100u, 256u, 0x100200u, 256u, 0x100300u, 256u};
    uint8_t ring_b[4096], ring_o[4096];
    memset(ring_b, 0, sizeof ring_b);
    memset(ring_o, 0, sizeof ring_o);
    size_t off_b = 0, off_o = 0;
    uint64_t pb = 0, po = 0;

    double t0 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        pairs[6] = 0x100300u + (i & 15u);
        for (int k = 0; k < 4; k++) {
            sim_ubo_small *u = (sim_ubo_small *)sim_rec_alloc_zero(ring_b, &off_b, 45u, sizeof *u);
            *u = (sim_ubo_small){0u, (uint16_t)k, (uint32_t)pairs[2 * k + 1], pairs[2 * k]};
        }
        pb += ring_b[(off_b - 24u) + 16u];
    }
    double ms_b = now_ms() - t0;

    double t1 = now_ms();
    for (uint32_t i = 0; i < 400000u; i++) {
        pairs[6] = 0x100300u + (i & 15u);
        sim_pack_ubo_batch(ring_o, &off_o, pairs);
        po += ring_o[(off_o - 24u) + 16u];
    }
    double ms_o = now_ms() - t1;
    assert(pb == po);
    g_w14_sink ^= pb ^ po;
    printf("aoi_w14_pack_ubo: base=%.2fms opt=%.2fms speedup=%.2fx\n",
           ms_b, ms_o, ms_b / (ms_o > 0.01 ? ms_o : 0.01));
}

int main(void) {
    const char *f = getenv("T_W14_FAULT");
    g_w14_fault = f ? atoi(f) : 0;
    assert(vka_enc_fast_on() == 0 || vka_enc_fast_on() == 1);
    test_spsc_and_fault1();
    test_dpool_and_fault2();
    test_pack_and_fault3();
    bench_dpool_seq();
    bench_pack_ubo();
    printf("t_enc_fast_w14: fault=%d PASS\n", g_w14_fault);
    return 0;
}
