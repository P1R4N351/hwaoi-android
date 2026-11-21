// Unit, parity, unmap-safety, TSan, fault-injection & benchmark suite for DM_FPS_FAST (Wave 41 / Opt 43).
#define _GNU_SOURCE
#define T_W41_TEST_HOOKS 1
#include "../fps_fast.h"
#include "../drs_steps.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/uio.h>
#endif

_Atomic int g_dm_fps_fast_state;
_Atomic int g_dm_w41_fault;

static _Atomic uint64_t g_slow_calls;
static _Atomic uint32_t g_test_map_seq = 2u;
static uint8_t *g_test_heap_base;
static _Atomic size_t g_test_heap_open;
static uintptr_t g_blocked_page;
static int g_bench_pipe[2] = {-1, -1};
static int g_use_pipe_bench;

static uint64_t w41_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static void w41_ensure_pipe(void) {
    if (g_bench_pipe[0] < 0) {
        if (pipe(g_bench_pipe) != 0) {
            g_bench_pipe[0] = g_bench_pipe[1] = -1;
        }
    }
}

static int w41_kernel_read(uint64_t a, void *out, size_t n) {
    if (a < 0x1000u || !out) return 0;
    if (!n) return 1;
#if defined(__APPLE__)
    vm_size_t got = 0;
    kern_return_t kr = vm_read_overwrite(
        mach_task_self(), (vm_address_t)a, (vm_size_t)n, (vm_address_t)(uintptr_t)out, &got);
    return kr == KERN_SUCCESS && got == (vm_size_t)n;
#elif defined(__linux__)
    if (!g_use_pipe_bench) {
        struct iovec l = {out, n}, r = {(void *)(uintptr_t)a, n};
        ssize_t got = process_vm_readv(getpid(), &l, 1, &r, 1, 0);
        if (got == (ssize_t)n) return 1;
        if (got >= 0 || (errno != ENOSYS && errno != EPERM)) return 0;
    }
    w41_ensure_pipe();
    if (g_bench_pipe[0] < 0 || n > 4096u) return 0;
    ssize_t w = write(g_bench_pipe[1], (const void *)(uintptr_t)a, n);
    if (w != (ssize_t)n) return 0;
    ssize_t rd = read(g_bench_pipe[0], out, n);
    return rd == (ssize_t)n;
#else
    memcpy(out, (const void *)(uintptr_t)a, n);
    return 1;
#endif
}

static int w41_slow_rd(uint64_t a, void *out, size_t n) {
    atomic_fetch_add_explicit(&g_slow_calls, 1u, memory_order_relaxed);
    if (g_blocked_page && (a & ~UINT64_C(0xfff)) == g_blocked_page) return 0;
    return w41_kernel_read(a, out, n);
}

static int w41_heap_ok(uint64_t a, size_t n) {
    uint64_t b = (uint64_t)(uintptr_t)g_test_heap_base;
    size_t cur = atomic_load_explicit(&g_test_heap_open, memory_order_acquire);
    return g_test_heap_base && a >= b && n <= cur && a - b <= cur - n;
}

static uint32_t w41_map_seq(void) {
    return atomic_load_explicit(&g_test_map_seq, memory_order_acquire);
}

