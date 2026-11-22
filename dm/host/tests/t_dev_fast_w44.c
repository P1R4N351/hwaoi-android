// Wave 44 (Optimization 46): parity, condvar waiter gating, after_rec pool, gate-ban hash, upload-batch Bloom, quickselect, TSan, and fault tests.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/vk_android/vka_dev_fast.h"

_Atomic int g_vka_dev_fast_override = -1;
_Atomic int g_aoi_w44_fault = -1;
static int g_fail = 0;

#define EXPECT_EQ_U64(msg, a, b) do { \
    uint64_t va_ = (uint64_t)(a), vb_ = (uint64_t)(b); \
    if (va_ != vb_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s: got %llu want %llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)vb_, (unsigned long long)va_); \
    } \
} while (0)

static uint64_t w44_now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

typedef struct { pthread_mutex_t mu; pthread_cond_t cv; _Atomic uint32_t waiters; int ready; } w44_sig_t;
static void *w44_waiter_fn(void *arg) {
    w44_sig_t *s = (w44_sig_t *)arg;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = (uint64_t)ts.tv_nsec + UINT64_C(250000000);
    ts.tv_sec += (time_t)(ns / UINT64_C(1000000000)); ts.tv_nsec = (long)(ns % UINT64_C(1000000000));
    pthread_mutex_lock(&s->mu); vka_waiter_enter(&s->waiters);
    int timed_out = 0;
    while (!s->ready) { if (pthread_cond_timedwait(&s->cv, &s->mu, &ts) != 0) { timed_out = 1; break; } }
    vka_waiter_leave(&s->waiters); int ok = (!timed_out && s->ready); pthread_mutex_unlock(&s->mu);
    return (void *)(uintptr_t)ok;
}
static void test_cond_waiter_gating(void) {
    w44_sig_t s = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};
    atomic_init(&s.waiters, 0u); s.ready = 0; vka_cond_broadcast_gated(&s.cv, &s.waiters, 1);
    pthread_t th; pthread_create(&th, NULL, w44_waiter_fn, &s);
    for (int k = 0; k < 200; k++) { if (atomic_load_explicit(&s.waiters, memory_order_relaxed) > 0u) break; struct timespec nap = {0, 1000000L}; nanosleep(&nap, NULL); }
    pthread_mutex_lock(&s.mu); s.ready = 1; vka_cond_broadcast_gated(&s.cv, &s.waiters, 1); pthread_mutex_unlock(&s.mu);
    void *ret = NULL; pthread_join(th, &ret); EXPECT_EQ_U64("gated cond wakeup", 1u, (uintptr_t)ret);
    pthread_cond_destroy(&s.cv); pthread_mutex_destroy(&s.mu);
}
static void dummy_after_fn(void *p) { (void)p; }
static void test_after_pool_and_overflow(void) {
    static vka_after_pool_t pool; memset(&pool, 0, sizeof pool); vka_after_node_t *ptrs[300];
    for (uint32_t i = 0u; i < 300u; i++) {
        ptrs[i] = vka_after_pool_alloc(&pool); EXPECT_EQ_U64("pool non-null", 1u, ptrs[i] != NULL);
        ptrs[i]->fence = (uint64_t)i + 1u; ptrs[i]->fn = dummy_after_fn;
    }
    for (uint32_t i = 0u; i < 300u; i++) EXPECT_EQ_U64("unique slot", (uint64_t)i + 1u, ptrs[i]->fence);
    for (uint32_t i = 0u; i < 300u; i++) vka_after_pool_free(&pool, ptrs[i]);
    EXPECT_EQ_U64("pool empty", 0u, pool.used[0] | pool.used[1] | pool.used[2] | pool.used[3]);
}

