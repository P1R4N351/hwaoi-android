// Wave 31 unit, ASan/UBSan, TSan, fault-injection, and microbenchmark test for movie_fast.h + hle_movie_nv12.h.
#include "../hle_movie_nv12.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t w31_now_ns(void) {
    struct timespec t = {0};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static int w31_ref_vp9_size(const uint8_t *p, int n, int *w, int *h) {
    int bit = 0;
    #define RB(k) ({ unsigned v_ = 0; for (int i_ = 0; i_ < (k); i_++, bit++) { if (bit >> 3 >= n) return 0; v_ = v_ << 1 | (p[bit >> 3] >> (7 - (bit & 7)) & 1); } v_; })
    if (RB(2) != 2) return 0;
    unsigned lo = RB(1), hi = RB(1), prof = hi << 1 | lo;
    if (prof == 3) RB(1);
    if (RB(1)) return 0;
    if (RB(1) != 0) return 0;
    RB(2);
    if (RB(24) != 0x498342) return 0;
    if (prof >= 2) RB(1);
    if (RB(3) != 7) { RB(1); if (prof == 1 || prof == 3) RB(3); }
    else if (prof == 1 || prof == 3) RB(1);
    *w = (int)RB(16) + 1; *h = (int)RB(16) + 1;
    #undef RB
    return 1;
}

static void w31_put_bits(uint8_t *buf, int *bit, unsigned val, int k) {
    for (int i = k - 1; i >= 0; i--) {
        int b = (*bit)++;
        if ((val >> i) & 1u) buf[b >> 3] |= (uint8_t)(1u << (7 - (b & 7)));
    }
}

__attribute__((noinline))
static void w31_ref_rgba_to_bgra(uint8_t *dst, const uint8_t *src, size_t nbytes) {
    for (size_t k = 0u; k + 3u < nbytes; k += 4u) {
        dst[k] = src[k + 2u]; dst[k + 1u] = src[k + 1u]; dst[k + 2u] = src[k]; dst[k + 3u] = src[k + 3u];
    }
}

static void w31_ref_pad_rgba(uint8_t *dst, int cw, int ch, int w, int h) {
    for (size_t i = 0u; i < (size_t)w * (size_t)h; i++) {
        size_t y = i / (size_t)w, x = i % (size_t)w;
        if ((int)x >= cw || (int)y >= ch) {
            dst[i * 4u] = 0u; dst[i * 4u + 1u] = 0u; dst[i * 4u + 2u] = 0u; dst[i * 4u + 3u] = 255u;
        }
    }
}

static void w31_verify_nv12_and_rgba(int fault_mode) {
    enum { W = 99, H = 55, CW = 71, CH = 39 };
    size_t ysz = (size_t)CW * CH, uvsz = (size_t)((CW + 1) / 2) * ((CH + 1) / 2);
    size_t nv12sz = (size_t)W * H * 3u / 2u, rgbasz = (size_t)W * H * 4u;
    uint8_t *y = malloc(ysz), *u = malloc(uvsz), *v = malloc(uvsz);
    uint8_t *nref = malloc(nv12sz), *ngot = malloc(nv12sz), *rref = malloc(rgbasz), *rgot = malloc(rgbasz);
    uint8_t *bref = malloc(rgbasz), *bgot = malloc(rgbasz);
    assert(y && u && v && nref && ngot && rref && rgot && bref && bgot);
    for (size_t i = 0u; i < ysz; i++) y[i] = (uint8_t)((i * 11u + 3u) & 0xffu);
    for (size_t i = 0u; i < uvsz; i++) { u[i] = (uint8_t)((i * 7u + 1u) & 0xffu); v[i] = (uint8_t)((i * 13u + 9u) & 0xffu); }
    memset(nref, 0x33, nv12sz); memset(ngot, 0x44, nv12sz);
    dm_movie_nv12_pack(y, CW, u, (CW + 1) / 2, v, (CW + 1) / 2, CW, CH, nref, W, H, 0, 0);
    dm_w31_set_fault(fault_mode == 1 ? 1 : 0);
    dm_movie_nv12_pack(y, CW, u, (CW + 1) / 2, v, (CW + 1) / 2, CW, CH, ngot, W, H, 1, 0);
    dm_w31_set_fault(0);
    assert(memcmp(nref, ngot, nv12sz) == 0);

    for (size_t i = 0u; i < rgbasz; i++) { rref[i] = (uint8_t)((i * 5u + 1u) & 0xffu); }
    memcpy(rgot, rref, rgbasz);
    w31_ref_pad_rgba(rref, CW, CH, W, H);
    dm_movie_rgba_pad_borders(rgot, CW, CH, W, H, fault_mode);
    assert(memcmp(rref, rgot, rgbasz) == 0);

    memset(bref, 0x77, rgbasz); memset(bgot, 0x88, rgbasz);
    w31_ref_rgba_to_bgra(bref, rgbasz ? rref : rgot, rgbasz);
    dm_movie_rgba_to_bgra(bgot, rref, rgbasz, fault_mode);
    assert(memcmp(bref, bgot, rgbasz) == 0);
    free(y); free(u); free(v); free(nref); free(ngot); free(rref); free(rgot); free(bref); free(bgot);
}

