#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W37_TEST_HOOKS 1
#include "../../gfx/vk_android/vka_perf_fast.h"
#include "../../gfx/vk_android/vka_pace.h"

_Atomic int g_vka_perf_fast_state;
_Atomic int g_vka_w37_fault;

static uint64_t now_ns_w37(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int dummy_env_read(const char *k, int def) {
    const char *v = getenv(k);
    return v && v[0] ? atoi(v) : def;
}

static double legacy_pct_ref(uint32_t *v, int n, int pct) {
    if (n <= 0) return -1;
    for (int i = 1; i < n && i < VKA_PACE_MAX_N; i++) {
        uint32_t x = v[i];
        int j = i;
        while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
        v[j] = x;
    }
    int k = (int)((int64_t)n * pct / 100);
    if (k >= n) k = n - 1;
    return v[k] / 1000.0;
}

static void test_env_and_parsers(void) {
    setenv("W37_TEST_VAR", "123", 1);
    vka_env_gen_i32 c = {0};
    int a = vka_env_cached_i32(&c, 0u, "W37_TEST_VAR", 7, dummy_env_read);
    setenv("W37_TEST_VAR", "456", 1);
    int b = vka_env_cached_i32(&c, 0u, "W37_TEST_VAR", 7, dummy_env_read);
    int d = vka_env_cached_i32(&c, 1u, "W37_TEST_VAR", 7, dummy_env_read);
    assert(a == 123 && b == 123 && d == 456);
    vka_env_gen_i32 z = {0};
    setenv("W37_TEST_VAR", "0", 1);
    assert(vka_env_cached_i32(&z, 0u, "W37_TEST_VAR", 9, dummy_env_read) == 0);
    setenv("W37_TEST_VAR", "88", 1);
    assert(vka_env_cached_i32(&z, 0u, "W37_TEST_VAR", 9, dummy_env_read) == 0);

    uint64_t u = 0, s = 0;
    const char *num = " 2745000\n";
    assert(vka_parse_u64_one(num, strlen(num), &u) && u == 2745000ull);
    assert(!vka_parse_u64_one("abc", 3u, &u));
    const char *stat = "42 (vk-replay ) S 1 42 42 0 -1 4194368 100 0 2 0 12345 6789 0 0 20 0\n";
    assert(vka_parse_stat_ut_st(stat, strlen(stat), &u, &s));
    assert(u == 12345ull && s == 6789ull);

    char fbuf[128] = {0};
    const char *tmp = "/tmp/t_w37_read_check.txt";
    FILE *fp = fopen(tmp, "w");
    assert(fp != NULL);
    fputs("987654\n", fp);
    fclose(fp);
    ssize_t rn = vka_read_small_file(tmp, fbuf, sizeof fbuf);
    unlink(tmp);
    assert(rn > 0 && vka_parse_u64_one(fbuf, (size_t)rn, &u) && u == 987654ull);
}

static void test_tid_map_and_pct_parity(void) {
    vka_tid_map m;
    vka_tid_map_init(&m);
    for (int i = 0; i < 192; i++) vka_tid_map_put(&m, 1000 + i * 7, (uint64_t)(i + 1) * 1111ull);
    for (int i = 0; i < 192; i++) assert(vka_tid_map_get(&m, 1000 + i * 7) == (uint64_t)(i + 1) * 1111ull);
    assert(vka_tid_map_get(&m, 99999) == 0);

    static uint32_t a[VKP_N], b[VKP_N], c[VKP_N];
    const int sizes[] = {1, 2, 7, 17, 64, 255, 513, VKP_N};
    for (size_t si = 0; si < sizeof sizes / sizeof sizes[0]; si++) {
        int n = sizes[si];
        for (int pat = 0; pat < 4; pat++) {
            for (int i = 0; i < n; i++) {
                uint32_t x = pat == 0 ? (uint32_t)(n - i) * 100u
                    : pat == 1 ? (uint32_t)((i * 37) % 19) * 500u
                    : pat == 2 ? 16667u
                    : (uint32_t)(((uint32_t)i * 2654435761u) & 0x3ffffu);
                a[i] = b[i] = c[i] = x;
            }
            double r50 = legacy_pct_ref(a, n, 50);
            double r90 = legacy_pct_ref(b, n, 90);
            double f50 = 0, f90 = 0;
            vka_pace_p50_p90(c, n, &f50, &f90);
            assert(fabs(f50 - r50) < 1e-12 && fabs(f90 - r90) < 1e-12);
        }
    }
    vka_perf_fast_reset_for_test(0);
    for (int i = 0; i < 100; i++) a[i] = (uint32_t)(100 - i) * 1000u;
    double off50 = 0, off90 = 0;
    vkpace_p50_p90(a, 100, &off50, &off90);
    assert(fabs(off50 - 51.0) < 1e-12 && fabs(off90 - 91.0) < 1e-12);
    vka_perf_fast_reset_for_test(1);
}

static double bench_mode(int fast_on, uint64_t *sink) {
    static uint32_t gpu_buf[VKP_N], span_buf[VKP_N];
    static int tids[192];
    static uint64_t bns[192], nns[192];
    for (int i = 0; i < 192; i++) {
        tids[i] = 2000 + i * 3;
        bns[i] = (uint64_t)i * 1000000ull;
        nns[i] = bns[i] + (uint64_t)((i * 19) % 50) * 100000ull;
    }
    const char *stat = "42 (vk-replay ) S 1 42 42 0 -1 4194368 100 0 2 0 12345 6789 0 0 20 0\n";
    const size_t stat_len = strlen(stat);
    vka_env_gen_i32 envc = {0};
    setenv("W37_BENCH_ENV", "1", 1);
    uint64_t t0 = now_ns_w37(), acc = 0;
    for (int r = 0; r < 1200; r++) {
        for (int i = 0; i < VKP_N; i++) {
            uint32_t v = (uint32_t)(((uint32_t)(i + r) * 2654435761u) & 0x1ffffu);
            gpu_buf[i] = v;
            span_buf[i] = (uint32_t)(100000 - i * 50 + (r & 15));
        }
        double p50 = 0, p90 = 0, s50 = 0, s90 = 0;
        if (fast_on) {
            vka_pace_p50_p90(gpu_buf, VKP_N, &p50, &p90);
            vka_pace_p50_p90(span_buf, VKP_N, &s50, &s90);
            vka_tid_map tm;
            vka_tid_map_init(&tm);
            for (int i = 0; i < 192; i++) vka_tid_map_put(&tm, tids[i], bns[i]);
            for (int i = 0; i < 192; i++) {
                uint64_t prev = vka_tid_map_get(&tm, tids[i]);
                acc += nns[i] > prev ? nns[i] - prev : 0;
            }
            uint64_t ut = 0, st = 0;
            vka_parse_stat_ut_st(stat, stat_len, &ut, &st);
            acc += ut + st + (uint64_t)vka_env_cached_i32(&envc, 1u, "W37_BENCH_ENV", 1, dummy_env_read);
        } else {
            p50 = legacy_pct_ref(gpu_buf, VKP_N, 50);
            p90 = legacy_pct_ref(gpu_buf, VKP_N, 90);
            s50 = legacy_pct_ref(span_buf, VKP_N, 50);
            s90 = legacy_pct_ref(span_buf, VKP_N, 90);
            for (int i = 0; i < 192; i++) {
                uint64_t prev = 0;
                for (int j = 0; j < 192; j++) if (bns[j] && tids[j] == tids[i]) { prev = bns[j]; break; }
                acc += nns[i] > prev ? nns[i] - prev : 0;
            }
            const char *q = strrchr(stat, ')');
            unsigned long long ut = 0, st = 0;
            if (q && sscanf(q + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) == 2) acc += ut + st;
            acc += (uint64_t)dummy_env_read("W37_BENCH_ENV", 1);
        }
        acc += (uint64_t)p50 + (uint64_t)p90 + (uint64_t)s50 + (uint64_t)s90;
    }
    *sink += acc;
    return (double)(now_ns_w37() - t0) / 1e6;
}

int main(void) {
    vka_perf_fast_reset_for_test(1);
    const char *fe = getenv("AOI_W37_FAULT");
    if (fe && fe[0]) vka_w37_set_fault(atoi(fe));
    test_env_and_parsers();
    test_tid_map_and_pct_parity();
    if (vka_w37_fault() != 0) return 0;
    uint64_t s0 = 0, s1 = 0;
    double ms0 = bench_mode(0, &s0);
    double ms1 = bench_mode(1, &s1);
    assert(s0 == s1);
    printf("t_perf_fast_w37: OK off_ms=%.3f on_ms=%.3f speedup=%.2fx sink=%llu\n",
           ms0, ms1, ms1 > 0 ? ms0 / ms1 : 0.0, (unsigned long long)s1);
    return 0;
}