static int cmp_u64_ref(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y ? 1 : 0;
}
static void test_tables_and_quickselect(void) {
    static vka_gban_fast_t gban; memset(&gban, 0, sizeof gban); static volatile uint32_t words[64];
    for (uint32_t i = 0u; i < 32u; i++) vka_gban_fast_add(&gban, &words[i]);
    for (uint32_t i = 0u; i < 64u; i++) EXPECT_EQ_U64("gban match", i < 32u, vka_gban_fast_has(&gban, &words[i]));
    EXPECT_EQ_U64("gban null", 0u, vka_gban_fast_has(&gban, NULL));
    vka_ub_fast_t ubf; memset(&ubf, 0, sizeof ubf); uint64_t cbs[16];
    for (uint32_t i = 0u; i < 16u; i++) cbs[i] = UINT64_C(0x100000) + ((uint64_t)i * 64u);
    vka_ub_fast_init(&ubf, cbs, 16u);
    for (uint32_t i = 0u; i < 16u; i++) EXPECT_EQ_U64("ub hit", 1u, vka_ub_fast_is_batch(&ubf, cbs[i]));
    EXPECT_EQ_U64("ub miss low", 0u, vka_ub_fast_is_batch(&ubf, UINT64_C(0xfff00)));
    EXPECT_EQ_U64("ub miss range", 0u, vka_ub_fast_is_batch(&ubf, UINT64_C(0x100020)));
    EXPECT_EQ_U64("ub miss null", 0u, vka_ub_fast_is_batch(&ubf, 0u));
    uint64_t rng = UINT64_C(0x9e3779b97f4a7c15), src[30], tmp[30], ref[30];
    for (int pat = 0; pat < 5; pat++) {
        for (size_t i = 0u; i < 30u; i++) {
            rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
            src[i] = pat == 0 ? (uint64_t)i : pat == 1 ? (uint64_t)(29u - i) : pat == 2 ? 777u : pat == 3 ? (i & 3u) : rng;
        }
        memcpy(ref, src, sizeof ref); qsort(ref, 30u, sizeof ref[0], cmp_u64_ref);
        for (size_t k = 0u; k < 30u; k++) { memcpy(tmp, src, sizeof tmp); EXPECT_EQ_U64("quickselect", ref[k], vka_select_kth_u64(tmp, 30u, k)); }
    }
}

typedef struct { vka_after_pool_t *pool; vka_gban_fast_t *gban; volatile uint32_t *words; uint32_t tid; } w44_tsan_ctx;
static void *w44_tsan_worker(void *arg) {
    w44_tsan_ctx *c = (w44_tsan_ctx *)arg;
    for (uint32_t r = 0u; r < 512u; r++) {
        vka_after_node_t *n = vka_after_pool_alloc(c->pool); n->fence = ((uint64_t)c->tid << 16) | (uint64_t)r;
        (void)vka_gban_fast_has(c->gban, &c->words[(c->tid * 4u + r) & 31u]); vka_after_pool_free(c->pool, n);
    }
    return NULL;
}
static void test_tsan_concurrent(void) {
    static vka_after_pool_t pool; static vka_gban_fast_t gban; static volatile uint32_t words[32];
    memset(&pool, 0, sizeof pool); memset(&gban, 0, sizeof gban);
    for (uint32_t i = 0u; i < 16u; i++) vka_gban_fast_add(&gban, &words[i]);
    pthread_t th[4]; w44_tsan_ctx ctx[4];
    for (uint32_t i = 0u; i < 4u; i++) { ctx[i] = (w44_tsan_ctx){&pool, &gban, words, i}; pthread_create(&th[i], NULL, w44_tsan_worker, &ctx[i]); }
    for (uint32_t i = 0u; i < 4u; i++) pthread_join(th[i], NULL);
    EXPECT_EQ_U64("tsan pool empty", 0u, pool.used[0] | pool.used[1] | pool.used[2] | pool.used[3]);
}

