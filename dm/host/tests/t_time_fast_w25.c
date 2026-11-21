#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__) || defined(__aarch64__)
#define T_W25_TEST_HOOKS 1
#include "../hle.h"
#undef HLE_FORCE
#define HLE_FORCE(mangled, why, ret, cname, ...) static ret cname(__VA_ARGS__)
#include "../hle_time.c"
static Result dm_hle_sf_event(uint32_t *w) {
    static _Atomic uint32_t nx = 0x1000u;
    uint32_t r = atomic_fetch_add_explicit(&nx, 1u, memory_order_relaxed);
    if (w) *w = r + 0x1000u;
    return RES_OK;
}
uint32_t hle_new_event(uint32_t *w) {
    uint32_t r = 0;
    (void)dm_hle_sf_event(&r);
    if (w) *w = r;
    return r;
}
#include "../hle_sf.c"

void dm_guest_sleep_ns(int64_t ns) { (void)ns; }
void *guest_sym(const char *n) { (void)n; return NULL; }
void *dm_hle_force_keep = NULL;
static _Atomic uint64_t g_fake_ns = 1000000000ull;
uint64_t guest_ns(void) { return atomic_load_explicit(&g_fake_ns, memory_order_relaxed); }

static uint64_t bench_now_ns(void) { return plat_clock_ns(CLOCK_MONOTONIC); }

static int run_mac_fault(int f) {
    time_fast_reset_for_test(1);
    setenv("TZ", "UTC", 1); tzset();
    if (f == 1) {
        uint8_t a[0x20], b[0x20];
        h_time_GetContext(a); h_time_GetContext(b);
        return memcmp(a, b, 0x20) == 0 ? 0 : 1;
    }
    if (f == 2) {
        CalendarTime c1, c2; CalendarAdditionalInfo i1, i2;
        int64_t t1 = 1700000000, t2 = 1700003600;
        h_time_ToCalendarTime(&c1, &i1, &t1);
        h_time_ToCalendarTime(&c2, &i2, &t2);
        return c2.hour != c1.hour ? 0 : 1;
    }
    if (f == 3) {
        CalendarTime c1, c2; CalendarAdditionalInfo i1, i2; int64_t t = 1700000000;
        setenv("TZ", "UTC", 1); tzset(); h_time_ToCalendarTime(&c1, &i1, &t);
        setenv("TZ", "Asia/Seoul", 1); tzset(); h_time_ToCalendarTime(&c2, &i2, &t);
        return i2.offset == 9 * 3600 && c2.hour != c1.hour ? 0 : 1;
    }
    if (f == 4) {
        uint8_t s1[0xd0], s2[0xd0];
        setenv("TZ", "UTC", 1); tzset();
        h_time_Snapshot(s1); h_time_Snapshot(s2);
        return strcmp((const char *)s2 + 0xa8, "UTC") == 0 ? 0 : 1;
    }
    if (f == 5) {
        uint32_t h = 0, m[0x40];
        h_sf_Connect(&h, "olsc:u");
        memset(m, 0, sizeof m); m[0] = 4; m[1] = 8; m[4] = 0x49434653u; m[6] = 1;
        Result r = h_sf_Send(h, m, sizeof m);
        h_sf_Close(h);
        return (r == RES_OK && m[4] == 0x4f434653u) ? 0 : 1;
    }
    return 1;
}

static void verify_parity(void) {
    setenv("TZ", "UTC", 1); tzset();
    int64_t t1 = 1794761043, t2 = 1700000000;
    CalendarTime ct0, ct1; CalendarAdditionalInfo ai0, ai1;
    uint8_t ctx0[0x20], ctx1[0x20], snap0[0xf0], snap1[0xf0];
    time_fast_reset_for_test(0);
    h_time_GetContext(ctx0);
    h_time_ToCalendarTime(&ct0, &ai0, &t1);
    memset(snap0, 0xa5, sizeof snap0);
    assert(h_time_Snapshot(snap0 + 0x10) == RES_OK);
    time_fast_reset_for_test(1);
    h_time_GetContext(ctx1); h_time_GetContext(ctx1);
    assert(memcmp(ctx0, ctx1, 0x20) == 0);
    h_time_ToCalendarTime(&ct1, &ai1, &t1);
    h_time_ToCalendarTime(&ct1, &ai1, &t1);
    assert(memcmp(&ct0, &ct1, sizeof ct0) == 0 && memcmp(&ai0, &ai1, sizeof ai0) == 0);
    setenv("TZ", "Asia/Seoul", 1); tzset();
    h_time_ToCalendarTime(&ct1, &ai1, &t1);
    assert(ai1.offset == 9 * 3600 && ct1.day == 16 && ct1.hour == 1);
    setenv("TZ", "America/New_York", 1); tzset();
    h_time_ToCalendarTime(&ct1, &ai1, &t2);
    assert(ai1.offset == -5 * 3600);
    setenv("TZ", "UTC", 1); tzset();
    h_time_ToCalendarTime(&ct1, &ai1, &t1);
    assert(memcmp(&ct0, &ct1, sizeof ct0) == 0 && ai1.offset == 0);
    memset(snap1, 0xa5, sizeof snap1);
    assert(h_time_Snapshot(snap1 + 0x10) == RES_OK);
    assert(h_time_Snapshot(snap1 + 0x10) == RES_OK);
    for (int i = 0; i < 0x10; i++) assert(snap1[i] == 0xa5 && snap1[0xe0 + i] == 0xa5);
    assert(strcmp((const char *)snap1 + 0x10 + 0xa8, (const char *)snap0 + 0x10 + 0xa8) == 0);
    uint32_t sf = 0, msg[0x40];
    assert(h_sf_Connect(&sf, "olsc:u") == RES_OK && sf);
    for (int k = 0; k < 4; k++) {
        memset(msg, 0, sizeof msg); msg[0] = 4; msg[1] = 8; msg[4] = 0x49434653u; msg[6] = (uint32_t)k;
        assert(h_sf_Send(sf, msg, sizeof msg) == RES_OK && msg[4] == 0x4f434653u && msg[6] == RES_OK);
    }
    h_sf_Close(sf);
}

