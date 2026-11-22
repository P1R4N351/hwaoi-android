#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W46_TEST_HOOKS 1
#include "vka_bind_fast.h"
#include "vka_bind.c"

_Atomic int g_vka_bind_fast_override = -1;
_Atomic int g_aoi_w46_fault = -1;
static int g_fail = 0;

#define EXPECT_EQ(m, w, g) do { \
    uint64_t wa_ = (uint64_t)(w), ga_ = (uint64_t)(g); \
    if (wa_ != ga_) { g_fail++; fprintf(stderr, "FAIL %s:%d %s want=%llu got=%llu\n", __FILE__, __LINE__, m, (unsigned long long)wa_, (unsigned long long)ga_); } \
} while (0)

static uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct { uint8_t mem[4096]; uint64_t calls; size_t fail_after; } rd_buf_t;
static int test_rd(void *ctx, uint64_t addr, void *out, size_t n) {
    rd_buf_t *m = (rd_buf_t *)ctx;
    m->calls++;
    if (addr < 0x10000ULL || addr + n > 0x10000ULL + sizeof m->mem) return 0;
    size_t off = (size_t)(addr - 0x10000ULL);
    if (m->fail_after && off + n > m->fail_after) return 0;
    memcpy(out, m->mem + off, n);
    return 1;
}

static void init_bstate(vka_bstate *s) {
    memset(s, 0, sizeof *s);
    s->tex_base = 100; s->samp_base = 200; s->base_instance = 77u; s->samples = 4u;
    s->dispatch[0] = 3u; s->dispatch[1] = 5u; s->dispatch[2] = 7u;
    for (int st = 0; st < VKA_NSTAGE; st++) {
        uint64_t su = (uint64_t)(unsigned)st;
        s->ubo[st][0] = (vka_range){0x10000ULL, 1024u};
        s->ubo[st][2] = (vka_range){0x10400ULL, 1024u};
        s->ssbo[st][0] = (vka_range){0x80000ULL + su * 0x1000ULL, 256u};
        s->ssbo[st][15] = (vka_range){0x90000ULL + su * 0x1000ULL, 512u};
        s->tex[st][1] = (101ULL + su) | ((201ULL + su) << 20);
        s->tex[st][34] = (134ULL + su) | ((210ULL + su) << 20);
        s->tex[st][35] = (135ULL + su) | ((211ULL + su) << 20);
        s->tex[st][DMSH_SEPTEX_SLOT0 + 5] = (150ULL + su) | ((220ULL + su) << 20);
        s->samp[st][6] = (230ULL + su) << 20;
        s->img[st][3] = 303ULL + su;
    }
    s->push[0][11] = 0x5au; s->push[1][22] = 0xa5u;
}

static void test_c2_drv_sup(const vka_bstate *base) {
    vka_bstate s = *base;
    uint8_t d0[DMSH_DRV_SIZE], d1[DMSH_DRV_SIZE], sup0[4096], sup1[4096];
    for (unsigned b = 0; b <= DMSH_DRV_SIZE + 16u; b += 4u) {
        vka_bind_fast_set_for_test(0); uint64_t w = vka_c2_handle(&s, 1, b);
        vka_bind_fast_set_for_test(1); uint64_t g = vka_c2_handle(&s, 1, b);
        EXPECT_EQ("c2 handle parity", w, g);
    }
    for (int n2 = 0; n2 <= 1; n2++) {
        s.nvn2 = (uint32_t)n2;
        for (int st = 0; st < VKA_NSTAGE; st++) {
            for (int bank = 0; bank <= 2; bank += 2) {
                for (int pool = 0; pool <= 1; pool++) {
                    s.samples = (st == 1 && pool) ? 16u : 4u;
                    vka_bind_fast_set_for_test(0); vka_drv_fill(&s, st, bank, pool, d0);
                    vka_bind_fast_set_for_test(1); vka_drv_fill(&s, st, bank, pool, d1);
                    EXPECT_EQ("drv fill parity", 0, memcmp(d0, d1, DMSH_DRV_SIZE));
                }
            }
        }
    }
    s.tex_base = 0; s.samp_base = 0;
    vka_bind_fast_set_for_test(0); vka_drv_fill(&s, 1, 2, 1, d0);
    vka_bind_fast_set_for_test(1); vka_drv_fill(&s, 1, 2, 1, d1);
    EXPECT_EQ("drv c2 raw parity", 0, memcmp(d0, d1, DMSH_DRV_SIZE));
    vka_bind_fast_set_for_test(0); vka_drv_fill(&s, 5, DMSH_PUSH_BANK, 0, d0);
    vka_bind_fast_set_for_test(1); vka_drv_fill(&s, 5, DMSH_PUSH_BANK, 0, d1);
    EXPECT_EQ("push bank parity", 0, memcmp(d0, d1, DMSH_PUSH_SIZE));
    size_t szs[3] = {128u, VKA_SUPPORT_SIZE, 4096u};
    for (int i = 0; i < 3; i++) {
        vka_bind_fast_set_for_test(0); vka_support_fill(sup0, szs[i]);
        vka_bind_fast_set_for_test(1); vka_support_fill(sup1, szs[i]);
        EXPECT_EQ("support parity", 0, memcmp(sup0, sup1, szs[i]));
    }
}

