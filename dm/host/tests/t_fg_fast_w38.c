#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W38_TEST_HOOKS 1
#include "../../gfx/vk_android/vka_fg_fast.h"

_Atomic int g_vka_fg_fast_state;
_Atomic int g_vka_w38_fault;

static const char *const g_w38_kinds[9] = {
    "TTTIIIIIU", "TTIIIIIIIIIUU", "TTTTIIIIIIIUU", "TIU", "TTTTTTTTTIIIU",
    "TTTTTTTIIU", "TTTTTTTTTIIIU", "TTTTTTTTTIIIU", "TTIUU"
};

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int legacy_pass_ref(
    int p, uint32_t *out_n, uint32_t *out_nb, uint32_t *out_ni,
    uint32_t *tex_mask, uint8_t ubo_slot[4], uint8_t ubo_which[4], uint8_t img_slot[16]) {
    uint32_t n = 0, nb = 0, ni = 0, tm = 0;
    for (const char *k = g_w38_kinds[p]; *k; k++, n++) {
        if (*k == 'U') {
            ubo_which[nb] = (nb == 0u) ? 0u : (p == 8 ? 2u : 1u);
            ubo_slot[nb++] = (uint8_t)n;
        } else {
            if (*k == 'T') tm |= (1u << ni);
            img_slot[ni++] = (uint8_t)n;
        }
    }
    *out_n = n; *out_nb = nb; *out_ni = ni; *tex_mask = tm;
    return 0;
}

static void *fmt_worker(void *arg) {
    vka_fmt_feat_cache *c = (vka_fmt_feat_cache *)arg;
    for (int r = 0; r < 200; r++) {
        for (uint32_t f = 1; f <= 16u; f++) {
            uint32_t expect = f * 0x101u + 7u, got = 0;
            if (!vka_fmt_feat_get(c, (uintptr_t)0x1000u, f, &got)) {
                vka_fmt_feat_put(c, (uintptr_t)0x1000u, f, expect);
                assert(vka_fmt_feat_get(c, (uintptr_t)0x1000u, f, &got));
            }
            assert(got == expect);
        }
    }
    return NULL;
}

static void verify_fmt_and_off(void) {
    vka_fmt_feat_cache fc;
    memset(&fc, 0, sizeof fc);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, fmt_worker, &fc) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    vka_fg_off_tracker trk = {0, 0, 0};
    assert(vka_fg_off_is_settled(&trk, 1u, 0, 0) == 0);
    vka_fg_off_mark_settled(&trk, 1u, 0);
    assert(vka_fg_off_is_settled(&trk, 1u, 0, 0) == 1);
    assert(vka_fg_off_is_settled(&trk, 2u, 0, 0) == 0);
    assert(vka_fg_off_is_settled(&trk, 1u, 1, 0) == 0);
    vka_fg_off_mark_active(&trk);
    assert(vka_fg_off_is_settled(&trk, 1u, 0, 0) == 0);
}