static void w31_verify_vp9_and_slots(int fault_mode) {
    for (unsigned prof = 0u; prof < 4u; prof++) {
        for (unsigned cs = 0u; cs < 8u; cs++) {
            uint8_t pkt[16] = {0}; int bit = 0;
            w31_put_bits(pkt, &bit, 2u, 2);
            w31_put_bits(pkt, &bit, prof & 1u, 1);
            w31_put_bits(pkt, &bit, (prof >> 1) & 1u, 1);
            if (prof == 3u) w31_put_bits(pkt, &bit, 0u, 1);
            w31_put_bits(pkt, &bit, 0u, 2);
            w31_put_bits(pkt, &bit, 1u, 2);
            w31_put_bits(pkt, &bit, 0x498342u, 24);
            if (prof >= 2u) w31_put_bits(pkt, &bit, 1u, 1);
            w31_put_bits(pkt, &bit, cs, 3);
            if (cs != 7u) { w31_put_bits(pkt, &bit, 1u, 1); if (prof == 1u || prof == 3u) w31_put_bits(pkt, &bit, 0u, 3); }
            else if (prof == 1u || prof == 3u) w31_put_bits(pkt, &bit, 0u, 1);
            w31_put_bits(pkt, &bit, 1919u - prof * 16u, 16);
            w31_put_bits(pkt, &bit, 1079u - cs * 8u, 16);
            for (int len = 0; len <= 16; len++) {
                int w0 = -1, h0 = -1, w1 = -1, h1 = -1;
                int ok0 = w31_ref_vp9_size(pkt, len, &w0, &h0);
                int ok1 = dm_movie_vp9_size_fast(pkt, len, &w1, &h1, fault_mode);
                assert(ok0 == ok1);
                if (ok0) assert(w0 == w1 && h0 == h1);
            }
        }
    }
    for (int idx = 0; idx <= 64; idx++) {
        uint64_t mask = idx == 64 ? UINT64_MAX : ((UINT64_C(1) << idx) - 1u);
        int exp = idx < 64 ? idx : -1;
        assert(dm_movie_first_free_slot(mask, 64, fault_mode) == exp);
    }
    assert(dm_movie_first_free_slot(0u, 16, fault_mode) == 0);
    assert(dm_movie_first_free_slot(UINT64_C(0xffff), 16, fault_mode) == -1);
}

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int go, waiting, quit, fast;
    uint64_t count, sum, elided;
} w31_pump_ctx;

static void *w31_pump_thread(void *arg) {
    w31_pump_ctx *c = (w31_pump_ctx *)arg;
    pthread_mutex_lock(&c->mu);
    for (uint32_t step = 0u; step < 100000u; step++) {
        for (uint32_t w = 0u; w < 100000u && !c->go && !c->quit; w++) {
            c->waiting++; pthread_cond_wait(&c->cv, &c->mu); c->waiting--;
        }
        if (c->quit && !c->go) break;
        c->go = 0;
        uint64_t target = c->count;
        pthread_mutex_unlock(&c->mu);
        c->sum = target;
        pthread_mutex_lock(&c->mu);
        if (c->quit && c->sum == c->count) break;
    }
    pthread_mutex_unlock(&c->mu);
    return NULL;
}

static void w31_verify_pump_tsan(int fast) {
    w31_pump_ctx c = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .fast = fast};
    pthread_t th;
    assert(pthread_create(&th, NULL, w31_pump_thread, &c) == 0);
    for (uint64_t i = 1u; i <= 5000u; i++) {
        pthread_mutex_lock(&c.mu);
        c.count = i;
        dm_movie_pump_wake(&c.go, c.waiting, &c.elided, &c.cv, fast);
        pthread_mutex_unlock(&c.mu);
    }
    pthread_mutex_lock(&c.mu);
    c.quit = 1;
    pthread_cond_signal(&c.cv);
    pthread_mutex_unlock(&c.mu);
    assert(pthread_join(th, NULL) == 0);
    assert(c.sum == 5000u);
    assert(fast ? (c.elided > 0u) : (c.elided == 0u));
}

