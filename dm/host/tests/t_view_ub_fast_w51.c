// Host unit test, TSan stress test, fault injection & benchmark for Wave 51 (Opt 53: DM_VK_VIEW_UB_FAST).
#ifndef T_W51_TEST_HOOKS
#define T_W51_TEST_HOOKS 1
#endif
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx/vk_android/vka_view_ub_fast.h"

_Atomic int g_vka_view_ub_fast_override = -1;
_Atomic int g_aoi_w51_fault = -1;
static int g_fail = 0;

typedef struct w51_view_node {
    struct w51_view_node *next;
    uint64_t view_u;
    int32_t f;
    uint32_t t, asp, l0, nl, a0, na, sr, sg, sb, sa;
} w51_view_node;

typedef struct {
    uint64_t lo, sz, copied;
} w51_ub_ent;

#define EXPECT_EQ(msg, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w51_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint64_t ref_view_find(const w51_view_node *head, int32_t f, uint32_t t, uint32_t asp,
                              uint32_t l0, uint32_t nl, uint32_t a0, uint32_t na,
                              uint32_t sr, uint32_t sg, uint32_t sb, uint32_t sa) {
    for (const w51_view_node *v = head; v; v = v->next) {
        if (v->f == f && v->t == t && v->asp == asp && v->l0 == l0 && v->nl == nl &&
            v->a0 == a0 && v->na == na && v->sr == sr && v->sg == sg && v->sb == sb && v->sa == sa)
            return v->view_u;
    }
    return UINT64_C(0);
}

static int ref_ub_overlap_count(const w51_ub_ent *ub, int n) {
    int bar = 0;
    for (int i = 0; i < n; i++) {
        uint64_t dst = ub[i].lo, sz = ub[i].sz;
        for (int j = 0; j < i; j++) {
            if (ub[j].lo < dst + sz && dst < ub[j].lo + ub[j].sz) { bar++; break; }
        }
    }
    return bar;
}

static int fast_ub_overlap_count(const w51_ub_ent *ub, int n) {
    int bar = 0;
    vka_range_haz_t haz = {0};
    for (int i = 0; i < n; i++) {
        uint64_t dst = ub[i].lo, sz = ub[i].sz, hi = dst + sz;
        uint64_t mask = vka_range_haz_mask(dst, hi);
        if (vka_range_haz_may_overlap(&haz, dst, hi, mask)) {
            for (int j = 0; j < i; j++) {
                if (ub[j].lo < hi && dst < ub[j].lo + ub[j].sz) { bar++; break; }
            }
        }
        vka_range_haz_add(&haz, dst, hi, mask);
    }
    return bar;
}

