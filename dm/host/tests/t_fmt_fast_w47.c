// Wave 47 (Optimization 49): parity, concurrency, TSan, benchmark, and fault tests for vka_fmt_fast.h.
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../gfx/vk_android/vka_fmt_fast.h"

_Atomic int g_vka_fmt_fast_override = -1;
_Atomic int g_aoi_w47_fault = -1;
static int g_fail = 0;

#define EXPECT_EQ_U64(msg, want, got) do { \
    uint64_t wa_ = (uint64_t)(want), ga_ = (uint64_t)(got); \
    if (wa_ != ga_) { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d %s: want=%llu got=%llu\n", __FILE__, __LINE__, msg, \
                (unsigned long long)wa_, (unsigned long long)ga_); \
    } \
} while (0)

static uint64_t w47_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static vka_fmt_lut_ent_t ref_fmt_ent_of(int f) {
#define C(v) (vka_fmt_lut_ent_t){VKA_FMT_VK_##v, 0, 0}
#define U(v) (vka_fmt_lut_ent_t){VKA_FMT_VK_##v, 1, 0}
#define S(v) (vka_fmt_lut_ent_t){VKA_FMT_VK_##v, 2, 0}
#define SRGB(v) (vka_fmt_lut_ent_t){VKA_FMT_VK_##v, 0, 1}
    if (f >= 0x79 && f <= 0x86) return (vka_fmt_lut_ent_t){(uint32_t)(VKA_FMT_VK_ASTC_4x4_UNORM_BLOCK + 2 * (f - 0x79)), 0, 0};
    if (f >= 0x87 && f <= 0x94) return (vka_fmt_lut_ent_t){(uint32_t)(VKA_FMT_VK_ASTC_4x4_UNORM_BLOCK + 2 * (f - 0x87)), 0, 1};
    switch (f) {
    case 0x01: case 0x51: case 0x52: return f == 0x01 ? C(R8_UNORM) : f == 0x51 ? U(R8_UINT) : S(R8_SINT);
    case 0x02: return C(R8_SNORM); case 0x03: return U(R8_UINT); case 0x04: return S(R8_SINT);
    case 0x05: return C(R16_SFLOAT); case 0x06: return C(R16_UNORM); case 0x07: return C(R16_SNORM); case 0x08: case 0x53: return U(R16_UINT); case 0x09: case 0x54: return S(R16_SINT);
    case 0x0A: return C(R32_SFLOAT); case 0x0B: case 0x55: return U(R32_UINT); case 0x0C: case 0x56: return S(R32_SINT);
    case 0x0D: return C(R8G8_UNORM); case 0x0E: return C(R8G8_SNORM); case 0x0F: case 0x57: return U(R8G8_UINT); case 0x10: case 0x58: return S(R8G8_SINT);
    case 0x11: return C(R16G16_SFLOAT); case 0x12: return C(R16G16_UNORM); case 0x13: return C(R16G16_SNORM); case 0x14: case 0x59: return U(R16G16_UINT); case 0x15: case 0x5A: return S(R16G16_SINT);
    case 0x16: return C(R32G32_SFLOAT); case 0x17: case 0x5B: return U(R32G32_UINT); case 0x18: case 0x5C: return S(R32G32_SINT);
    case 0x22: return C(R32G32B32_SFLOAT); case 0x23: case 0x61: return U(R32G32B32_UINT); case 0x24: case 0x62: return S(R32G32B32_SINT);
    case 0x25: case 0x6D: return C(R8G8B8A8_UNORM); case 0x26: case 0x6E: return C(R8G8B8A8_SNORM); case 0x27: case 0x6F: case 0x63: return U(R8G8B8A8_UINT); case 0x28: case 0x70: case 0x64: return S(R8G8B8A8_SINT);
    case 0x29: case 0x71: return C(R16G16B16A16_SFLOAT); case 0x2A: case 0x72: return C(R16G16B16A16_UNORM); case 0x2B: case 0x73: return C(R16G16B16A16_SNORM);
    case 0x2C: case 0x74: case 0x65: return U(R16G16B16A16_UINT); case 0x2D: case 0x75: case 0x66: return S(R16G16B16A16_SINT);
    case 0x2E: case 0x76: return C(R32G32B32A32_SFLOAT); case 0x2F: case 0x77: case 0x67: return U(R32G32B32A32_UINT); case 0x30: case 0x78: case 0x68: return S(R32G32B32A32_SINT);
    case 0x31: return (vka_fmt_lut_ent_t){VKA_FMT_VK_S8_UINT, 4, 0};
    case 0x32: return (vka_fmt_lut_ent_t){VKA_FMT_VK_D16_UNORM, 3, 0};
    case 0x33: return (vka_fmt_lut_ent_t){VKA_FMT_VK_X8_D24_UNORM_PACK32, 3, 0};
    case 0x34: return (vka_fmt_lut_ent_t){VKA_FMT_VK_D32_SFLOAT, 3, 0};
    case 0x35: return (vka_fmt_lut_ent_t){VKA_FMT_VK_D24_UNORM_S8_UINT, 5, 0};
    case 0x36: return (vka_fmt_lut_ent_t){VKA_FMT_VK_D32_SFLOAT_S8_UINT, 5, 0};
    case 0x37: case 0x38: return SRGB(R8G8B8A8_UNORM);
    case 0x39: return C(A4B4G4R4_UNORM_PACK16);
    case 0x3A: case 0x3B: return C(A1B5G5R5_UNORM_PACK16);
    case 0x3C: return C(B5G6R5_UNORM_PACK16); case 0x95: return C(R5G6B5_UNORM_PACK16);
    case 0x3D: return C(A2B10G10R10_UNORM_PACK32); case 0x3E: case 0x6B: return U(A2B10G10R10_UINT_PACK32); case 0x69: return C(A2B10G10R10_SNORM_PACK32); case 0x6A: case 0x6C: return S(A2B10G10R10_SINT_PACK32);
    case 0x3F: return C(B10G11R11_UFLOAT_PACK32); case 0x40: return C(E5B9G9R9_UFLOAT_PACK32);
    case 0x41: return C(BC1_RGB_UNORM_BLOCK); case 0x42: return C(BC1_RGBA_UNORM_BLOCK); case 0x43: return C(BC2_UNORM_BLOCK); case 0x44: return C(BC3_UNORM_BLOCK);
    case 0x45: return SRGB(BC1_RGB_UNORM_BLOCK); case 0x46: return SRGB(BC1_RGBA_UNORM_BLOCK); case 0x47: return SRGB(BC2_UNORM_BLOCK); case 0x48: return SRGB(BC3_UNORM_BLOCK);
    case 0x49: return C(BC4_UNORM_BLOCK); case 0x4A: return C(BC4_SNORM_BLOCK); case 0x4B: return C(BC5_UNORM_BLOCK); case 0x4C: return C(BC5_SNORM_BLOCK);
    case 0x4D: return C(BC7_UNORM_BLOCK); case 0x4E: return SRGB(BC7_UNORM_BLOCK); case 0x4F: return C(BC6H_SFLOAT_BLOCK); case 0x50: return C(BC6H_UFLOAT_BLOCK);
    case 0x99: case 0x9A: return C(B8G8R8A8_UNORM); case 0x9B: case 0x9C: return SRGB(B8G8R8A8_UNORM);
    }
#undef C
#undef U
#undef S
#undef SRGB
    return (vka_fmt_lut_ent_t){0u, 0, 0};
}

static uint32_t ref_srgb_twin(uint32_t f) {
    switch (f) {
    case VKA_FMT_VK_R8G8B8A8_UNORM: return VKA_FMT_VK_R8G8B8A8_SRGB;
    case VKA_FMT_VK_B8G8R8A8_UNORM: return VKA_FMT_VK_B8G8R8A8_SRGB;
    case VKA_FMT_VK_BC1_RGB_UNORM_BLOCK: return VKA_FMT_VK_BC1_RGB_SRGB_BLOCK;
    case VKA_FMT_VK_BC1_RGBA_UNORM_BLOCK: return VKA_FMT_VK_BC1_RGBA_SRGB_BLOCK;
    case VKA_FMT_VK_BC2_UNORM_BLOCK: return VKA_FMT_VK_BC2_SRGB_BLOCK;
    case VKA_FMT_VK_BC3_UNORM_BLOCK: return VKA_FMT_VK_BC3_SRGB_BLOCK;
    case VKA_FMT_VK_BC7_UNORM_BLOCK: return VKA_FMT_VK_BC7_SRGB_BLOCK;
    default: return f >= VKA_FMT_VK_ASTC_4x4_UNORM_BLOCK && f <= VKA_FMT_VK_ASTC_12x12_SRGB_BLOCK ? f + 1u : f;
    }
}

static uint32_t ref_raw_unorm_twin(uint32_t f) {
    switch (f) {
    case VKA_FMT_VK_R8G8B8A8_SRGB: return VKA_FMT_VK_R8G8B8A8_UNORM;
    case VKA_FMT_VK_B8G8R8A8_SRGB: return VKA_FMT_VK_B8G8R8A8_UNORM;
    case VKA_FMT_VK_A8B8G8R8_SRGB_PACK32: return VKA_FMT_VK_A8B8G8R8_UNORM_PACK32;
    default: return f;
    }
}

typedef struct { _Atomic uint32_t probes; } w47_probe_ctx_t;
static uint32_t w47_probe_fn(uint32_t fmt, void *ctx) {
    w47_probe_ctx_t *p = (w47_probe_ctx_t *)ctx;
    atomic_fetch_add_explicit(&p->probes, 1u, memory_order_relaxed);
    return (fmt * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468);
}

static void test_lut_and_twins(void) {
    for (int f = -16; f <= 512; f++) {
        vka_fmt_lut_ent_t ref = ref_fmt_ent_of(f);
        vka_fmt_lut_ent_t got = vka_fmt_lut_lookup(f);
        EXPECT_EQ_U64("lut.vk", ref.vk, got.vk);
        EXPECT_EQ_U64("lut.kind", ref.kind, got.kind);
        EXPECT_EQ_U64("lut.srgb", ref.srgb, got.srgb);
    }
    for (uint32_t f = 0; f < 300u; f++) {
        EXPECT_EQ_U64("srgb_twin", ref_srgb_twin(f), vka_srgb_twin_fast(f));
        EXPECT_EQ_U64("raw_unorm_twin", ref_raw_unorm_twin(f), vka_raw_unorm_twin_fast(f));
    }
}

typedef struct { vka_feats_cache_t *c; w47_probe_ctx_t *p; } w47_feats_thr_arg_t;

static void *w47_feats_worker(void *arg) {
    w47_feats_thr_arg_t *a = (w47_feats_thr_arg_t *)arg;
    static const uint32_t fmts[6] = {
        VKA_FMT_VK_R8G8B8A8_UNORM, VKA_FMT_VK_B8G8R8A8_SRGB, VKA_FMT_VK_BC7_UNORM_BLOCK,
        VKA_FMT_VK_ASTC_4x4_UNORM_BLOCK, VKA_FMT_VK_A4B4G4R4_UNORM_PACK16, VKA_FMT_VK_A1B5G5R5_UNORM_PACK16
    };
    for (int it = 0; it < 200; it++) {
        uint32_t f = fmts[it % 6];
        uint32_t want = (f * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468);
        uint32_t got = vka_feats_fast_get(a->c, f, w47_probe_fn, a->p);
        if (got != want) atomic_fetch_add(&a->p->probes, 1000u);
    }
    return NULL;
}

static void test_feats_cache(void) {
    static vka_feats_cache_t c;
    vka_feats_cache_init(&c);
    w47_probe_ctx_t p = {0};
    for (uint32_t pass = 0; pass < 3u; pass++) {
        uint32_t a = vka_feats_fast_get(&c, VKA_FMT_VK_R8G8B8A8_UNORM, w47_probe_fn, &p);
        uint32_t b = vka_feats_fast_get(&c, VKA_FMT_VK_A4B4G4R4_UNORM_PACK16, w47_probe_fn, &p);
        uint32_t d = vka_feats_fast_get(&c, VKA_FMT_VK_A1B5G5R5_UNORM_PACK16, w47_probe_fn, &p);
        EXPECT_EQ_U64("core feat", (VKA_FMT_VK_R8G8B8A8_UNORM * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468), a);
        EXPECT_EQ_U64("ext1 feat", (VKA_FMT_VK_A4B4G4R4_UNORM_PACK16 * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468), b);
        EXPECT_EQ_U64("ext2 feat", (VKA_FMT_VK_A1B5G5R5_UNORM_PACK16 * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468), d);
    }
    EXPECT_EQ_U64("single probe per fmt", 3u, atomic_load(&p.probes));
    pthread_t th[4];
    w47_feats_thr_arg_t arg = {&c, &p};
    for (int t = 0; t < 4; t++) pthread_create(&th[t], NULL, w47_feats_worker, &arg);
    for (int t = 0; t < 4; t++) pthread_join(th[t], NULL);
    EXPECT_EQ_U64("threaded feats probe bound", 6u, atomic_load(&p.probes));
}

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t done_cv;
    _Atomic uint32_t next, done, waiting, sum;
    uint32_t n;
} w47_up_state_t;