typedef struct { uint32_t sf; _Atomic int stop; } stress_arg_t;

static void *mac_stress_fn(void *p) {
    stress_arg_t *a = (stress_arg_t *)p;
    int64_t ts = 1794761043; uint8_t ctx[0x20], sn[0xd0]; uint32_t msg[0x40];
    CalendarTime ct; CalendarAdditionalInfo ai;
    for (int i = 0; i < 2000 && !atomic_load_explicit(&a->stop, memory_order_relaxed); i++) {
        if ((i & 3) == 0) h_time_GetContext(ctx);
        h_time_ToCalendarTime(&ct, &ai, &ts);
        if ((i & 7) == 0) h_time_Snapshot(sn);
        memset(msg, 0, sizeof msg); msg[0] = 4; msg[1] = 8; msg[4] = 0x49434653u; msg[6] = (uint32_t)(i & 1);
        assert(h_sf_Send(a->sf, msg, sizeof msg) == RES_OK && msg[4] == 0x4f434653u);
    }
    return NULL;
}

static void run_mac_benchmarks(void) {
    int64_t ts = 1794761043; uint8_t ctx[0x20], sn[0xd0]; CalendarTime ct; CalendarAdditionalInfo ai;
    uint32_t sf = 0, msg[0x40]; assert(h_sf_Connect(&sf, "olsc:u") == RES_OK);
    for (int mode = 0; mode < 2; mode++) {
        volatile uint64_t sink = 0;
        time_fast_reset_for_test(mode);
        uint64_t t0 = bench_now_ns();
        for (int i = 0; i < 50000; i++) {
            h_time_GetContext(ctx); h_time_ToCalendarTime(&ct, &ai, &ts); h_time_Snapshot(sn);
            memset(msg, 0, 32); msg[0] = 4; msg[1] = 8; msg[4] = 0x49434653u; msg[6] = 0;
            h_sf_Send(sf, msg, sizeof msg);
            sink += (uint64_t)ct.year + (uint64_t)ctx[0] + (uint64_t)sn[0xa8] + msg[6];
        }
        double ms = (double)(bench_now_ns() - t0) / 1e6;
        printf("BENCH aoi_w25_%s time_sf=%.3f ms sink=%llu\n", mode ? "on" : "off", ms, (unsigned long long)sink);
    }
    h_sf_Close(sf);
}