static void test_view_and_bpb_maps(void) {
    vka_img_view_map_t *m = calloc(1, sizeof *m);
    assert(m != NULL);
    w51_view_node nodes[32];
    w51_view_node *head = NULL;
    for (uint32_t i = 0u; i < 32u; i++) {
        nodes[i] = (w51_view_node){head, UINT64_C(0x10000) + i, 37 + (int32_t)(i & 3u),
                                   1u, 1u, i & 3u, 1u, 0u, (i >> 2) + 1u,
                                   i & 7u, (i + 1u) & 7u, (i + 2u) & 7u, (i + 3u) & 7u};
        head = &nodes[i];
        uint64_t k0 = 0u, k1 = 0u;
        EXPECT_EQ("pack_ok", 1, vka_iview_key_pack(nodes[i].f, nodes[i].t, nodes[i].asp,
                  nodes[i].l0, nodes[i].nl, nodes[i].a0, nodes[i].na,
                  nodes[i].sr, nodes[i].sg, nodes[i].sb, nodes[i].sa, &k0, &k1));
        uint32_t tg = vka_img_view_map_put(m, 1u, k0, k1, nodes[i].view_u);
        EXPECT_EQ("tgen_1", 1u, tg);
    }
    uint64_t k0a = 0u, k1a = 0u, k0b = 0u, k1b = 0u, out = 0u;
    EXPECT_EQ("pack_a", 1, vka_iview_key_pack(37, 1u, 1u, 0u, 1u, 0u, 1u, 2u, 3u, 4u, 5u, &k0a, &k1a));
    EXPECT_EQ("pack_b", 1, vka_iview_key_pack(37, 1u, 1u, 0u, 1u, 0u, 2u, 2u, 3u, 4u, 5u, &k0b, &k1b));
    EXPECT_EQ("nlayers_diff_k1", 1, k1a != k1b);
    vka_img_view_map_put(m, 2u, k0a, k1a, UINT64_C(0xabc1));
    vka_img_view_map_put(m, 2u, k0a, k1b, UINT64_C(0xabc2));
    EXPECT_EQ("find_a", 1, vka_img_view_map_find(m, 2u, k0a, k1a, &out));
    EXPECT_EQ("val_a", UINT64_C(0xabc1), out);
    EXPECT_EQ("find_b", 1, vka_img_view_map_find(m, 2u, k0a, k1b, &out));
    EXPECT_EQ("val_b", UINT64_C(0xabc2), out);
    for (uint32_t nl = 3u; nl < 65536u; nl++) {
        uint64_t ck0 = 0u, ck1 = 0u;
        vka_iview_key_pack(37, 1u, 1u, 0u, 1u, 0u, nl, 2u, 3u, 4u, 5u, &ck0, &ck1);
        if (vka_ivm_slot_hash(2u, ck0, ck1) == vka_ivm_slot_hash(2u, k0a, k1a)) {
            EXPECT_EQ("vk1_col_miss", 0, vka_img_view_map_find(m, 2u, ck0, ck1, &out));
            break;
        }
    }
    uint64_t col_ep = 77u;
    for (uint64_t ep = 100u; ep < 20000u; ep++) {
        if (vka_ivm_slot_hash(ep, k0a, k1a) == vka_ivm_slot_hash(col_ep, k0b, k1b)) {
            vka_img_view_map_put(m, col_ep, k0b, k1b, UINT64_C(0xbeef1));
            EXPECT_EQ("col_miss", 0, vka_img_view_map_find(m, ep, k0a, k1a, &out));
            break;
        }
    }
    for (uint64_t i = 0u; i < 8000u; i++) {
        vka_img_view_map_put(m, UINT64_C(1000) + i, k0a, k1a, UINT64_C(0x20000) + i);
    }
    EXPECT_EQ("tgen_roll", 2u, m->tgen);
    EXPECT_EQ("stale_miss", 0, vka_img_view_map_find(m, 2u, k0a, k1a, &out));
    vka_vl_bpb_memo_t bm = {0};
    vka_vl_bpb_put(&bm, 37, 4);
    vka_vl_bpb_put(&bm, 97, 16);
    int bpb = 0;
    EXPECT_EQ("bpb_hit", 1, vka_vl_bpb_get(&bm, 37, &bpb));
    EXPECT_EQ("bpb_4", 4, bpb);
    EXPECT_EQ("bpb_hit2", 1, vka_vl_bpb_get(&bm, 97, &bpb));
    EXPECT_EQ("bpb_16", 16, bpb);
    free(m);
}

static void test_haz_inst_and_pps(void) {
    w51_ub_ent ubs[128];
    for (int i = 0; i < 128; i++) {
        ubs[i].lo = UINT64_C(0x80000000) + (uint64_t)i * UINT64_C(512);
        ubs[i].sz = 64u;
    }
    ubs[60].lo = ubs[12].lo + 16u;
    ubs[95].lo = ubs[40].lo;
    EXPECT_EQ("haz_parity", ref_ub_overlap_count(ubs, 128), fast_ub_overlap_count(ubs, 128));
    EXPECT_EQ("haz_2", 2, fast_ub_overlap_count(ubs, 128));
    vka_inst_var_map_t ivm = {0};
    vka_inst_var_map_put(&ivm, UINT64_C(0xfeed), 0u, 1u);
    vka_inst_var_map_put(&ivm, UINT64_C(0xfeed), 1u, 2u);
    EXPECT_EQ("ivar_b0", 0, vka_inst_var_map_find(&ivm, UINT64_C(0xfeed), 0u));
    EXPECT_EQ("ivar_b1", 1, vka_inst_var_map_find(&ivm, UINT64_C(0xfeed), 1u));
    for (uint32_t b = 2u; b < 65536u; b++) {
        if (vka_ivar_hash(UINT64_C(0xfeed), b) == vka_ivar_hash(UINT64_C(0xfeed), 0u)) {
            EXPECT_EQ("ivar_miss", -1, vka_inst_var_map_find(&ivm, UINT64_C(0xfeed), b));
            break;
        }
    }
    vka_pps_mask_t pm = {0};
    vka_pps_note_slot(&pm, 5u, 3);
    vka_pps_note_slot(&pm, 19u, 3);
    EXPECT_EQ("pps_p3", (UINT32_C(1) << 5) | (UINT32_C(1) << 19), vka_pps_cand_mask(&pm, 3));
    vka_pps_note_slot(&pm, 5u, 7);
    EXPECT_EQ("pps_p3_after", UINT32_C(1) << 19, vka_pps_cand_mask(&pm, 3));
    EXPECT_EQ("pps_p7", UINT32_C(1) << 5, vka_pps_cand_mask(&pm, 7));
}

typedef struct {
    int tid;
    pthread_mutex_t *mu;
    vka_img_view_map_t *vm;
} w51_targs;

