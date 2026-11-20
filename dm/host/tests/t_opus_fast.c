// Host unit, benchmark, and Delta #27 fault-injection test for hle_codec_fast.h.
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "../hle_codec_fast.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { N_THREADS = 4, N_OPS = 100000, N_DECS = 16, N_PEAK_FRAMES = 40000, PEAK_LEN = 1920 };

static int g_fault;

static uint64_t op_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void verify_peak(void) {
    int16_t buf[65536];
    for (int i = 0; i < 65536; i++) buf[i] = (int16_t)(i - 32768);
    assert(dm_opus_pcm_peak_scalar(buf, 65536) == 32768);
    int pk_full = dm_opus_pcm_peak_fast(buf, 65536);
    if (g_fault == 1) pk_full = 32767;
    assert(pk_full == 32768);

    int16_t slice[128];
    for (size_t len = 0; len <= 97; len++) {
        for (size_t i = 0; i < len; i++) slice[i] = (int16_t)(((int)(i * 977 + len * 13) % 65535) - 32767);
        if (len > 0 && (len & 1u)) slice[len - 1] = -32768;
        int want = dm_opus_pcm_peak_scalar(slice, len);
        int got = dm_opus_pcm_peak_fast(slice, len - (g_fault == 3 && len > 0 ? 1 : 0));
        assert(want == got);
    }
}

static void bench_peak(void) {
    int16_t frame[PEAK_LEN];
    for (int i = 0; i < PEAK_LEN; i++) frame[i] = (int16_t)(((i * 313) % 40000) - 20000);
    frame[PEAK_LEN - 3] = -32768;
    volatile int sink = 0;

    uint64_t t0 = op_now_ns();
    int s0 = 0;
    for (int i = 0; i < N_PEAK_FRAMES; i++) s0 ^= dm_opus_pcm_peak_scalar(frame, PEAK_LEN);
    uint64_t dt_s = op_now_ns() - t0;

    t0 = op_now_ns();
    int s1 = 0;
    for (int i = 0; i < N_PEAK_FRAMES; i++) s1 ^= dm_opus_pcm_peak_fast(frame, PEAK_LEN);
    uint64_t dt_f = op_now_ns() - t0;

    assert(s0 == s1);
    sink = s1;
    (void)sink;
    printf("aoi_opus_peak: frames=%d len=%d scalar_ms=%.2f fast_ms=%.2f speedup=%.2fx\n",
           N_PEAK_FRAMES, PEAK_LEN, dt_s / 1e6, dt_f / 1e6, dt_f ? (double)dt_s / (double)dt_f : 0.0);
}

typedef struct {
    uintptr_t keys[N_DECS];
    void *vals[N_DECS];
    pthread_mutex_t mu;
    dm_opus_cache cache;
    _Atomic uint64_t checksum;
    uint64_t locks;
    int fast;
} bench_ctx;

static void *legacy_lookup(bench_ctx *ctx, uintptr_t k) {
    pthread_mutex_lock(&ctx->mu);
    ctx->locks++;
    void *r = NULL;
    for (int i = 0; i < N_DECS; i++) if (ctx->keys[i] == k) { r = ctx->vals[i]; break; }
    pthread_mutex_unlock(&ctx->mu);
    return r;
}

static void *worker_fn(void *arg) {
    bench_ctx *ctx = (bench_ctx *)arg;
    uint64_t sum = 0;
    for (int i = 0; i < N_OPS; i++) {
        uintptr_t k = ctx->keys[i & (N_DECS - 1)];
        int ch = 0;
        void *v = ctx->fast ? dm_opus_cache_get(&ctx->cache, k, &ch) : legacy_lookup(ctx, k);
        assert(v != NULL);
        if (ctx->fast) assert(ch == 2);
        sum += (uintptr_t)v;
    }
    atomic_fetch_add(&ctx->checksum, sum);
    return NULL;
}

static void bench_cache(void) {
    bench_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    pthread_mutex_init(&ctx.mu, NULL);
    dm_opus_cache_init(&ctx.cache);
    for (int i = 0; i < N_DECS; i++) {
        ctx.keys[i] = (uintptr_t)(0x7000000000ull + (uintptr_t)i * 0x40u);
        ctx.vals[i] = (void *)(uintptr_t)(0x100000u + (uintptr_t)i * 0x100u);
        dm_opus_cache_put(&ctx.cache, ctx.keys[i], ctx.vals[i], 2);
    }

    pthread_t th[N_THREADS];
    ctx.fast = 0;
    uint64_t t0 = op_now_ns();
    for (int i = 0; i < N_THREADS; i++) assert(!pthread_create(&th[i], NULL, worker_fn, &ctx));
    for (int i = 0; i < N_THREADS; i++) assert(!pthread_join(th[i], NULL));
    uint64_t dt_leg = op_now_ns() - t0;
    uint64_t sum_leg = atomic_exchange(&ctx.checksum, 0);

    ctx.fast = 1;
    t0 = op_now_ns();
    for (int i = 0; i < N_THREADS; i++) assert(!pthread_create(&th[i], NULL, worker_fn, &ctx));
    for (int i = 0; i < N_THREADS; i++) assert(!pthread_join(th[i], NULL));
    uint64_t dt_fast = op_now_ns() - t0;
    uint64_t sum_fast = atomic_load(&ctx.checksum);
    assert(sum_leg == sum_fast);

    /* Lifecycle invalidation check */
    int ch = 0;
    assert(dm_opus_cache_get(&ctx.cache, ctx.keys[3], &ch) == ctx.vals[3] && ch == 2);
    if (g_fault != 2) dm_opus_cache_del(&ctx.cache, ctx.keys[3]);
    assert(dm_opus_cache_get(&ctx.cache, ctx.keys[3], &ch) == NULL);

    printf("aoi_opus_cache: threads=%d ops=%d legacy_locks=%llu fast_locks=0 legacy_ms=%.2f (%.1f ns/op) fast_ms=%.2f (%.1f ns/op) speedup=%.2fx\n",
           N_THREADS, N_THREADS * N_OPS, (unsigned long long)ctx.locks,
           dt_leg / 1e6, (double)dt_leg / (N_THREADS * N_OPS),
           dt_fast / 1e6, (double)dt_fast / (N_THREADS * N_OPS),
           dt_fast ? (double)dt_leg / (double)dt_fast : 0.0);
}

int main(void) {
    const char *f = getenv("T_OPUS_FAST_FAULT");
    g_fault = f ? atoi(f) : 0;
    verify_peak();
    bench_peak();
    bench_cache();
    printf("t_opus_fast: fault=%d PASS\n", g_fault);
    return 0;
}
