// Wave 45 (Optimization 47): parity, seqlock override, kind hash, double parse, layerreq slab/pairs/lockfree & TSan tests.
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/vk_android/vka_ctl_fast.h"
#include "../../gfx/vk_android/vka_layerreq.h"

_Atomic int g_vka_ctl_fast_override = -1;
_Atomic int g_aoi_w45_fault = -1;
static int g_fail = 0;

#define EXPECT_TRUE(msg, c) do { \
    if (!(c)) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

#define EXPECT_EQ_U64(msg, a, b) do { \
    uint64_t va_ = (uint64_t)(a), vb_ = (uint64_t)(b); \
    if (va_ != vb_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s: got %llu want %llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)vb_, (unsigned long long)va_); \
    } \
} while (0)

static uint64_t w45_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static vka_ctl_ovr_fast_t g_ovr_tab;
static pthread_mutex_t g_ovr_mu = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_tsan_stop;

static void *w45_tsan_reader(void *arg) {
    (void)arg;
    char buf[256];
    for (int i = 0; i < 4000 && !atomic_load_explicit(&g_tsan_stop, memory_order_relaxed); i++) {
        int found = 0;
        const char *k = (i & 1) ? "DM_VK_TEST_TSAN_K1" : "DM_VK_TEST_TSAN_MISS";
        int r = vka_ctl_ovr_lookup(&g_ovr_tab, k, buf, sizeof buf, &found);
        EXPECT_TRUE("tsan lookup non-neg", r >= -1 && r <= 1);
    }
    return NULL;
}

