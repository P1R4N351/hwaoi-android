// Unit, parity, ASan+UBSan, TSan, fault-injection & benchmark suite for DM_VK_OVL_FAST (Wave 40 / Opt 42).
#define _POSIX_C_SOURCE 200809L
#define T_W40_TEST_HOOKS 1
#include "../../gfx/vk_android/vka_ovl_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Atomic int g_vka_ovl_fast_state;
_Atomic int g_vka_w40_fault;

typedef struct {
    _Atomic uint64_t vals[160];
    _Atomic uint64_t calls;
} w40_gpu_ctx;

static inline uint64_t w40_d2u(double d) { uint64_t u; memcpy(&u, &d, 8u); return u; }
static inline double w40_u2d(uint64_t u) { double d; memcpy(&d, &u, 8u); return d; }

static uint64_t w40_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static double w40_sample_cb(uint64_t frame, void *ctx) {
    w40_gpu_ctx *c = (w40_gpu_ctx *)ctx;
    atomic_fetch_add_explicit(&c->calls, 1u, memory_order_relaxed);
    if (frame >= 160u) return -1.0;
    return w40_u2d(atomic_load_explicit(&c->vals[frame], memory_order_acquire));
}

static int w40_test_overlay_copy_and_pub(void) {
    const int W = 64, H = 32, STRIDE = W * 4 + 32;
    uint8_t *dst = (uint8_t *)calloc((size_t)W * H * 4u, 1u);
    uint8_t *src = (uint8_t *)calloc((size_t)STRIDE * H, 1u);
    assert(dst && src);
    for (int y = 0; y < H; y++) for (int x = 0; x < W * 4; x++)
        src[(size_t)y * STRIDE + (size_t)x] = (uint8_t)((x * 3 + y * 7) & 255);
    int chg = 0;
    assert(vka_ovl_copy_changed(dst, 0, 0, src, W, H, STRIDE, &chg) && chg == 1);
    if (!vka_ovl_copy_changed(dst, W, H, src, W, H, STRIDE, &chg) || chg != 0) {
        free(dst); free(src); return 1;
    }
    src[(size_t)(H - 2) * STRIDE + 19u] ^= 0x5au;
    if (!vka_ovl_copy_changed(dst, W, H, src, W, H, STRIDE, &chg) || chg != 1) {
        free(dst); free(src); return 1;
    }
    for (int y = 0; y < H; y++)
        if (memcmp(dst + (size_t)y * (size_t)W * 4u, src + (size_t)y * STRIDE, (size_t)W * 4u) != 0) {
            free(dst); free(src); return 1;
        }
    vka_ovl_pub_state pub = {0}; uint64_t cur = 0u;
    if (!vka_ovl_can_skip_lock(&pub, 0u, &cur) || cur != 0u || vka_ovl_has_content(&pub)) {
        free(dst); free(src); return 1;
    }
    vka_ovl_pub_bump(&pub, 1);
    if (vka_ovl_can_skip_lock(&pub, 0u, &cur) || cur != 1u || !vka_ovl_has_content(&pub)) {
        free(dst); free(src); return 1;
    }
    assert(vka_ovl_copy_changed(dst, W, H, NULL, 0, 0, 0, &chg) && chg == 1);
    assert(vka_ovl_copy_changed(dst, 0, 0, NULL, 0, 0, 0, &chg) && chg == 0);
    free(dst); free(src);
    return 0;
}

static int w40_test_gpu_memo(void) {
    w40_gpu_ctx ctx;
    atomic_init(&ctx.calls, 0u);
    for (uint32_t i = 0; i < 160u; i++) {
        double v = (i % 7u == 0u) ? -1.0 : (3.25 + (double)(i % 13u) * 0.41);
        atomic_init(&ctx.vals[i], w40_d2u(v));
    }
    atomic_store_explicit(&ctx.vals[50], w40_d2u(-1.0), memory_order_release);
    vka_ovl_gpu_avg_memo memo = {0};
    for (uint64_t f = 0; f <= 60u; f++) {
        if (f == 60u) atomic_store_explicit(&ctx.vals[50], w40_d2u(9.875), memory_order_release);
        vka_ovl_fast_reset_for_test(0);
        float slow = vka_ovl_gpu_avg(NULL, f, w40_sample_cb, &ctx);
        vka_ovl_fast_reset_for_test(1);
        float fast = vka_ovl_gpu_avg(&memo, f, w40_sample_cb, &ctx);
        uint32_t sb = 0u, fb = 0u; memcpy(&sb, &slow, 4u); memcpy(&fb, &fast, 4u);
        if (sb != fb) return 1;
    }
    atomic_store_explicit(&ctx.calls, 0u, memory_order_release);
    float r1 = vka_ovl_gpu_avg(&memo, 60u, w40_sample_cb, &ctx);
    float r2 = vka_ovl_gpu_avg(&memo, 60u, w40_sample_cb, &ctx);
    uint32_t b1 = 0u, b2 = 0u; memcpy(&b1, &r1, 4u); memcpy(&b2, &r2, 4u);
    if (b1 != b2) return 1;
    return 0;
}

