/* Wave 29 (Opt 31) unit, TSan stress, microbench, and fault-injection tests for DM_AUDREN_FAST. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DM_AUDREN_FAST_TEST 1
#include "../audren_fast.h"

static uint64_t w29_now_ns(void) {
    struct timespec t;
    memset(&t, 0, sizeof t);
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static uint32_t w29_build_list(uint8_t *buf, int sdk, uint32_t ncmd, int sink_on, int bad_tail) {
    memset(buf, 0, 0x20u + (size_t)ncmd * 0x40u);
    uint8_t *q = buf + 0x20u;
    int sink_id = (sdk <= 6) ? 0x10 : (sdk == 21 ? 0x14 : 0x13);
    for (uint32_t i = 0u; i < ncmd; i++) {
        uint16_t sz = 0x40u;
        int id = (i == ncmd - 8u) ? sink_id : 6;
        uint8_t en = (id == sink_id) ? (uint8_t)sink_on : 1u;
        if (bad_tail && i == ncmd - 1u) {
            memset(q, 0, sz);
            q += sz;
            continue;
        }
        if (sdk == 21) {
            q[0] = en;
            q[1] = (uint8_t)id;
            memcpy(q + 2, &sz, sizeof sz);
        } else {
            uint32_t magic = AUDREN_FAST_MAGIC;
            memcpy(q, &magic, sizeof magic);
            if (sdk <= 6) {
                int16_t id16 = (int16_t)id;
                memcpy(q + 4, &sz, sizeof sz);
                memcpy(q + 6, &id16, sizeof id16);
                q[0x10] = en;
            } else {
                q[4] = en;
                q[5] = (uint8_t)id;
                memcpy(q + 6, &sz, sizeof sz);
            }
        }
        q += sz;
    }
    return (uint32_t)(q - buf);
}

static void w29_build_rs_table(float h[3][AUDREN_FAST_RS_T]) {
    for (int p = 0; p < 3; p++) {
        double sum = 0.0;
        double w[AUDREN_FAST_RS_T];
        for (int k = 0; k < (int)AUDREN_FAST_RS_T; k++) {
            double x = p / 3.0 + 7.0 - k;
            double fc = 0.9;
            double s = (x == 0.0) ? fc : sin(3.14159265358979323846 * fc * x) /
                                             (3.14159265358979323846 * x);
            double t = (x + 8.0) / 16.0;
            double win = 0.42 - 0.5 * cos(6.28318530717958647692 * t) +
                         0.08 * cos(12.5663706143591729538 * t);
            w[k] = s * win;
            sum += w[k];
        }
        for (int k = 0; k < (int)AUDREN_FAST_RS_T; k++) h[p][k] = (float)(w[k] / sum);
    }
}

static uint32_t w29_slow_up_3_2(const float h[3][AUDREN_FAST_RS_T], int32_t *buf,
                                uint32_t *pos_io, const int32_t *in, uint32_t n, int32_t *out) {
    memcpy(buf + 2u * (AUDREN_FAST_RS_T - 1u), in, (size_t)n * 8u);
    uint32_t pos = *pos_io, m = 0u;
    for (; pos < 3u * n; pos += 2u, m++) {
        const int32_t *x = buf + 2u * (pos / 3u);
        const float *c = h[pos % 3u];
        float l = 0.0f, r = 0.0f;
        for (uint32_t k = 0u; k < AUDREN_FAST_RS_T; k++) {
            l += c[k] * (float)x[2u * k];
            r += c[k] * (float)x[2u * k + 1u];
        }
        out[2u * m] = (int32_t)lrintf(l);
        out[2u * m + 1u] = (int32_t)lrintf(r);
    }
    *pos_io = pos - 3u * n;
    memmove(buf, buf + 2u * n, (AUDREN_FAST_RS_T - 1u) * 8u);
    return m;
}

static const uint8_t *w29_slow_scan(const uint8_t *cl, size_t len, uint32_t ncmd, int sdk,
                                    int *malformed, uint32_t *bad_i, size_t *bad_off) {
    *malformed = 0;
    const uint8_t *c = cl + 0x20u, *cend = cl + len;
    int sink_id = (sdk <= 6) ? 0x10 : (sdk == 21 ? 0x14 : 0x13);
    for (uint32_t i = 0u; i < ncmd; i++) {
        size_t hdr = (sdk == 21) ? 0x10u : ((sdk <= 6) ? 0x11u : 8u);
        if (c > cend || (size_t)(cend - c) < hdr) {
            *malformed = 1; *bad_i = i; *bad_off = (size_t)(c - cl);
            return NULL;
        }
        if (sdk != 21) {
            uint32_t magic = 0u;
            memcpy(&magic, c, sizeof magic);
            if (magic != AUDREN_FAST_MAGIC) {
                *malformed = 1; *bad_i = i; *bad_off = (size_t)(c - cl);
                return NULL;
            }
        }
        int16_t sz = 0;
        memcpy(&sz, c + (sdk == 21 ? 2 : (sdk <= 6 ? 4 : 6)), sizeof sz);
        if (sz < (int)hdr || (size_t)sz > (size_t)(cend - c)) {
            *malformed = 1; *bad_i = i; *bad_off = (size_t)(c - cl);
            return NULL;
        }
        c += (size_t)sz;
    }
    c = cl + 0x20u;
    for (uint32_t i = 0u; i < ncmd; i++) {
        int16_t sz = 0, id16 = 0;
        memcpy(&sz, c + (sdk == 21 ? 2 : (sdk <= 6 ? 4 : 6)), sizeof sz);
        int id = (sdk == 21) ? (int)c[1] : ((sdk <= 6) ? (memcpy(&id16, c + 6, 2), (int)id16) : (int)c[5]);
        int en = (sdk == 21) ? ((c[0] & 1u) != 0u) : ((sdk <= 6) ? (c[0x10] != 0u) : (c[4] != 0u));
        if (id == sink_id && en) return c;
        c += (size_t)sz;
    }
    return NULL;
}

typedef struct {
    _Atomic uint64_t renders;
    _Atomic uint64_t done_r;
    _Atomic uint32_t waiters;
    pthread_mutex_t cq_mu;
    pthread_cond_t cq_cv;
    audren_rev_slot rev[4];
    pthread_mutex_t rev_mu;
    void *obj;
} w29_sync_ctx;

static int w29_wait_step(w29_sync_ctx *ctx, uint64_t want_r) {
    int woke = 0;
    pthread_mutex_lock(&ctx->cq_mu);
    audren_cq_wait_enter(&ctx->waiters);
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_nsec += 50000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    while (atomic_load(&ctx->renders) < want_r) {
        if (pthread_cond_timedwait(&ctx->cq_cv, &ctx->cq_mu, &dl) != 0) break;
    }
    woke = atomic_load(&ctx->renders) >= want_r;
    audren_cq_wait_leave(&ctx->waiters);
    pthread_mutex_unlock(&ctx->cq_mu);
    return woke;
}

static int run_fault(int f) {
    audren_fast_reset_for_test(1);
    static uint8_t unaligned[0x20u + 32u * 0x40u + 4u];
    uint8_t *cl = unaligned + 1;
    uint32_t len = w29_build_list(cl, 16, 32u, 1, f == 2 ? 1 : 0);
    int mal = 0;
    uint32_t bi = 0u;
    size_t bo = 0u;
    const uint8_t *s = audren_fast_find_sink(cl, len, 32u, 0, 0, 0x13, &mal, &bi, &bo);
    if (f == 1) return (s == cl + 0x20u + 24u * 0x40u && !mal) ? 0 : 1;
    if (f == 2) return (s == NULL && mal == 1 && bi == 31u) ? 0 : 1;
    if (f == 3) {
        float h[3][AUDREN_FAST_RS_T];
        w29_build_rs_table(h);
        int32_t b0[2 * 1040] = {0}, b1[2 * 1040] = {0}, in[320], o0[480], o1[480];
        uint32_t p0 = 0u, p1 = 0u;
        for (int i = 0; i < 320; i++) in[i] = (i - 160) * 19;
        uint32_t n0 = w29_slow_up_3_2(h, b0, &p0, in, 160u, o0);
        uint32_t n1 = audren_fast_up_3_2(h, b1, &p1, in, 160u, o1);
        return (n0 == n1 && p0 == p1 && memcmp(o0, o1, (size_t)n0 * 8u) == 0) ? 0 : 1;
    }
    if (f == 4) {
        _Atomic uint32_t w = 1u;
        return audren_cq_should_wake(&w) == 1 ? 0 : 1;
    }
    if (f == 5) {
        audren_rev_slot slots[4];
        memset(slots, 0, sizeof slots);
        int dummy = 42;
        atomic_store_explicit(&slots[2].obj, (uintptr_t)&dummy, memory_order_release);
        void *ev = NULL;
        return (audren_rev_find(slots, &dummy, &ev) && ev == slots[2].ev) ? 0 : 1;
    }
    return 1;
}

static void w29_test_all_layouts(void) {
    static uint8_t raw[0x20u + 64u * 0x40u + 4u];
    uint8_t *cl = raw + 1;
    const int sdks[3] = {6, 16, 21};
    for (int k = 0; k < 3; k++) {
        int sdk = sdks[k], hdr6 = (sdk == 6), hdr21 = (sdk == 21);
        int sink_id = hdr21 ? 0x14 : (hdr6 ? 0x10 : 0x13);
        for (int on = 0; on < 2; on++) {
            uint32_t len = w29_build_list(cl, sdk, 48u, on, 0);
            int mal = 0; uint32_t bi = 0u; size_t bo = 0u;
            const uint8_t *got = audren_fast_find_sink(cl, len, 48u, hdr6, hdr21, sink_id, &mal, &bi, &bo);
            const uint8_t *want = on ? (cl + 0x20u + 40u * 0x40u) : NULL;
            assert(got == want && !mal);
        }
        uint32_t len = w29_build_list(cl, sdk, 48u, 1, 1);
        int mal = 0; uint32_t bi = 0u; size_t bo = 0u;
        const uint8_t *got = audren_fast_find_sink(cl, len, 48u, hdr6, hdr21, sink_id, &mal, &bi, &bo);
        assert(!got && mal == 1 && bi == 47u);
        assert(!audren_fast_find_sink(cl, 0x10u, 1u, hdr6, hdr21, sink_id, &mal, &bi, &bo));
    }
}

static void *w29_waiter_thread(void *arg) {
    w29_sync_ctx *ctx = (w29_sync_ctx *)arg;
    for (int i = 0; i < 64; i++) {
        assert(w29_wait_step(ctx, (uint64_t)(i + 1)));
        atomic_store_explicit(&ctx->done_r, (uint64_t)(i + 1), memory_order_release);
        void *ev = NULL;
        assert(audren_rev_find(ctx->rev, ctx->obj, &ev) && ev == ctx->rev[1].ev);
    }
    return NULL;
}

static void w29_test_resampler_and_sync(void) {
    float h[3][AUDREN_FAST_RS_T];
    w29_build_rs_table(h);
    int32_t b0[2 * 1040] = {0}, b1[2 * 1040] = {0}, in[320], o0[480], o1[480];
    uint32_t p0 = 0u, p1 = 0u;
    for (int blk = 0; blk < 12; blk++) {
        for (int i = 0; i < 320; i++) in[i] = ((blk * 37 + i * 13) & 1023) - 512;
        uint32_t n0 = w29_slow_up_3_2(h, b0, &p0, in, 160u, o0);
        uint32_t n1 = audren_fast_up_3_2(h, b1, &p1, in, 160u, o1);
        assert(n0 == n1 && p0 == p1 && memcmp(o0, o1, (size_t)n0 * 8u) == 0);
    }
    w29_sync_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    pthread_mutex_init(&ctx.cq_mu, NULL);
    pthread_cond_init(&ctx.cq_cv, NULL);
    pthread_mutex_init(&ctx.rev_mu, NULL);
    int dummy = 9;
    ctx.obj = &dummy;
    atomic_store_explicit(&ctx.rev[1].obj, (uintptr_t)ctx.obj, memory_order_release);
    assert(!audren_cq_should_wake(&ctx.waiters));
    pthread_t th;
    assert(pthread_create(&th, NULL, w29_waiter_thread, &ctx) == 0);
    for (uint64_t i = 0u; i < 64u; i++) {
        while (atomic_load(&ctx.waiters) == 0u) {
            struct timespec ts = {0, 10000L};
            nanosleep(&ts, NULL);
        }
        atomic_fetch_add(&ctx.renders, 1u);
        if (audren_cq_should_wake(&ctx.waiters)) {
            pthread_mutex_lock(&ctx.cq_mu);
            pthread_cond_broadcast(&ctx.cq_cv);
            pthread_mutex_unlock(&ctx.cq_mu);
        }
        while (atomic_load_explicit(&ctx.done_r, memory_order_acquire) < i + 1ull) {
            struct timespec ts = {0, 5000L};
            nanosleep(&ts, NULL);
        }
    }
    assert(pthread_join(th, NULL) == 0);
}

static void w29_run_bench(void) {
    static uint8_t cl[0x20u + 512u * 0x40u];
    uint32_t cl_len = w29_build_list(cl, 16, 512u, 1, 0);
    float h[3][AUDREN_FAST_RS_T];
    w29_build_rs_table(h);
    int32_t b_rs[2 * 1040] = {0}, in_rs[320], out_rs[480];
    for (int i = 0; i < 320; i++) in_rs[i] = i - 160;
    audren_rev_slot slots[4];
    memset(slots, 0, sizeof slots);
    pthread_mutex_t rev_mu = PTHREAD_MUTEX_INITIALIZER;
    int obj = 77;
    atomic_store_explicit(&slots[0].obj, (uintptr_t)&obj, memory_order_release);
    _Atomic uint32_t waiters = 0u;
    pthread_mutex_t cq_mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cq_cv = PTHREAD_COND_INITIALIZER;

    for (int mode = 0; mode < 2; mode++) {
        audren_fast_reset_for_test(mode);
        uint32_t pos = 0u;
        memset(b_rs, 0, sizeof b_rs);
        volatile uint64_t sink = 0u;
        uint64_t t0 = w29_now_ns();
        for (int it = 0; it < 20000; it++) {
            int mal = 0; uint32_t bi = 0u; size_t bo = 0u;
            const uint8_t *snk = mode
                ? audren_fast_find_sink(cl, cl_len, 512u, 0, 0, 0x13, &mal, &bi, &bo)
                : w29_slow_scan(cl, cl_len, 512u, 16, &mal, &bi, &bo);
            if (audren_cq_should_wake(&waiters)) {
                pthread_mutex_lock(&cq_mu);
                pthread_cond_broadcast(&cq_cv);
                pthread_mutex_unlock(&cq_mu);
            }
            void *ev = NULL;
            if (!audren_rev_find(slots, &obj, &ev)) {
                pthread_mutex_lock(&rev_mu);
                for (int j = 0; j < 4 && !ev; j++)
                    if (atomic_load_explicit(&slots[j].obj, memory_order_relaxed) == (uintptr_t)&obj) ev = slots[j].ev;
                pthread_mutex_unlock(&rev_mu);
            }
            sink += (uint64_t)(snk != NULL) + (uint64_t)(ev != NULL);
        }
        double ms_48k = (double)(w29_now_ns() - t0) / 1e6;
        uint64_t t1 = w29_now_ns();
        for (int it = 0; it < 2000; it++) {
            uint32_t nout = mode
                ? audren_fast_up_3_2(h, b_rs, &pos, in_rs, 160u, out_rs)
                : w29_slow_up_3_2(h, b_rs, &pos, in_rs, 160u, out_rs);
            sink += (uint64_t)nout + (uint64_t)(uint32_t)out_rs[0];
        }
        double ms_rs32 = (double)(w29_now_ns() - t1) / 1e6;
        printf("BENCH aoi_w29_%s audren_48k=%.3f ms rs32=%.3f ms sink=%llu\n",
               mode ? "on" : "off", ms_48k, ms_rs32, (unsigned long long)sink);
    }
}

int main(void) {
    const char *fs = getenv("T_W29_FAULT");
    int f = fs ? atoi(fs) : 0;
    if (f >= 1 && f <= 5) {
        int rc = run_fault(f);
        printf("t_audren_fast_w29: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    audren_fast_reset_for_test(1);
    w29_test_all_layouts();
    w29_test_resampler_and_sync();
    w29_run_bench();
    return 0;
}
