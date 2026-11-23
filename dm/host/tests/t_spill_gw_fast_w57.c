// Standalone unit, fault-injection, TSan & -O2 microbenchmark for Wave 57 (vka_spill_gw_fast.h).
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef T_W57_TEST_HOOKS
#define T_W57_TEST_HOOKS
#endif
#include "vka_spill_gw_fast.h"

_Atomic int g_vka_spill_gw_fast_override = -1;
_Atomic int g_aoi_w57_fault = -1;

static void w57_req(int ok) {
    if (!ok) abort();
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct { uint8_t kind, kind2, k0, k1; uint32_t klen; uint64_t key[5]; } w57_rec_t;

static void ref_fill_quad_ib(uint32_t *ix, uint32_t nquads) {
    for (uint32_t q = 0u; q < nquads; q++) {
        uint32_t v = q * 4u;
        ix[(size_t)q * 6u + 0u] = v;
        ix[(size_t)q * 6u + 1u] = v + 1u;
        ix[(size_t)q * 6u + 2u] = v + 2u;
        ix[(size_t)q * 6u + 3u] = v;
        ix[(size_t)q * 6u + 4u] = v + 2u;
        ix[(size_t)q * 6u + 5u] = v + 3u;
    }
}

static void check_btraits_and_gw(void) {
    vka_sh_btraits_t bt = {0u, 0u};
    vka_sh_btraits_step(&bt, 0, 0u, 0);
    w57_req(bt.flags == VKA_SH_BF_READY && bt.spill_bytes == 0u);
    vka_sh_btraits_step(&bt, 1, 8192u, 1);
    w57_req((bt.flags & (VKA_SH_BF_READY | VKA_SH_BF_WRITES | VKA_SH_BF_SPILL)) ==
            (VKA_SH_BF_READY | VKA_SH_BF_WRITES | VKA_SH_BF_SPILL));
    w57_req(bt.spill_bytes == 8192u);
    vka_sh_btraits_step(&bt, 1, 16384u, 0);
    w57_req(bt.spill_bytes == 8192u);

    uint64_t minf = UINT64_MAX;
    w57_req(vka_gw_has_landed(minf, 100u) == 0);
    vka_gw_min_fence_note(&minf, 50u);
    vka_gw_min_fence_note(&minf, 80u);
    w57_req(minf == 50u);
    w57_req(vka_gw_has_landed(minf, 49u) == 0);
    w57_req(vka_gw_has_landed(minf, 50u) == 1);

    w57_req(vka_gw_can_dedup(0u) == 0);
    w57_req(vka_gw_can_dedup(3u) == 1);
    w57_req(vka_gw_dedup_window(20, 5u) == 15);
    w57_req(vka_gw_dedup_window(20, 32u) == 4);
}

static void check_quad_bloom_and_pw(void) {
    uint32_t qref[6 * 37], qfast[6 * 37];
    ref_fill_quad_ib(qref, 37u);
    vka_fill_quad_ib_u32(qfast, 37u);
    w57_req(memcmp(qref, qfast, sizeof qref) == 0);

    vka_pool_bloom_t blm = {0};
    uintptr_t p0 = 0x10040000u, p1 = 0x20080000u, p2 = 0x300c0000u;
    w57_req(vka_pool_bloom_may(&blm, p0) == 0);
    vka_pool_bloom_add(&blm, p0);
    vka_pool_bloom_add(&blm, p1);
    w57_req(vka_pool_bloom_may(&blm, p0) == 1 && vka_pool_bloom_may(&blm, p1) == 1);
    vka_pool_bloom_store(&blm, vka_pool_bloom_bits(p2));
    w57_req(vka_pool_bloom_may(&blm, p2) == 1);

    static uint8_t buf[4096];
    size_t len = 0u;
    for (uint32_t i = 0u; i < 40u; i++) {
        w57_rec_t r = {0};
        r.kind = (uint8_t)(i % 3u);
        r.klen = 16u;
        r.key[0] = UINT64_C(0x1000) + (i / 4u);
        r.key[1] = UINT64_C(0x2000) + (i % 2u);
        memcpy(buf + len, &r, sizeof r);
        memset(buf + len + sizeof r, (int)(i & 0xffu), 16u);
        len += sizeof r + 16u;
    }
    vka_pw_sh_idx_t ix = {0};
    w57_req(vka_pw_sh_idx_build(&ix, buf, len, sizeof(w57_rec_t), offsetof(w57_rec_t, klen)) == 1);
    uint64_t want[5] = {UINT64_C(0x1002), UINT64_C(0x2000), 0u, 0u, 0u};
    int ref_hits = 0, fast_hits = 0;
    for (size_t o = 0u; o + sizeof(w57_rec_t) <= len;) {
        w57_rec_t r;
        memcpy(&r, buf + o, sizeof r);
        o += sizeof r + ((r.klen + 7u) & ~(size_t)7);
        if (r.kind == 1u && memcmp(r.key, want, sizeof want) == 0) ref_hits++;
    }
    uint32_t prev_off = 0u, has_prev = 0u;
    for (uint32_t cur = vka_pw_sh_idx_first(&ix, 1u, want); cur; cur = ix.next[cur - 1u]) {
        uint32_t off = ix.offs[cur - 1u];
        if (has_prev) w57_req(off > prev_off);
        prev_off = off;
        has_prev = 1u;
        w57_rec_t r;
        memcpy(&r, buf + off, sizeof r);
        if (r.kind == 1u && memcmp(r.key, want, sizeof want) == 0) fast_hits++;
    }
    w57_req(ref_hits == fast_hits && ref_hits > 0);
    vka_pw_sh_idx_free(&ix);
}

static void *thread_worker(void *arg) {
    int tid = *(int *)arg;
    for (int it = 0; it < 200; it++) {
        vka_spill_gw_fast_set_for_test((tid + it) & 1);
        (void)vka_spill_gw_fast_on();
        check_btraits_and_gw();
    }
    return NULL;
}

static uint64_t run_bench_arm(int fast, const uint8_t *pw_buf, size_t pw_len, const vka_pw_sh_idx_t *ix) {
    static uint32_t ib_buf[65536];
    uint64_t sink = 0u;
    uint64_t want[5] = {UINT64_C(0x1005), UINT64_C(0x2001), 0u, 0u, 0u};
    uint32_t fence_arr[64];
    for (uint32_t i = 0u; i < 64u; i++) fence_arr[i] = 1000u + i;
    for (int it = 0; it < 2000; it++) {
        if (fast) {
            if (vka_gw_has_landed(UINT64_C(1000), UINT64_C(999))) sink++;
            vka_fill_quad_ib_u32(ib_buf, 1024u);
            sink += ib_buf[it & 1023];
            for (uint32_t c = vka_pw_sh_idx_first(ix, 2u, want); c; c = ix->next[c - 1u]) {
                w57_rec_t r;
                memcpy(&r, pw_buf + ix->offs[c - 1u], sizeof r);
                if (r.kind == 2u && memcmp(r.key, want, sizeof want) == 0) sink += r.klen;
            }
        } else {
            int any = 0;
            for (int i = 0; i < 64 && !any; i++) any = fence_arr[i] <= 999u;
            if (any) sink++;
            ref_fill_quad_ib(ib_buf, 1024u);
            sink += ib_buf[it & 1023];
            for (size_t o = 0u; o + sizeof(w57_rec_t) <= pw_len;) {
                w57_rec_t r;
                memcpy(&r, pw_buf + o, sizeof r);
                o += sizeof r + ((r.klen + 7u) & ~(size_t)7);
                if (r.kind == 2u && memcmp(r.key, want, sizeof want) == 0) sink += r.klen;
            }
        }
    }
    return sink;
}

static void check_benchmark(void) {
    static uint8_t pw_buf[4096u * 64u];
    size_t pw_len = 0u;
    for (uint32_t i = 0u; i < 2048u; i++) {
        w57_rec_t r = {0};
        r.kind = (uint8_t)(i % 3u);
        r.klen = 16u;
        r.key[0] = UINT64_C(0x1000) + (i % 128u);
        r.key[1] = UINT64_C(0x2000) + (i % 2u);
        memcpy(pw_buf + pw_len, &r, sizeof r);
        pw_len += sizeof r + 16u;
    }
    vka_pw_sh_idx_t ix = {0};
    w57_req(vka_pw_sh_idx_build(&ix, pw_buf, pw_len, sizeof(w57_rec_t), offsetof(w57_rec_t, klen)) == 1);
    uint64_t t0 = now_ns(), s0 = run_bench_arm(0, pw_buf, pw_len, &ix);
    uint64_t t1 = now_ns(), s1 = run_bench_arm(1, pw_buf, pw_len, &ix), t2 = now_ns();
    w57_req(s0 == s1);
    double ms0 = (double)(t1 - t0) / 1e6, ms1 = (double)(t2 - t1) / 1e6;
    printf("t_spill_gw_fast_w57: baseline=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=PASS\n",
           ms0, ms1, ms0 / (ms1 > 0.001 ? ms1 : 0.001), (unsigned long long)s1);
    vka_pw_sh_idx_free(&ix);
}

int main(void) {
    pthread_t th[4];
    int tids[4] = {0, 1, 2, 3};
    check_btraits_and_gw();
    check_quad_bloom_and_pw();
    for (int i = 0; i < 4; i++) w57_req(pthread_create(&th[i], NULL, thread_worker, &tids[i]) == 0);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    vka_spill_gw_fast_set_for_test(-1);
    check_benchmark();
    return 0;
}