typedef struct { uint8_t tag; uint64_t k0, k1, shape; double ns; } w40_step;

static int w40_test_gprof_ht(void) {
    enum { NSTEP = 512, NKEY = 384, NSH = 12 };
    w40_step steps[NSTEP];
    for (int i = 0; i < NSTEP; i++) {
        uint32_t k = (uint32_t)((i < NKEY) ? i : ((i * 37 + (i >> 4)) % NKEY));
        uint32_t sh = (uint32_t)(k % NSH);
        steps[i].tag = (uint8_t)((k & 1u) ? 'c' : 'v');
        steps[i].k0 = (uint64_t)k + UINT64_C(0x1000);
        steps[i].k1 = (uint64_t)(k & 3u) + UINT64_C(0x2000);
        steps[i].shape = (uint64_t)sh * 131u + 7u;
        steps[i].ns = 50.0 + (double)((i * 19 + 5) % 41);
    }
    uint64_t sk[NKEY]; double sm[NKEY]; int nsk = 0;
    for (int i = 0; i < NSTEP; i++) {
        int j = 0;
        while (j < nsk && sk[j] != steps[i].shape) j++;
        if (j == nsk) { sk[nsk] = steps[i].shape; sm[nsk++] = steps[i].ns; }
        else if (steps[i].ns < sm[j]) sm[j] = steps[i].ns;
    }
    vka_gp_shape_ht sh_ht; vka_gp_shape_ht_init(&sh_ht);
    for (int i = 0; i < NSTEP; i++) vka_gp_shape_ht_add(&sh_ht, steps[i].shape, steps[i].ns);
    for (int j = 0; j < nsk; j++) {
        double mv = -1.0;
        if (!vka_gp_shape_ht_get(&sh_ht, sk[j], &mv) || mv != sm[j]) return 1;
    }
    vka_gp_key3_ht k_ht; vka_gp_key3_ht_init(&k_ht);
    uint8_t kt[NKEY]; uint64_t kk0[NKEY], kk1[NKEY]; int nk = 0;
    for (int i = 0; i < NSTEP; i++) {
        int si = 0;
        while (si < nk && !(kt[si] == steps[i].tag && kk0[si] == steps[i].k0 && kk1[si] == steps[i].k1)) si++;
        if (si == nk) { kt[nk] = steps[i].tag; kk0[nk] = steps[i].k0; kk1[nk] = steps[i].k1; nk++; }
        int32_t got = -1;
        int ins = vka_gp_key3_ht_find_or_put(&k_ht, steps[i].tag, steps[i].k0, steps[i].k1, si, &got);
        if (ins < 0 || got != si) return 1;
    }
    return 0;
}

typedef struct {
    pthread_mutex_t mu;
    vka_ovl_pub_state pub;
    uint8_t *slot_px;
    int slot_w, slot_h;
} w40_tsan_shared;

static void *w40_writer_thr(void *arg) {
    w40_tsan_shared *sh = (w40_tsan_shared *)arg;
    uint8_t buf[64 * 16 * 4];
    for (int it = 0; it < 400; it++) {
        memset(buf, (it >> 2) & 0xff, sizeof buf);
        pthread_mutex_lock(&sh->mu);
        int chg = 0;
        vka_ovl_copy_changed(sh->slot_px, sh->slot_w, sh->slot_h, buf, 64, 16, 256, &chg);
        sh->slot_w = 64; sh->slot_h = 16;
        if (chg) vka_ovl_pub_bump(&sh->pub, 1);
        pthread_mutex_unlock(&sh->mu);
    }
    return NULL;
}

static void *w40_reader_thr(void *arg) {
    w40_tsan_shared *sh = (w40_tsan_shared *)arg;
    uint64_t drawn = 0u, cur = 0u;
    w40_gpu_ctx gctx; atomic_init(&gctx.calls, 0u);
    for (uint32_t i = 0; i < 160u; i++) atomic_init(&gctx.vals[i], w40_d2u(4.5 + (double)(i & 3u)));
    vka_ovl_gpu_avg_memo memo = {0};
    for (int it = 0; it < 400; it++) {
        if (!vka_ovl_can_skip_lock(&sh->pub, drawn, &cur)) {
            pthread_mutex_lock(&sh->mu);
            drawn = cur;
            pthread_mutex_unlock(&sh->mu);
        }
        (void)vka_ovl_has_content(&sh->pub);
        (void)vka_ovl_gpu_avg(&memo, (uint64_t)20 + (uint64_t)(it & 31), w40_sample_cb, &gctx);
    }
    return NULL;
}