static int w41_test_chain_parity(void) {
    size_t pg = 4096u;
    uint8_t *mod = (uint8_t *)mmap(NULL, 4u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *hp = (uint8_t *)mmap(NULL, 4u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *ext = (uint8_t *)mmap(NULL, 4u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    int err = (mod == MAP_FAILED || hp == MAP_FAILED || ext == MAP_FAILED);
    if (!err) {
        uint64_t node_hp = (uint64_t)(uintptr_t)(hp + 0x180u);
        uint64_t node_ext = (uint64_t)(uintptr_t)(ext + 0x240u);
        memcpy(mod + 0x400u, &node_hp, sizeof node_hp);
        memcpy(hp + 0x180u + 0x18u, &node_ext, sizeof node_ext);
        int32_t vals[2] = {1, 3};
        g_test_heap_base = hp; g_blocked_page = 0u;
        atomic_store_explicit(&g_test_heap_open, 2u * pg, memory_order_release);
        atomic_store_explicit(&g_test_map_seq, 2u, memory_order_release);
        dm_fps_pcache_tls tls_on = {0}, tls_off = {0};
        for (int iter = 0; iter < 64 && !err; iter++) {
            vals[0] = 1 + (iter & 1);
            memcpy(ext + 0x240u + 0x40u, vals, sizeof vals);
            uint64_t p0a = 0, p0b = 0, p1a = 0, p1b = 0;
            int32_t v0a[2] = {0}, v0b[2] = {0};
            dm_fps_fast_set_for_test(1);
            int ok1 = dm_fps_rd_fast((uint64_t)(uintptr_t)(mod + 0x400u), &p0a, 8u, (uint64_t)(uintptr_t)mod, 4u * pg, &tls_on, w41_heap_ok, w41_map_seq, w41_slow_rd)
                && dm_fps_rd_fast(p0a + 0x18u, &p1a, 8u, (uint64_t)(uintptr_t)mod, 4u * pg, &tls_on, w41_heap_ok, w41_map_seq, w41_slow_rd)
                && dm_fps_rd_fast(p1a + 0x40u, v0a, sizeof v0a, (uint64_t)(uintptr_t)mod, 4u * pg, &tls_on, w41_heap_ok, w41_map_seq, w41_slow_rd);
            dm_fps_fast_set_for_test(0);
            int ok0 = dm_fps_rd_fast((uint64_t)(uintptr_t)(mod + 0x400u), &p0b, 8u, (uint64_t)(uintptr_t)mod, 4u * pg, &tls_off, w41_heap_ok, w41_map_seq, w41_slow_rd)
                && dm_fps_rd_fast(p0b + 0x18u, &p1b, 8u, (uint64_t)(uintptr_t)mod, 4u * pg, &tls_off, w41_heap_ok, w41_map_seq, w41_slow_rd)
                && dm_fps_rd_fast(p1b + 0x40u, v0b, sizeof v0b, (uint64_t)(uintptr_t)mod, 4u * pg, &tls_off, w41_heap_ok, w41_map_seq, w41_slow_rd);
            if (!ok1 || !ok0 || p0a != p0b || p1a != p1b || memcmp(v0a, v0b, sizeof v0a) != 0) err = 1;
        }
        dm_fps_fast_set_for_test(-1);
    }
    if (mod != MAP_FAILED) munmap(mod, 4u * pg);
    if (hp != MAP_FAILED) munmap(hp, 4u * pg);
    if (ext != MAP_FAILED) munmap(ext, 4u * pg);
    return err;
}

static int w41_test_unmap_safety(void) {
    size_t pg = 4096u;
    uint8_t *p = (uint8_t *)mmap(NULL, 2u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return 1;
    uint64_t magic = UINT64_C(0x1122334455667788), v = 0u;
    memcpy(p + 64u, &magic, sizeof magic);
    atomic_store_explicit(&g_test_map_seq, 4u, memory_order_release);
    dm_fps_pcache_tls tls = {0};
    dm_fps_fast_set_for_test(1);
    int ok = dm_fps_rd_fast((uint64_t)(uintptr_t)(p + 64u), &v, 8u, 0u, 0u, &tls, NULL, w41_map_seq, w41_slow_rd);
    if (!ok || v != magic) { munmap(p, 2u * pg); return 1; }
    g_blocked_page = (uintptr_t)p;
    atomic_store_explicit(&g_test_map_seq, 5u, memory_order_release);
    if (dm_fps_rd_fast((uint64_t)(uintptr_t)(p + 64u), &v, 8u, 0u, 0u, &tls, NULL, w41_map_seq, w41_slow_rd)) {
        g_blocked_page = 0u; munmap(p, 2u * pg); return 1;
    }
    g_blocked_page = 0u;
    int fault = dm_fps_w41_fault();
    if (fault != 3 && mprotect(p, pg, PROT_NONE) == 0) {
        atomic_store_explicit(&g_test_map_seq, 6u, memory_order_release);
        int hit = dm_fps_rd_fast((uint64_t)(uintptr_t)(p + 64u), &v, 8u, 0u, 0u, &tls, NULL, w41_map_seq, w41_slow_rd);
        mprotect(p, pg, PROT_READ | PROT_WRITE);
        if (hit) { dm_fps_fast_set_for_test(-1); munmap(p, 2u * pg); return 1; }
    }
    dm_fps_fast_set_for_test(-1);
    munmap(p, 2u * pg);
    return 0;
}

static inline uint32_t w41_f32_bits(float v) {
    uint32_t b = 0u;
    memcpy(&b, &v, sizeof(b));
    return b;
}

static int w41_test_drs_parity(void) {
    static const float sl10[9] = {1.0f, 1.375f, 1.5f, 5.0f / 3.0f, 12.0f / 7.0f, 11.0f / 6.0f, 2.0f, 2.2f, 3.0f};
    static const float tie2[4] = {1.0f, 1.5f, 2.0f, 3.0f};
    static const float nan1[3] = {1.0f, NAN, 1.5f};
    static const float caps[6] = {0.0f, 1.0f, 1.6f, 3.0f, INFINITY, NAN};
    int err = 0;
    for (int i = 0; i < 500 && !err; i++) {
        float r = 0.4f + (float)i * 0.03f;
        for (int c = 0; c < 6 && !err; c++) {
            dm_fps_fast_set_for_test(1);
            float f1 = drs_steps_fit(sl10, 9u, caps[c], r);
            float n1 = drs_steps_nearest(tie2, 4u, 1.2f);
            float s1 = drs_steps_snap(nan1, 3u, caps[c], r);
            dm_fps_fast_set_for_test(0);
            float f0 = drs_steps_fit(sl10, 9u, caps[c], r);
            float n0 = drs_steps_nearest(tie2, 4u, 1.2f);
            float s0 = drs_steps_snap(nan1, 3u, caps[c], r);
            if (w41_f32_bits(f1) != w41_f32_bits(f0) ||
                w41_f32_bits(n1) != w41_f32_bits(n0) ||
                w41_f32_bits(s1) != w41_f32_bits(s0)) err = 1;
        }
    }
    dm_fps_fast_set_for_test(-1);
    return err;
}

typedef struct {
    const uint8_t *mod;
    size_t pg;
    _Atomic int stop;
    _Atomic int fail;
} w41_tsan_ctx;

static void *w41_tsan_reader(void *arg) {
    w41_tsan_ctx *ctx = (w41_tsan_ctx *)arg;
    dm_fps_pcache_tls tls = {0};
    for (int i = 0; i < 4000 && !atomic_load_explicit(&ctx->fail, memory_order_relaxed); i++) {
        uint64_t val = 0u;
        int ok = dm_fps_rd_fast((uint64_t)(uintptr_t)(ctx->mod + 128u), &val, 8u,
            (uint64_t)(uintptr_t)ctx->mod, 2u * ctx->pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
        if (!ok || val != UINT64_C(0xdeadbeefcafebabe)) {
            atomic_store_explicit(&ctx->fail, 1, memory_order_relaxed);
            break;
        }
        uint64_t hv = 0u;
        (void)dm_fps_rd_fast((uint64_t)(uintptr_t)(g_test_heap_base + 256u), &hv, 8u,
            (uint64_t)(uintptr_t)ctx->mod, 2u * ctx->pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
    }
    return NULL;
}

static int w41_test_tsan(void) {
    size_t pg = 4096u;
    uint8_t *mod = (uint8_t *)mmap(NULL, 2u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *hp = (uint8_t *)mmap(NULL, 2u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    int fail = (mod == MAP_FAILED || hp == MAP_FAILED);
    if (!fail) {
        uint64_t mval = UINT64_C(0xdeadbeefcafebabe);
        memcpy(mod + 128u, &mval, sizeof mval);
        memcpy(hp + 256u, &mval, sizeof mval);
        g_test_heap_base = hp;
        atomic_store_explicit(&g_test_heap_open, pg, memory_order_release);
        atomic_store_explicit(&g_test_map_seq, 2u, memory_order_release);
        w41_tsan_ctx ctx = {.mod = mod, .pg = pg};
        pthread_t th[3];
        int started = 0;
        for (int i = 0; i < 3; i++) {
            if (pthread_create(&th[i], NULL, w41_tsan_reader, &ctx) != 0) { fail = 1; break; }
            started++;
        }
        if (!fail) {
            for (int i = 0; i < 400; i++) {
                uint32_t s = atomic_fetch_add_explicit(&g_test_map_seq, 1u, memory_order_acq_rel);
                atomic_store_explicit(&g_test_heap_open, (i & 1) ? pg : 0u, memory_order_release);
                atomic_store_explicit(&g_test_map_seq, s + 2u, memory_order_release);
            }
        }
        for (int i = 0; i < started; i++) pthread_join(th[i], NULL);
        fail |= atomic_load_explicit(&ctx.fail, memory_order_acquire);
    }
    if (mod != MAP_FAILED) munmap(mod, 2u * pg);
    if (hp != MAP_FAILED) munmap(hp, 2u * pg);
    return fail;
}

static uint64_t w41_bench_one(int fast_on, int use_pipe, size_t iters) {
    size_t pg = 4096u;
    uint8_t *mod = (uint8_t *)mmap(NULL, 2u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *hp = (uint8_t *)mmap(NULL, 2u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *ext = (uint8_t *)mmap(NULL, 2u * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint64_t sink = 0u;
    if (mod != MAP_FAILED && hp != MAP_FAILED && ext != MAP_FAILED) {
        uint64_t p_app = (uint64_t)(uintptr_t)(hp + 0x100u);
        uint64_t p_fw = (uint64_t)(uintptr_t)(hp + 0x400u);
        uint64_t p_ctl = (uint64_t)(uintptr_t)(ext + 0x200u);
        memcpy(mod + 0x120u, &p_app, 8u);
        memcpy(hp + 0x100u, &p_fw, 8u);
        memcpy(hp + 0x400u + 0x218u, &p_ctl, 8u);
        int32_t ctl_w[20] = {0}; ctl_w[0] = 1; ctl_w[1] = 3; ctl_w[3] = 2; ctl_w[16] = 1; ctl_w[17] = 1;
        memcpy(ext + 0x200u, ctl_w, sizeof ctl_w);
        static const float sl10[9] = {1.0f, 1.375f, 1.5f, 5.0f / 3.0f, 12.0f / 7.0f, 11.0f / 6.0f, 2.0f, 2.2f, 3.0f};
        g_test_heap_base = hp; g_use_pipe_bench = use_pipe;
        atomic_store_explicit(&g_test_heap_open, 2u * pg, memory_order_release);
        atomic_store_explicit(&g_test_map_seq, 2u, memory_order_release);
        dm_fps_pcache_tls tls = {0};
        dm_fps_fast_set_for_test(fast_on);
        for (size_t i = 0; i < iters; i++) {
            uint64_t app = 0, fw = 0, ctl = 0; int32_t mx = 0; int32_t block[20] = {0};
            dm_fps_rd_fast((uint64_t)(uintptr_t)(mod + 0x120u), &app, 8u, (uint64_t)(uintptr_t)mod, 2u * pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
            dm_fps_rd_fast(app, &fw, 8u, (uint64_t)(uintptr_t)mod, 2u * pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
            dm_fps_rd_fast(fw + 0x218u, &ctl, 8u, (uint64_t)(uintptr_t)mod, 2u * pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
            dm_fps_rd_fast(ctl + 4u, &mx, 4u, (uint64_t)(uintptr_t)mod, 2u * pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
            dm_fps_rd_fast(ctl, block, sizeof block, (uint64_t)(uintptr_t)mod, 2u * pg, &tls, w41_heap_ok, w41_map_seq, w41_slow_rd);
            float fit = drs_steps_fit(sl10, 9u, (float)mx, 1.25f + 0.01f * (float)(i & 15u));
            uint32_t fb = 0u; memcpy(&fb, &fit, 4u);
            sink += (uint64_t)(mx + block[0] + block[3]) ^ fb;
        }
        g_use_pipe_bench = 0; dm_fps_fast_set_for_test(-1);
    }
    if (mod != MAP_FAILED) munmap(mod, 2u * pg);
    if (hp != MAP_FAILED) munmap(hp, 2u * pg);
    if (ext != MAP_FAILED) munmap(ext, 2u * pg);
    return sink;
}

int main(void) {
    const size_t iters = 100000u;
    int err = 0;
    if (w41_test_chain_parity() != 0) { puts("FAIL chain_parity"); err = 1; }
    if (w41_test_unmap_safety() != 0) { puts("FAIL unmap_safety"); err = 1; }
    if (w41_test_drs_parity() != 0) { puts("FAIL drs_parity"); err = 1; }
    if (w41_test_tsan() != 0) { puts("FAIL tsan"); err = 1; }
    atomic_store_explicit(&g_slow_calls, 0u, memory_order_relaxed);
    uint64_t t0 = w41_ns(), s0 = w41_bench_one(0, 0, iters), t1 = w41_ns();
    uint64_t calls0 = atomic_load_explicit(&g_slow_calls, memory_order_relaxed);
    atomic_store_explicit(&g_slow_calls, 0u, memory_order_relaxed);
    uint64_t t2 = w41_ns(), s1 = w41_bench_one(1, 0, iters), t3 = w41_ns();
    uint64_t calls1 = atomic_load_explicit(&g_slow_calls, memory_order_relaxed);
    if (s0 != s1) { printf("FAIL sink mismatch %llu != %llu\n", (unsigned long long)s0, (unsigned long long)s1); err = 1; }
    double slow_ms = (double)(t1 - t0) / 1e6, fast_ms = (double)(t3 - t2) / 1e6;
    printf("t_fps_fast_w41: req_off=0 slow_ms=%.3f fast_ms=%.3f speedup=%.2fx calls=%llu->%llu sink=%llu %s\n",
        slow_ms, fast_ms, fast_ms > 0.0 ? slow_ms / fast_ms : 0.0,
        (unsigned long long)calls0, (unsigned long long)calls1, (unsigned long long)s1, err ? "FAIL" : "PASS");
#if defined(__linux__)
    uint64_t p0 = w41_ns(), sp0 = w41_bench_one(0, 1, iters), p1 = w41_ns();
    uint64_t p2 = w41_ns(), sp1 = w41_bench_one(1, 1, iters), p3 = w41_ns();
    if (sp0 != sp1) { printf("FAIL pipe sink mismatch\n"); err = 1; }
    double pslow = (double)(p1 - p0) / 1e6, pfast = (double)(p3 - p2) / 1e6;
    printf("t_fps_fast_w41: pipe_fallback slow_ms=%.3f fast_ms=%.3f speedup=%.2fx sink=%llu %s\n",
        pslow, pfast, pfast > 0.0 ? pslow / pfast : 0.0, (unsigned long long)sp1, err ? "FAIL" : "PASS");
#endif
    return err;
}