static void *w51_tsan_worker(void *arg) {
    w51_targs *a = (w51_targs *)arg;
    for (int it = 0; it < 200; it++) {
        uint64_t k0 = 0u, k1 = 0u, out = 0u;
        vka_iview_key_pack(37 + (it & 3), 1u, 1u, (uint32_t)(it & 3), 1u, 0u, 1u,
                           2u, 3u, 4u, 5u, &k0, &k1);
        pthread_mutex_lock(a->mu);
        uint64_t ep = UINT64_C(10) + (uint64_t)(it & 7);
        if (!vka_img_view_map_find(a->vm, ep, k0, k1, &out)) {
            vka_img_view_map_put(a->vm, ep, k0, k1, UINT64_C(0x50000) + ep * 16u + (uint64_t)(it & 3));
        }
        pthread_mutex_unlock(a->mu);
    }
    return NULL;
}

static void test_tsan(void) {
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    vka_img_view_map_t *vm = calloc(1, sizeof *vm);
    assert(vm != NULL);
    pthread_t th[4];
    w51_targs args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (w51_targs){i, &mu, vm};
        pthread_create(&th[i], NULL, w51_tsan_worker, &args[i]);
    }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    free(vm);
}

static uint64_t bench_one_pass(int fast, double *ms_out, const w51_view_node *head,
                               const w51_view_node *nodes, const vka_img_view_map_t *vm,
                               const w51_ub_ent *ubs) {
    uint64_t sink = 0u, t0 = w51_now_ns();
    for (int rep = 0; rep < 2000; rep++) {
        for (int i = 0; i < 24; i++) {
            const w51_view_node *q = &nodes[i];
            if (fast) {
                uint64_t k0 = 0u, k1 = 0u, out = 0u;
                vka_iview_key_pack(q->f, q->t, q->asp, q->l0, q->nl, q->a0, q->na,
                                   q->sr, q->sg, q->sb, q->sa, &k0, &k1);
                if (vka_img_view_map_find(vm, 1u, k0, k1, &out)) sink += out;
            } else {
                sink += ref_view_find(head, q->f, q->t, q->asp, q->l0, q->nl, q->a0, q->na,
                                      q->sr, q->sg, q->sb, q->sa);
            }
        }
        sink += (uint64_t)(fast ? fast_ub_overlap_count(ubs, 64) : ref_ub_overlap_count(ubs, 64));
    }
    *ms_out = (double)(w51_now_ns() - t0) / 1000000.0;
    return sink;
}

static void run_bench(void) {
    vka_img_view_map_t *vm = calloc(1, sizeof *vm);
    assert(vm != NULL);
    w51_view_node nodes[24];
    w51_view_node *head = NULL;
    for (uint32_t i = 0u; i < 24u; i++) {
        nodes[i] = (w51_view_node){head, UINT64_C(0x1000) + i, 37 + (int32_t)(i & 3u),
                                   1u, 1u, i & 7u, 1u, (i >> 3) + 1u, 1u, 2u, 3u, 4u, 5u};
        head = &nodes[i];
        uint64_t k0 = 0u, k1 = 0u;
        vka_iview_key_pack(nodes[i].f, nodes[i].t, nodes[i].asp, nodes[i].l0, nodes[i].nl,
                           nodes[i].a0, nodes[i].na, nodes[i].sr, nodes[i].sg, nodes[i].sb,
                           nodes[i].sa, &k0, &k1);
        vka_img_view_map_put(vm, 1u, k0, k1, nodes[i].view_u);
    }
    w51_ub_ent ubs[64];
    for (int i = 0; i < 64; i++) {
        ubs[i].lo = UINT64_C(0x90000000) + (uint64_t)i * UINT64_C(512);
        ubs[i].sz = 64u;
    }
    double s_ms = 0.0, f_ms = 0.0;
    uint64_t s_sink = bench_one_pass(0, &s_ms, head, nodes, vm, ubs);
    uint64_t f_sink = bench_one_pass(1, &f_ms, head, nodes, vm, ubs);
    EXPECT_EQ("bench_sink", s_sink, f_sink);
    printf("t_view_ub_fast_w51: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu status=%s\n",
           s_ms, f_ms, f_ms > 0.0 ? s_ms / f_ms : 0.0, (unsigned long long)f_sink, g_fail ? "FAIL" : "PASS");
    free(vm);
}

int main(void) {
    vka_view_ub_fast_set_for_test(1);
    EXPECT_EQ("switch_1", 1, vka_view_ub_fast_on());
    vka_view_ub_fast_set_for_test(0);
    EXPECT_EQ("switch_0", 0, vka_view_ub_fast_on());
    vka_view_ub_fast_set_for_test(1);
    test_view_and_bpb_maps();
    test_haz_inst_and_pps();
    test_tsan();
    run_bench();
    return g_fail ? 1 : 0;
}