static void *w47_up_worker(void *arg) {
    w47_up_state_t *s = (w47_up_state_t *)arg;
    for (;;) {
        uint32_t i = vka_up_claim_fast(&s->next, s->n);
        if (i >= s->n) break;
        atomic_fetch_add_explicit(&s->sum, i + 1u, memory_order_relaxed);
        vka_up_finish_part_fast(&s->done, s->n, &s->waiting, &s->mu, &s->done_cv);
    }
    return NULL;
}

static void *w47_det_worker(void *arg) {
    w47_up_state_t *s = (w47_up_state_t *)arg;
    for (uint32_t sp = 0u; sp < 5000000u && !atomic_load_explicit(&s->waiting, memory_order_acquire); sp++) {
    }
    uint32_t i = vka_up_claim_fast(&s->next, s->n);
    if (i < s->n) {
        atomic_fetch_add_explicit(&s->sum, i + 1u, memory_order_relaxed);
        vka_up_finish_part_fast(&s->done, s->n, &s->waiting, &s->mu, &s->done_cv);
    }
    return NULL;
}

static void test_upload_waiter_wake_det(void) {
    w47_up_state_t s = {
        .mu = PTHREAD_MUTEX_INITIALIZER, .done_cv = PTHREAD_COND_INITIALIZER,
        .next = 0, .done = 0, .waiting = 0, .sum = 0, .n = 1u
    };
    pthread_t th;
    pthread_create(&th, NULL, w47_det_worker, &s);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = (uint64_t)ts.tv_nsec + UINT64_C(80000000);
    ts.tv_sec += (time_t)(ns / UINT64_C(1000000000));
    ts.tv_nsec = (long)(ns % UINT64_C(1000000000));
    int timed_out = 0;
    pthread_mutex_lock(&s.mu);
    atomic_store_explicit(&s.waiting, 1u, memory_order_seq_cst);
    while (atomic_load_explicit(&s.done, memory_order_seq_cst) < s.n) {
        if (pthread_cond_timedwait(&s.done_cv, &s.mu, &ts) != 0) { timed_out = 1; break; }
    }
    atomic_store_explicit(&s.waiting, 0u, memory_order_seq_cst);
    pthread_mutex_unlock(&s.mu);
    pthread_join(th, NULL);
    EXPECT_EQ_U64("det wake not timed out", 0u, (uint64_t)timed_out);
    EXPECT_EQ_U64("det done count", 1u, atomic_load(&s.done));
}