static void test_walk_parity(const vka_bstate *s) {
    rd_buf_t rm0 = {0}, rm1 = {0};
    for (uint32_t k = 0; k < 32u; k++) {
        uint32_t th = (10u + k) | ((20u + k) << 20);
        uint32_t sh = ((30u + k) << 20);
        memcpy(rm0.mem + 8u * k, &th, 4u); memcpy(rm0.mem + 0x400u + 8u * k, &sh, 4u);
    }
    rm1 = rm0;
    dmsh_vk_bind bs[9] = {
        {.kind = DMSH_VK_KIND_CBUF, .desc = 6, .binding = 0, .count = 1, .reg = 0},
        {.kind = DMSH_VK_KIND_C1, .desc = 6, .binding = 1, .count = 1, .reg = -1},
        {.kind = DMSH_VK_KIND_SUPPORT, .desc = 6, .binding = 2, .count = 1, .reg = -1},
        {.kind = DMSH_VK_KIND_DRVCB, .desc = 6, .binding = 3, .count = 1, .reg = 0},
        {.kind = DMSH_VK_KIND_SBUF, .desc = 7, .binding = 4, .count = 1, .reg = 15},
        {.kind = DMSH_VK_KIND_TEX_UBO, .desc = 1, .binding = 5, .count = 32, .reg = 0, .off = 0, .skind = DMSH_VK_KIND_SAMP_UBO, .sreg = 2, .soff = 0},
        {.kind = DMSH_VK_KIND_TEX_UBO, .desc = 1, .binding = 6, .count = 16, .reg = 0, .off = 0, .skind = DMSH_VK_KIND_SAMP_UBO, .sreg = 0, .soff = 0},
        {.kind = DMSH_VK_KIND_IMG_UBO, .desc = 3, .binding = 7, .count = 8, .reg = 0, .off = 0},
        {.kind = DMSH_VK_KIND_SAMP_UBO, .desc = 0, .binding = 8, .count = 8, .reg = 2, .off = 0},
    };
    dmsh_vk_entry e = {.bind_count = 9};
    vka_res r0[128], r1[128]; char w0[128] = "", w1[128] = ""; int p0 = 0, p1 = 0;
    vka_bind_fast_set_for_test(0);
    int n0 = vka_walk(s, 1, &e, bs, DMSH_VK_SSBO_ALIGN, 64, test_rd, &rm0, r0, 128, &p0, w0, sizeof w0);
    vka_bind_fast_set_for_test(1);
    int n1 = vka_walk(s, 1, &e, bs, DMSH_VK_SSBO_ALIGN, 64, test_rd, &rm1, r1, 128, &p1, w1, sizeof w1);
    EXPECT_EQ("walk n", n0, n1); EXPECT_EQ("walk pool", p0, p1);
    EXPECT_EQ("walk res", 0, memcmp(r0, r1, (size_t)n0 * sizeof r0[0]));
    rm0.fail_after = 128u; rm1.fail_after = 128u;
    vka_bind_fast_set_for_test(0);
    n0 = vka_walk(s, 1, &e, bs, DMSH_VK_SSBO_ALIGN, 64, test_rd, &rm0, r0, 128, &p0, w0, sizeof w0);
    vka_bind_fast_set_for_test(1);
    n1 = vka_walk(s, 1, &e, bs, DMSH_VK_SSBO_ALIGN, 64, test_rd, &rm1, r1, 128, &p1, w1, sizeof w1);
    EXPECT_EQ("partial n", n0, n1); EXPECT_EQ("partial res", 0, memcmp(r0, r1, (size_t)n0 * sizeof r0[0]));
    vka_bind_fast_set_for_test(1);
    EXPECT_EQ("bad stage", VKA_WALK_REFUSE, vka_walk(s, 9, &e, bs, 0, 64, test_rd, &rm1, r1, 128, NULL, w1, sizeof w1));
}