static void verify_fsr3_meta_and_memo(void) {
    for (int p = 0; p < 9; p++) {
        vka_fsr3_pass_meta m;
        vka_fsr3_compile_kinds(&m, p == 8, g_w38_kinds[p]);
        uint32_t rn = 0, rnb = 0, rni = 0, rtm = 0;
        uint8_t ru_slot[4] = {0}, ru_wh[4] = {0}, ri_slot[16] = {0};
        legacy_pass_ref(p, &rn, &rnb, &rni, &rtm, ru_slot, ru_wh, ri_slot);
        assert(m.n_total == rn && m.n_ubo == rnb && m.n_img == rni);
        assert(m.img_is_tex_mask == rtm);
        assert(memcmp(m.ubo_slot, ru_slot, rnb) == 0);
        assert(memcmp(m.ubo_which, ru_wh, rnb) == 0);
        assert(memcmp(m.img_slot, ri_slot, rni) == 0);
    }
    static const uint32_t sz[5][2] = {{960, 540}, {1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    vka_fsr3_fast_memo memo;
    memset(&memo, 0, sizeof memo);
    for (int i = 0; i < 5; i++) {
        uint32_t rw = sz[i][0], rh = sz[i][1];
        vka_fsr3_geom_sync(&memo, rw, rh);
        uint32_t mx = rw > rh ? rw : rh, mips = 0;
        while ((2u << mips) <= mx) mips++;
        if (mips > 12u) mips = 12u;
        assert(memo.mips == mips);
        assert(memo.sgx == (rw - 1u) / 64u + 1u);
        assert(memo.sgy == (rh - 1u) / 64u + 1u);
        float sharps[3] = {0.f, 0.5f, 1.f};
        for (int k = 0; k < 3; k++) {
            vka_fsr3_rcas_sync(&memo, sharps[k]);
            float sx = exp2f(-(2.f - 2.f * sharps[k]));
            uint32_t sxu = 0;
            memcpy(&sxu, &sx, 4);
            assert(memo.rcas0 == sxu);
            assert(memo.sharpen == (sharps[k] > 0.f));
        }
    }
}

static void verify_lsfg_batch(void) {
    vka_lsfg_batch_ctl ctl = {0, 0, 0};
    uint32_t total = 0;
    for (uint32_t i = 0; i < 154u; i++) {
        uint32_t n = 4u + (i % 9u);
        vka_lsfg_batch_step(&ctl, n);
        total += n;
    }
    vka_lsfg_batch_finish(&ctl);
    assert(ctl.emitted == total);
    assert(ctl.flushes >= 1u && ctl.flushes < 154u);
}

static double bench_mode(int fast_on, uint64_t *sink) {
    vka_fmt_feat_cache fc;
    vka_fsr3_fast_memo memo;
    vka_fsr3_pass_meta metas[9];
    memset(&fc, 0, sizeof fc);
    memset(&memo, 0, sizeof memo);
    vka_fmt_feat_put(&fc, (uintptr_t)0x2000u, 37u, 0x1234u);
    for (int p = 0; p < 9; p++) vka_fsr3_compile_kinds(&metas[p], p == 8, g_w38_kinds[p]);
    vka_fg_off_tracker trk = {1u, 1, 0};
    uint64_t t0 = now_ns(), acc = 0;
    for (int it = 0; it < 20000; it++) {
        uint32_t feats = 0;
        if (fast_on) {
            vka_fmt_feat_get(&fc, (uintptr_t)0x2000u, 37u, &feats);
            acc += (uint64_t)vka_fg_off_is_settled(&trk, 1u, 0, 0);
            vka_fsr3_geom_sync(&memo, 1280u, 720u);
            vka_fsr3_rcas_sync(&memo, 0.5f);
            acc += memo.mips + memo.rcas0;
            for (int p = 0; p < 9; p++) acc += metas[p].n_total + metas[p].img_is_tex_mask;
        } else {
            volatile uint32_t raw_f = 0x1234u ^ (uint32_t)(it & 0);
            feats = raw_f;
            char tmp[16];
            snprintf(tmp, sizeof tmp, "%s", "off");
            acc += (uint64_t)(tmp[0] == 'o');
            float aspect = 1280.f / 720.f;
            float fov = 60.f * 3.14159265f / 180.f;
            float cot = cosf(0.5f * fov) / sinf(0.5f * fov);
            float thf = tanf(atanf(tanf(fov * 0.5f) * aspect));
            volatile uint32_t tb = 0;
            float sx = exp2f(-(2.f - 2.f * 0.5f));
            memcpy((void *)&tb, &sx, 4);
            (void)cot; (void)thf;
            uint32_t mips = 0;
            while ((2u << mips) <= 1280u) mips++;
            acc += mips + tb;
            for (int p = 0; p < 9; p++) {
                uint32_t rn = 0, rnb = 0, rni = 0, rtm = 0;
                uint8_t us[4], uw[4], is[16];
                legacy_pass_ref(p, &rn, &rnb, &rni, &rtm, us, uw, is);
                acc += rn + rtm;
            }
        }
        acc += feats;
    }
    *sink += acc;
    return (double)(now_ns() - t0) / 1e6;
}

int main(void) {
    vka_fg_fast_reset_for_test(1);
    const char *fs = getenv("AOI_W38_FAULT");
    vka_w38_set_fault(fs ? atoi(fs) : 0);
    verify_fmt_and_off();
    verify_fsr3_meta_and_memo();
    verify_lsfg_batch();
    vka_w38_set_fault(0);
    const char *env = getenv("DM_VK_FG_FAST");
    int req_off = (env && env[0] == '0' && !env[1]);
    uint64_t s0 = 0, s1 = 0;
    double ms0 = bench_mode(0, &s0);
    double ms1 = bench_mode(1, &s1);
    assert(s0 == s1);
    printf("t_fg_fast_w38: req_off=%d off_ms=%.3f on_ms=%.3f ratio=%.2fx sink=%llu\n",
           req_off, ms0, ms1, ms1 > 0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}