static void test_upload_claim_and_wake(void) {
    w47_up_state_t s = {
        .mu = PTHREAD_MUTEX_INITIALIZER, .done_cv = PTHREAD_COND_INITIALIZER,
        .next = 0, .done = 0, .waiting = 0, .sum = 0, .n = 32u
    };
    pthread_t th[3];
    for (int t = 0; t < 3; t++) pthread_create(&th[t], NULL, w47_up_worker, &s);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ns = (uint64_t)ts.tv_nsec + UINT64_C(200000000);
    ts.tv_sec += (time_t)(ns / UINT64_C(1000000000));
    ts.tv_nsec = (long)(ns % UINT64_C(1000000000));
    int timed_out = 0;
    pthread_mutex_lock(&s.mu);
    atomic_store_explicit(&s.waiting, 1u, memory_order_seq_cst);
    while (atomic_load_explicit(&s.done, memory_order_seq_cst) < s.n) {
        if (pthread_cond_timedwait(&s.done_cv, &s.mu, &ts) != 0) { timed_out = 1; break; }
    }
    atomic_store_explicit(&s.waiting, 0u, memory_order_seq_cst);
    pthread_mutex_unlock(&s.mu);
    for (int t = 0; t < 3; t++) pthread_join(th[t], NULL);
    EXPECT_EQ_U64("wake not timed out", 0u, (uint64_t)timed_out);
    EXPECT_EQ_U64("done count", s.n, atomic_load(&s.done));
    EXPECT_EQ_U64("sum 1..32", 528u, atomic_load(&s.sum));
    test_upload_waiter_wake_det();
}