static void *tsan_worker(void *arg) {
    (void)arg;
    uint8_t buf[VKA_SUPPORT_SIZE];
    for (int i = 0; i < 200; i++) {
        vka_support_fill(buf, sizeof buf);
        float f = 0.0f; memcpy(&f, buf + 192, 4);
        if (f != 1.0f) return (void *)(uintptr_t)1;
    }
    return NULL;
}

static uint64_t bench_arm(const vka_bstate *s, int fast, double *ms_out) {
    rd_buf_t rm = {0};
    for (uint32_t k = 0; k < 32u; k++) { uint32_t v = (10u + k) | ((20u + k) << 20); memcpy(rm.mem + 8u * k, &v, 4u); }
    dmsh_vk_bind bs[2] = {
        {.kind = DMSH_VK_KIND_TEX_UBO, .desc = 1, .binding = 0, .count = 32, .reg = 0, .off = 0, .skind = DMSH_VK_KIND_SAMP_UBO, .sreg = 0, .soff = 0},
        {.kind = DMSH_VK_KIND_TEX_UBO, .desc = 1, .binding = 1, .count = 16, .reg = 2, .off = 0, .skind = DMSH_VK_KIND_SAMP_UBO, .sreg = 2, .soff = 0},
    };
    dmsh_vk_entry e = {.bind_count = 2};
    vka_res res[64]; char why[64]; uint8_t seg[DMSH_DRV_SIZE], sup[VKA_SUPPORT_SIZE];
    vka_bind_fast_set_for_test(fast);
    uint64_t t0 = now_ns(), acc = 0;
    for (uint32_t it = 0; it < 20000u; it++) {
        vka_drv_fill(s, 1, (it & 1u) ? 2 : 0, 1, seg);
        vka_support_fill(sup, sizeof sup);
        int n = vka_walk(s, 1, &e, bs, DMSH_VK_SSBO_ALIGN, 64, test_rd, &rm, res, 64, NULL, why, sizeof why);
        acc += (uint64_t)n + (uint64_t)seg[DMSH_C2_TEX + 8] + (uint64_t)sup[192] + (uint64_t)res[0].tid;
    }
    *ms_out = (double)(now_ns() - t0) / 1e6;
    return acc;
}

int main(void) {
    vka_bstate s; init_bstate(&s);
    aoi_w46_set_fault(-1);
    test_c2_drv_sup(&s);
    test_walk_parity(&s);
    vka_bind_fast_set_for_test(1);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, tsan_worker, NULL);
    for (int i = 0; i < 4; i++) { void *r = NULL; pthread_join(th[i], &r); EXPECT_EQ("tsan", 0, (uint64_t)(uintptr_t)r); }
    double bms = 0.0, fms = 0.0;
    uint64_t s0 = bench_arm(&s, 0, &bms), s1 = bench_arm(&s, 1, &fms);
    EXPECT_EQ("bench parity", s0, s1);
    printf("t_bind_fast_w46: base_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           bms, fms, bms / (fms > 0.0 ? fms : 1e-6), (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