int main(void) {
    int f = time_fast_w25_fault();
    if (f > 0) {
        int rc = run_mac_fault(f);
        printf("t_time_fast_w25: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    verify_parity();
    stress_arg_t arg = {0};
    assert(h_sf_Connect(&arg.sf, "olsc:u") == RES_OK);
    time_fast_reset_for_test(1);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, mac_stress_fn, &arg) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    h_sf_Close(arg.sf);
    run_mac_benchmarks();
    printf("t_time_fast_w25: PASS\n");
    return 0;
}
#else
#define T_W25_TEST_HOOKS 1
#include "time_fast.h"

static uint64_t lin_now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

typedef struct { int16_t year; int8_t month, day, hour, minute, second, pad; } lin_ct_t;
typedef struct { uint32_t wday, yday; char tzname[8]; uint32_t dst; int32_t offset; } lin_ai_t;

static void lin_slow_cal(int64_t sec, lin_ct_t *ct, lin_ai_t *ai) {
    time_t t = (time_t)sec; struct tm tm; localtime_r(&t, &tm);
    *ct = (lin_ct_t){(int16_t)(tm.tm_year + 1900), (int8_t)(tm.tm_mon + 1), (int8_t)tm.tm_mday,
                     (int8_t)tm.tm_hour, (int8_t)tm.tm_min, (int8_t)tm.tm_sec, 0};
    memset(ai, 0, sizeof *ai); ai->wday = (uint32_t)tm.tm_wday; ai->yday = (uint32_t)tm.tm_yday;
    snprintf(ai->tzname, sizeof ai->tzname, "%s", tm.tm_zone ? tm.tm_zone : "UTC");
    ai->dst = tm.tm_isdst > 0; ai->offset = (int32_t)tm.tm_gmtoff;
}

static int run_linux_fault(int f) {
    if (f == 1) {
        time_fast_ctx_slot s = {0}; uint8_t in[0x20] = {7}, out[0x20] = {0};
        time_fast_ctx_write(&s, 0, in);
        time_fast_ctx_read(&s, 1, 0, out);
        return memcmp(in, out, 0x20) == 0 ? 0 : 1;
    }
    if (f == 2 || f == 3) {
        time_fast_cal_slot s = {0}; lin_ct_t ct, c2; lin_ai_t ai, a2;
        uint64_t tz1 = time_fast_tz_tag("UTC"), tz2 = f == 3 ? time_fast_tz_tag("Asia/Tokyo") : tz1;
        lin_slow_cal(1700000000, &ct, &ai);
        time_fast_cal_write(&s, 1700000000, tz1, &ct, &ai);
        int64_t sec2 = f == 2 ? 1700003600 : 1700000000;
        return time_fast_cal_read(&s, sec2, tz2, &c2, &a2) ? 1 : 0;
    }
    if (f == 4) {
        time_fast_loc_slot s = {0}; char in[0x24] = "UTC", out[0x24] = {0};
        time_fast_loc_write(&s, 1ull, in);
        time_fast_loc_read(&s, 1ull, out);
        return strcmp(in, out) == 0 ? 0 : 1;
    }
    if (f == 5) {
        time_fast_sf_seen seen;
        time_fast_sf_seen_init(&seen);
        (void)time_fast_sf_seen_mark(&seen, 1u);
        return time_fast_sf_seen_mark(&seen, 1u) == 0 ? 0 : 1;
    }
    return 1;
}

typedef struct { time_fast_cal_slot *cal; time_fast_loc_slot *loc; time_fast_sf_seen *seen; } lin_ctx_t;

static void *lin_worker(void *p) {
    lin_ctx_t *c = (lin_ctx_t *)p;
    lin_ct_t ct; lin_ai_t ai; char loc[0x24];
    for (int i = 0; i < 4000; i++) {
        if (time_fast_cal_read(c->cal, 1700000000, 1ull, &ct, &ai)) assert(ct.year >= 1970);
        if (time_fast_loc_read(c->loc, 1ull, loc)) assert(strcmp(loc, "UTC") == 0);
        (void)time_fast_sf_seen_mark(c->seen, (unsigned)(i & 1));
    }
    return NULL;
}

int main(void) {
    int f = time_fast_w25_fault();
    if (f > 0) {
        int rc = run_linux_fault(f);
        printf("t_time_fast_w25 (linux): fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    time_fast_ctx_slot cs = {0}; uint8_t in[0x20] = {3}, out[0x20] = {0};
    time_fast_ctx_write(&cs, 10, in);
    assert(time_fast_ctx_read(&cs, -1, 10, out) && memcmp(in, out, 0x20) == 0);
    time_fast_cal_slot cal = {0}; lin_ct_t ct, c2; lin_ai_t ai, a2;
    setenv("TZ", "UTC", 1); tzset();
    lin_slow_cal(1700000000, &ct, &ai);
    time_fast_cal_write(&cal, 1700000000, 1ull, &ct, &ai);
    assert(time_fast_cal_read(&cal, 1700000000, 1ull, &c2, &a2) && memcmp(&ct, &c2, sizeof ct) == 0);
    time_fast_loc_slot loc = {0}; char lbuf[0x24] = "UTC", lout[0x24] = {0};
    time_fast_loc_write(&loc, 1ull, lbuf);
    assert(time_fast_loc_read(&loc, 1ull, lout) && strcmp(lbuf, lout) == 0);
    time_fast_sf_seen seen;
    time_fast_sf_seen_init(&seen);
    assert(time_fast_sf_seen_mark(&seen, 0u) == 1 && time_fast_sf_seen_mark(&seen, 1u) == 1);
    assert(time_fast_sf_seen_mark(&seen, 0u) == 0 && time_fast_sf_seen_mark(&seen, 99u) == 1);
    assert(time_fast_sf_seen_mark(&seen, 99u) == 0);
    lin_ctx_t lc = {&cal, &loc, &seen}; pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, lin_worker, &lc) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    for (int mode = 0; mode < 2; mode++) {
        volatile uint64_t sink = 0;
        uint64_t t0 = lin_now_ns();
        for (int i = 0; i < 50000; i++) {
            if (!mode) { lin_slow_cal(1700000000, &c2, &a2); tzset(); snprintf(lout, sizeof lout, "%s", tzname[0] ? tzname[0] : "UTC"); }
            else { time_fast_cal_read(&cal, 1700000000, 1ull, &c2, &a2); time_fast_loc_read(&loc, 1ull, lout); }
            sink += (uint64_t)c2.year + (uint64_t)a2.offset + (uint64_t)lout[0];
        }
        printf("BENCH aoi_w25_linux_%s cal_loc=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(lin_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    printf("t_time_fast_w25 (linux): PASS\n");
    return 0;
}
#endif