__attribute__((noinline))
static uint64_t w31_bench_step(int fast, const uint8_t *y, const uint8_t *u, const uint8_t *v,
                               uint8_t *nv12, const uint8_t *rgba, uint8_t *bgra,
                               const uint8_t *vp9_pkt, size_t n_vp9,
                               size_t u_idx, size_t px_off, int w, int h, int uvw) {
    dm_movie_nv12_pack(y, w, u, uvw, v, uvw, w, h, nv12, w, h, fast, 0);
    if (fast) dm_movie_rgba_to_bgra(bgra, rgba, (size_t)w * (size_t)h * 4u, 0);
    else w31_ref_rgba_to_bgra(bgra, rgba, (size_t)w * (size_t)h * 4u);
    uint64_t vp9_acc = 0u;
    for (int r = 0; r < 64; r++) {
        int pw = 0, ph = 0;
        int ok = fast ? dm_movie_vp9_size_fast(vp9_pkt, (int)n_vp9, &pw, &ph, 0)
                      : w31_ref_vp9_size(vp9_pkt, (int)n_vp9, &pw, &ph);
        vp9_acc += (uint64_t)(ok ? pw + ph : 0);
    }
    return (uint64_t)nv12[(size_t)w * (size_t)h + 2u * u_idx] + (uint64_t)bgra[px_off + 2u] + vp9_acc;
}

static void w31_run_bench(void) {
    enum { W = 1920, H = 1080, UVW = 960, UVH = 540 };
    uint8_t *y = malloc((size_t)W * H), *u = malloc((size_t)UVW * UVH), *v = malloc((size_t)UVW * UVH);
    uint8_t *nv12 = malloc((size_t)W * H * 3u / 2u), *rgba = malloc((size_t)W * H * 4u), *bgra = malloc((size_t)W * H * 4u);
    assert(y && u && v && nv12 && rgba && bgra);
    memset(y, 0x55, (size_t)W * H); memset(u, 0x3a, (size_t)UVW * UVH); memset(v, 0xc4, (size_t)UVW * UVH);
    memset(nv12, 0, (size_t)W * H * 3u / 2u); memset(rgba, 0x22, (size_t)W * H * 4u); memset(bgra, 0, (size_t)W * H * 4u);
    uint8_t vp9_pkt[16] = {0x82, 0x49, 0x83, 0x42, 0x00, 0x07, 0x7f, 0x04, 0x37, 0x00, 0, 0, 0, 0, 0, 0};
    uint64_t ref_sink = 0u;
    for (int fast = 0; fast < 2; fast++) {
        dm_movie_fast_reset_for_test(fast);
        volatile uint64_t sink = 0u;
        uint64_t t0 = w31_now_ns();
        for (uint64_t it = 0u; it < 200u; it++) {
            size_t u_idx = (size_t)((it * 17u) % UVW), px_off = (size_t)((it * 31u) % (W * H)) * 4u;
            u[u_idx] = (uint8_t)it;
            rgba[px_off] = (uint8_t)it;
            sink += w31_bench_step(fast, y, u, v, nv12, rgba, bgra, vp9_pkt, sizeof vp9_pkt, u_idx, px_off, W, H, UVW);
        }
        double ms = (double)(w31_now_ns() - t0) / 1e6;
        uint64_t got = sink;
        if (fast == 0) ref_sink = got;
        else assert(got == ref_sink);
        printf("BENCH aoi_w31_%s movie_200f=%.3f ms sink=%llu\n", fast ? "on" : "off", ms, (unsigned long long)got);
    }
    free(y); free(u); free(v); free(nv12); free(rgba); free(bgra);
}

int main(void) {
    const char *fs = getenv("T_W31_FAULT");
    int f = fs ? atoi(fs) : 0;
    int fast_env = dm_movie_fast_on();
    if (f) dm_movie_fast_reset_for_test(1);
    dm_w31_set_fault(f);
    w31_verify_nv12_and_rgba(f);
    w31_verify_vp9_and_slots(f);
    if (!f) {
        w31_verify_pump_tsan(fast_env);
        w31_run_bench();
    }
    return 0;
}