static int w40_run_all(int fault_mode) {
    vka_ovl_fast_reset_for_test(1);
    vka_w40_set_fault(fault_mode);
    if (w40_test_overlay_copy_and_pub() != 0) return 1;
    if (w40_test_gpu_memo() != 0) return 1;
    if (w40_test_gprof_ht() != 0) return 1;
    if (fault_mode == 0) {
        size_t slen = 0u;
        assert(vka_ovl_fnv1a_cstr("render_pass_0", &slen) != 0u && slen == 13u);
        w40_tsan_shared sh = {.mu = PTHREAD_MUTEX_INITIALIZER, .slot_px = (uint8_t *)calloc(64 * 16 * 4, 1u)};
        assert(sh.slot_px);
        pthread_t th[4];
        assert(pthread_create(&th[0], NULL, w40_writer_thr, &sh) == 0);
        assert(pthread_create(&th[1], NULL, w40_writer_thr, &sh) == 0);
        assert(pthread_create(&th[2], NULL, w40_reader_thr, &sh) == 0);
        assert(pthread_create(&th[3], NULL, w40_reader_thr, &sh) == 0);
        for (int t = 0; t < 4; t++) assert(pthread_join(th[t], NULL) == 0);
        free(sh.slot_px);
    }
    vka_w40_set_fault(0);
    return 0;
}

static double w40_bench(int fast_on, uint64_t *sink) {
    enum { W = 256, H = 64, NSTEP = 256, NKEY = 192 };
    uint8_t *dst = (uint8_t *)calloc((size_t)W * H * 4u, 1u);
    uint8_t *src = (uint8_t *)calloc((size_t)W * H * 4u, 1u);
    assert(dst && src);
    memset(src, 0x3cu, (size_t)W * H * 4u);
    w40_gpu_ctx gctx; atomic_init(&gctx.calls, 0u);
    for (uint32_t i = 0; i < 160u; i++) atomic_init(&gctx.vals[i], w40_d2u(6.5 + (double)(i % 5u)));
    vka_ovl_gpu_avg_memo memo = {0};
    vka_gp_key3_ht ht; uint64_t kk0[NKEY];
    vka_ovl_fast_reset_for_test(fast_on);
    uint64_t acc = 0u, t0 = w40_ns();
    for (int it = 0; it < 600; it++) {
        int chg = 0;
        assert(vka_ovl_copy_changed(dst, it ? W : 0, it ? H : 0, src, W, H, W * 4, &chg));
        acc += (uint64_t)dst[(it * 17) & 4095];
        float g = vka_ovl_gpu_avg(&memo, (uint64_t)45 + (uint64_t)(it & 31), w40_sample_cb, &gctx);
        acc += (uint64_t)(g * 100.0f);
        if (fast_on) vka_gp_key3_ht_init(&ht);
        int nk = 0;
        for (int s = 0; s < NSTEP; s++) {
            uint64_t key = (uint64_t)((s * 37) % NKEY) + 0x1000u;
            int idx = 0;
            if (fast_on) {
                int32_t out = 0;
                if (vka_gp_key3_ht_find_or_put(&ht, 'v', key, key ^ 7u, nk, &out) == 1) nk++;
                idx = out;
            } else {
                while (idx < nk && kk0[idx] != key) idx++;
                if (idx == nk) kk0[nk++] = key;
            }
            acc += (uint64_t)idx;
        }
    }
    double ms = (double)(w40_ns() - t0) / 1e6;
    free(dst); free(src); *sink += acc;
    return ms;
}

int main(void) {
    vka_ovl_fast_reset_for_test(-1);
    const char *env = getenv("DM_VK_OVL_FAST");
    int req_off = (env && env[0] == '0' && !env[1]);
    int ext_f = vka_w40_fault();
    if (ext_f > 0) return w40_run_all(ext_f) != 0 ? 1 : 0;
    assert(w40_run_all(0) == 0);
    for (int f = 1; f <= 5; f++) assert(w40_run_all(f) != 0);
    vka_w40_set_fault(0);
    uint64_t s0 = 0u, s1 = 0u;
    double ms0 = w40_bench(0, &s0), ms1 = w40_bench(1, &s1);
    assert(s0 == s1);
    vka_ovl_fast_reset_for_test(-1);
    printf("t_ovl_fast_w40: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0.0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}