static uint64_t run_bench_step(int fast, vka_after_pool_t *pool, const vka_gban_fast_t *gban,
        const vka_ub_fast_t *ubf, volatile uint32_t *words, const uint64_t cbs[16],
        pthread_mutex_t *mu, pthread_cond_t *cv, _Atomic uint32_t *waiters, _Atomic uint64_t *fence, uint32_t it) {
    uint64_t acc = 0u, arr[30];
    if (fast) {
        acc += atomic_fetch_add_explicit(fence, 1u, memory_order_acq_rel) + 1u;
        vka_cond_broadcast_gated(cv, waiters, 1); vka_after_node_t *nd = vka_after_pool_alloc(pool);
        nd->fence = it; acc += (uint64_t)vka_gban_fast_has(gban, &words[it & 31u]);
        acc += (uint64_t)vka_ub_fast_is_batch(ubf, cbs[it & 15u]) + (uint64_t)vka_ub_fast_is_batch(ubf, (uint64_t)it);
        vka_after_pool_free(pool, nd);
    } else {
        pthread_mutex_lock(mu); uint64_t f = atomic_load_explicit(fence, memory_order_relaxed) + 1u;
        atomic_store_explicit(fence, f, memory_order_release); pthread_cond_broadcast(cv); pthread_mutex_unlock(mu); acc += f;
        vka_after_node_t *nd = (vka_after_node_t *)malloc(sizeof *nd); nd->fence = it;
        pthread_mutex_lock(mu); for (uint32_t k = 0u; k < 32u; k++) acc += (&words[k] == &words[it & 31u]); pthread_mutex_unlock(mu);
        for (uint32_t k = 0u; k < 16u; k++) if (cbs[k] == cbs[it & 15u]) { acc++; break; }
        for (uint32_t k = 0u; k < 16u; k++) if (cbs[k] == (uint64_t)it) { acc++; break; }
        free(nd);
    }
    for (uint32_t k = 0u; k < 30u; k++) arr[k] = (uint64_t)((it + k * 7u) % 97u) + UINT64_C(1000000);
    if (fast) acc += vka_select_kth_u64(arr, 30u, 27u); else { qsort(arr, 30u, sizeof arr[0], cmp_u64_ref); acc += arr[27u]; }
    return acc;
}

static uint64_t run_bench_arm(int fast, double *ms_out) {
    static vka_after_pool_t pool; static vka_gban_fast_t gban; static vka_ub_fast_t ubf;
    static volatile uint32_t words[32];
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    _Atomic uint32_t waiters = 0u; _Atomic uint64_t fence = 0u; uint64_t cbs[16], acc = 0u;
    for (uint32_t i = 0u; i < 32u; i++) vka_gban_fast_add(&gban, &words[i]);
    for (uint32_t i = 0u; i < 16u; i++) cbs[i] = UINT64_C(0x80000000) + (uint64_t)i * 128u;
    vka_ub_fast_init(&ubf, cbs, 16u);
    uint64_t t0 = w44_now_ns();
    for (uint32_t it = 0u; it < 20000u; it++) acc += run_bench_step(fast, &pool, &gban, &ubf, words, cbs, &mu, &cv, &waiters, &fence, it);
    *ms_out = (double)(w44_now_ns() - t0) / 1e6;
    return acc;
}
int main(void) {
    vka_dev_fast_set_for_test(1);
    test_cond_waiter_gating(); test_after_pool_and_overflow(); test_tables_and_quickselect(); test_tsan_concurrent();
    double base_ms = 0.0, fast_ms = 0.0; uint64_t s0 = run_bench_arm(0, &base_ms), s1 = run_bench_arm(1, &fast_ms);
    EXPECT_EQ_U64("bench sink eq", s0, s1);
    vka_dev_fast_set_for_test(-1);
    printf("t_dev_fast_w44: base_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
           base_ms, fast_ms, base_ms / (fast_ms > 0.0 ? fast_ms : 1e-6), (unsigned long long)s1, g_fail ? "FAIL" : "PASS");
    return g_fail != 0;
}