static uint32_t slow_feats(uint32_t fmt) {
    static struct { uint32_t f, ff; } cache[128];
    static int n = 0;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < n; i++) {
        if (cache[i].f == fmt) { uint32_t ff = cache[i].ff; pthread_mutex_unlock(&mu); return ff; }
    }
    uint32_t ff = (fmt * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468);
    if (n < 128) { cache[n].f = fmt; cache[n].ff = ff; n++; }
    pthread_mutex_unlock(&mu);
    return ff;
}

static uint32_t bench_noop_probe(uint32_t fmt, void *ctx) {
    (void)ctx;
    return (fmt * UINT32_C(0x9E3779B1)) ^ UINT32_C(0x13572468);
}

static void run_benchmark(void) {
    static vka_feats_cache_t fc;
    vka_feats_cache_init(&fc);
    volatile uint64_t sink = 0;
    const uint32_t fmts[32] = {
        0x01, 0x05, 0x0A, 0x0D, 0x11, 0x16, 0x25, 0x29,
        0x2E, 0x31, 0x32, 0x34, 0x36, 0x37, 0x3D, 0x3F,
        0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x48, 0x4D,
        0x4E, 0x4F, 0x79, 0x7A, 0x80, 0x87, 0x8E, 0x9C
    };
    uint64_t t0 = w47_now_ns();
    for (int it = 0; it < 200000; it++) {
        vka_fmt_lut_ent_t e = ref_fmt_ent_of((int)fmts[it & 31]);
        uint32_t twin = e.srgb ? ref_srgb_twin(e.vk) : ref_raw_unorm_twin(e.vk);
        sink += slow_feats(e.vk & 0xFFu) + twin;
    }
    uint64_t dt_slow = w47_now_ns() - t0;
    uint64_t t1 = w47_now_ns();
    for (int it = 0; it < 200000; it++) {
        vka_fmt_lut_ent_t e = vka_fmt_lut_lookup((int)fmts[it & 31]);
        uint32_t twin = e.srgb ? vka_srgb_twin_fast(e.vk) : vka_raw_unorm_twin_fast(e.vk);
        sink += vka_feats_fast_get(&fc, e.vk & 0xFFu, bench_noop_probe, NULL) + twin;
    }
    uint64_t dt_fast = w47_now_ns() - t1;
    printf("t_fmt_fast_w47: slow=%.3f ms fast=%.3f ms speedup=%.2fx sink=%llu\n",
           (double)dt_slow / 1e6, (double)dt_fast / 1e6,
           dt_fast ? (double)dt_slow / (double)dt_fast : 0.0, (unsigned long long)sink);
}

int main(void) {
    const char *env_f = getenv("AOI_W47_FAULT");
    if (env_f && *env_f) aoi_w47_set_fault(atoi(env_f));
    vka_fmt_fast_set_for_test(1);
    test_lut_and_twins();
    test_feats_cache();
    test_upload_claim_and_wake();
    vka_fmt_fast_set_for_test(0);
    test_lut_and_twins();
    vka_fmt_fast_set_for_test(1);
    run_benchmark();
    printf("t_fmt_fast_w47: %s (failures=%d)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail != 0;
}