static void test_ovr_and_tsan(void) {
    memset(&g_ovr_tab, 0, sizeof g_ovr_tab);
    char buf[256];
    int found = 1;
    EXPECT_EQ_U64("empty miss r", 0, (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, "DM_VK_NONE", buf, sizeof buf, &found));
    EXPECT_EQ_U64("empty miss fnd", 0, (uint64_t)found);
    EXPECT_EQ_U64("put k1", 1, (uint64_t)vka_ctl_ovr_put_locked(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_001", "val_a", 1));
    EXPECT_EQ_U64("put k2", 1, (uint64_t)vka_ctl_ovr_put_locked(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_002", "val_b", 1));
    EXPECT_EQ_U64("hit k1 r", 1, (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_001", buf, sizeof buf, &found));
    EXPECT_TRUE("hit k1 str", found && strcmp(buf, "val_a") == 0);
    EXPECT_EQ_U64("hit k2 r", 1, (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_002", buf, sizeof buf, &found));
    EXPECT_TRUE("hit k2 str", found && strcmp(buf, "val_b") == 0);
    EXPECT_EQ_U64("colliding miss", 0, (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_999", buf, sizeof buf, &found));
    EXPECT_EQ_U64("colliding miss fnd", 0, (uint64_t)found);
    EXPECT_EQ_U64("clear k1", 1, (uint64_t)vka_ctl_ovr_put_locked(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_001", "", 0));
    EXPECT_EQ_U64("cleared k1 r", 0, (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_001", buf, sizeof buf, &found));
    EXPECT_EQ_U64("cleared k1 fnd", 1, (uint64_t)found);
    for (int i = 0; i < 60; i++) {
        char k[64], v[64];
        snprintf(k, sizeof k, "DM_VK_LIVE_BULK_%02d", i);
        snprintf(v, sizeof v, "%d", i * 3 + 7);
        EXPECT_EQ_U64("bulk put", 1, (uint64_t)vka_ctl_ovr_put_locked(&g_ovr_tab, k, v, 1));
    }
    EXPECT_EQ_U64("bulk lookup 59", 1, (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, "DM_VK_LIVE_BULK_59", buf, sizeof buf, &found));
    EXPECT_TRUE("bulk 59 str", found && strcmp(buf, "184") == 0);
    atomic_store_explicit(&g_tsan_stop, 0, memory_order_release);
    vka_ctl_ovr_put_locked(&g_ovr_tab, "DM_VK_TEST_TSAN_K1", "init", 1);
    pthread_t th[3];
    for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, w45_tsan_reader, NULL);
    for (int i = 0; i < 1000; i++) {
        char v[64];
        snprintf(v, sizeof v, "v%d", i);
        pthread_mutex_lock(&g_ovr_mu);
        vka_ctl_ovr_put_locked(&g_ovr_tab, "DM_VK_TEST_TSAN_K1", v, 1);
        pthread_mutex_unlock(&g_ovr_mu);
    }
    atomic_store_explicit(&g_tsan_stop, 1, memory_order_release);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
}

static int ref_in_tab(const char *const *t, const char *k) {
    for (; *t; t++) if (!strcmp(*t, k)) return 1;
    return 0;
}

static void test_kind_and_double(void) {
    static const char *const k_res[] = {"DM_VK_VALIDATE", "DM_VK_DRIVER", "DM_VK_MESH", "DM_VK_GPL", NULL};
    static const char *const k_liv[] = {"DM_VK_DRAW", "DM_VK_STATS", "DM_VK_FSR", "DM_LSFG", NULL};
    const char *probes[] = {"DM_VK_VALIDATE", "DM_VK_GPL", "DM_VK_DRAW", "DM_LSFG", "DM_VK_OTHER", ""};
    for (size_t i = 0; i < 6; i++) {
        int want = ref_in_tab(k_res, probes[i]) ? 1 : ref_in_tab(k_liv, probes[i]) ? 2 : 0;
        EXPECT_EQ_U64("kind parity", (uint64_t)want, (uint64_t)vka_ctl_kind_fast(probes[i], k_res, k_liv));
    }
    const char *nums[] = {"0", "1", "-1", "+42", "3.14159", "-0.75", "12345.6789"};
    for (size_t i = 0; i < 7; i++) {
        double d = 0.0;
        int ok = vka_ctl_parse_double(nums[i], &d);
        double ref = strtod(nums[i], NULL);
        EXPECT_EQ_U64("double ok", 1, (uint64_t)ok);
        EXPECT_TRUE("double exact", fabs(d - ref) < 1e-12);
    }
    double dummy = 0.0;
    EXPECT_EQ_U64("sci fallback", 0, (uint64_t)vka_ctl_parse_double("1e4", &dummy));
    EXPECT_EQ_U64("trailing fallback", 0, (uint64_t)vka_ctl_parse_double("12abc", &dummy));
}

static void test_layerreq_helpers(void) {
    char line[64];
    uint64_t vs0 = UINT64_C(0xfedcba9876543210), ps0 = UINT64_C(0x90123456789abcde);
    size_t n = vka_ctl_pair_line_put(line, vs0, ps0);
    EXPECT_EQ_U64("pair len", 34, (uint64_t)n);
    line[n] = '\0';
    uint64_t vs1 = 0, ps1 = 0;
    EXPECT_EQ_U64("pair parse", 1, (uint64_t)vka_ctl_pair_line_parse(line, n, &vs1, &ps1));
    EXPECT_EQ_U64("pair vs", vs0, vs1);
    EXPECT_EQ_U64("pair ps", ps0, ps1);
    vka_lreq *r = calloc(1, sizeof *r);
    uint8_t c0[12] = {1, 2, 3}, d0[20] = {4, 5, 6};
    EXPECT_EQ_U64("lreq add", 1, (uint64_t)vka_lreq_add(r, vs0, c0, sizeof c0, d0, sizeof d0));
    EXPECT_TRUE("single slab ptr", r->slab_mask == 1u && r->st[0].code == r->st[0].ctl + sizeof c0);
    vka_lreq_free(r);
    static vka_lylife l = VKA_LYLIFE_INIT(NULL);
    EXPECT_TRUE("ly_get empty", vka_ly_get(&l) == NULL);
    static vka_lrq q = VKA_LRQ_INIT;
    EXPECT_EQ_U64("lrq room init", 1, (uint64_t)vka_lrq_room(&q));
    EXPECT_EQ_U64("lrq written init", 0, (uint64_t)vka_lrq_written(&q));
}

static uint64_t bench_arm(int fast, double *ms_out) {
    static const char *const k_res[] = {"DM_VK_VALIDATE", "DM_VK_DRIVER", "DM_VK_HOOKDIR", "DM_VK_PLACED",
        "DM_VK_PREFAULT", "DM_VK_PCACHE", "DM_VK_GPL", "DM_VK_MESH", "DM_VK_WORKER", "DM_VK_SPV_OVERRIDE", NULL};
    static const char *const k_liv[] = {"DM_VK_DRAW", "DM_VK_STATS", "DM_VK_HANG_S", "DM_VK_FRAME_RATE",
        "DM_VK_FSR", "DM_VK_CHUNK", "DM_VK_PROF", "DM_LSFG", "DM_LSFG_MULT", "DM_VK_PRESENT_SCALE", NULL};
    const char *keys[4] = {"DM_VK_PREFIX_KEY_ALPHA_001", "DM_VK_PREFIX_KEY_ALPHA_002", "DM_VK_DRAW", "DM_VK_FRAME_RATE"};
    uint64_t t0 = w45_now_ns(), acc = 0;
    char out[256], pbuf[40];
    for (uint32_t it = 0; it < 20000u; it++) {
        const char *k = keys[it & 3u];
        int found = 0;
        if (fast) {
            acc += (uint64_t)vka_ctl_ovr_lookup(&g_ovr_tab, k, out, sizeof out, &found);
            acc += (uint64_t)vka_ctl_kind_fast(k, k_res, k_liv);
            double dv = 0.0;
            vka_ctl_parse_double("60.25", &dv);
            acc += (uint64_t)(dv * 4.0);
            acc += (uint64_t)vka_ctl_pair_line_put(pbuf, UINT64_C(0x1000000000000001) + it, UINT64_C(0x2000000000000002));
        } else {
            acc += (uint64_t)(ref_in_tab(k_res, k) ? 1 : ref_in_tab(k_liv, k) ? 2 : 0);
            for (int j = 0; j < 64; j++) found += (!strcmp(k, keys[j & 3u]));
            acc += (uint64_t)(found > 0);
            acc += (uint64_t)(strtod("60.25", NULL) * 4.0);
            acc += (uint64_t)snprintf(pbuf, sizeof pbuf, "%016llx\t%016llx\n",
                                      (unsigned long long)(UINT64_C(0x1000000000000001) + it),
                                      (unsigned long long)UINT64_C(0x2000000000000002));
        }
    }
    *ms_out = (double)(w45_now_ns() - t0) / 1e6;
    return acc;
}

int main(void) {
    vka_ctl_fast_set_for_test(1);
    test_ovr_and_tsan();
    test_kind_and_double();
    test_layerreq_helpers();
    double bms = 0.0, fms = 0.0;
    uint64_t s0 = bench_arm(0, &bms);
    memset(&g_ovr_tab, 0, sizeof g_ovr_tab);
    vka_ctl_ovr_put_locked(&g_ovr_tab, "DM_VK_PREFIX_KEY_ALPHA_001", "1", 1);
    uint64_t s1 = bench_arm(1, &fms);
    EXPECT_TRUE("bench acc nonzero", s0 > 0 && s1 > 0);
    printf("t_ctl_fast_w45: base_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           bms, fms, bms / (fms > 0.0 ? fms : 1e-6), (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
