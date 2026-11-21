/* Wave 28 (Opt 30) unit, TSan stress, microbench, and fault-injection tests for DM_OE_FAST. */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W28_OE_TEST_HOOKS 1
#include "../oe_fast.h"

static uint64_t w28_now_ns(void) {
    struct timespec t;
    memset(&t, 0, sizeof t);
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

typedef struct {
    pthread_mutex_t mu;
    uint32_t ring[32];
    int head;
    _Atomic int n;
    _Atomic int ev_on;
} w28_mq_t;

static void w28_mq_init(w28_mq_t *q) {
    pthread_mutex_init(&q->mu, NULL);
    memset(q->ring, 0, sizeof q->ring);
    q->head = 0;
    atomic_store_explicit(&q->n, 0, memory_order_release);
    atomic_store_explicit(&q->ev_on, 0, memory_order_release);
}

static void w28_mq_push(w28_mq_t *q, uint32_t msg) {
    pthread_mutex_lock(&q->mu);
    int cur = atomic_load_explicit(&q->n, memory_order_relaxed);
    if (cur < 32) {
        q->ring[(q->head + cur) & 31] = msg;
        atomic_store_explicit(&q->n, cur + 1, memory_order_release);
    }
    pthread_mutex_unlock(&q->mu);
}

static unsigned w28_try_pop(w28_mq_t *q, uint32_t *msg_out) {
    unsigned fast_ret = 0;
    if (oe_fast_try_pop_empty(&q->n, &q->ev_on, msg_out, &fast_ret)) return fast_ret;
    pthread_mutex_lock(&q->mu);
    int cur = atomic_load_explicit(&q->n, memory_order_relaxed);
    int ok = cur > 0;
    if (ok) {
        if (msg_out) *msg_out = q->ring[q->head];
        q->head = (q->head + 1) & 31;
        atomic_store_explicit(&q->n, cur - 1, memory_order_release);
    }
    pthread_mutex_unlock(&q->mu);
    return (unsigned)ok;
}

static uint64_t w28_slow_lang(const char *tag, int *reg_out) {
    char code[8] = {0};
    const char *mapped = "en-US";
    int r = 1;
    if (tag && !strcmp(tag, "fr-FR")) { mapped = "fr"; r = 2; }
    else if (tag && !strcmp(tag, "es-MX")) { mapped = "es-419"; r = 1; }
    else if (tag && !strcmp(tag, "zh-Hant-HK")) { mapped = "zh-Hant"; r = 4; }
    else if (tag && !strcmp(tag, "ja")) { mapped = "ja"; r = 0; }
    size_t ml = strnlen(mapped, 7u); memcpy(code, mapped, ml);
    uint64_t u = 0;
    memcpy(&u, code, 8);
    *reg_out = oe_fast_region_of(code);
    assert(*reg_out == r);
    return u;
}

static uint64_t w28_fast_lang(oe_fast_lang_slot *s, pthread_mutex_t *mu, const char *tag, int *reg_out) {
    uint64_t u = 0;
    int r = 0;
    if (oe_fast_lang_get(s, tag, &u, &r)) {
        *reg_out = r;
        return u;
    }
    u = w28_slow_lang(tag, &r);
    pthread_mutex_lock(mu);
    oe_fast_lang_put(s, tag, u, r);
    pthread_mutex_unlock(mu);
    *reg_out = r;
    return u;
}

static int run_fault(int f) {
    oe_fast_reset_for_test(1);
    w28_mq_t q;
    w28_mq_init(&q);
    if (f == 1) {
        uint32_t m = 999u;
        unsigned got = w28_try_pop(&q, &m);
        return (got == 0u && m == 999u) ? 0 : 1;
    }
    if (f == 2) {
        w28_mq_push(&q, 15u);
        uint32_t m = 0u;
        unsigned got = w28_try_pop(&q, &m);
        return (got == 1u && m == 15u) ? 0 : 1;
    }
    if (f == 3) {
        int w = 0, h = 0;
        oe_fast_def_res(1, &w, &h);
        return (w == 1920 && h == 1080) ? 0 : 1;
    }
    if (f == 4) {
        static oe_fast_lang_slot s;
        static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        memset(&s, 0, sizeof s);
        int r1 = 0, r2 = 0;
        uint64_t c1 = w28_fast_lang(&s, &mu, "fr-FR", &r1);
        uint64_t c2 = w28_fast_lang(&s, &mu, "es-MX", &r2);
        return (c1 != c2 && r1 == 2 && r2 == 1) ? 0 : 1;
    }
    if (f == 5) {
        char us[8] = "en-US";
        char fr[8] = "fr";
        return (oe_fast_region_of(us) == 1 && oe_fast_region_of(fr) == 2) ? 0 : 1;
    }
    return 1;
}

typedef struct {
    w28_mq_t *q;
    oe_fast_lang_slot *s;
    pthread_mutex_t *mu;
    _Atomic int *opmode;
} w28_stress_t;

static void *w28_worker(void *arg) {
    w28_stress_t *c = (w28_stress_t *)arg;
    static const char *const tags[4] = {"fr-FR", "es-MX", "zh-Hant-HK", NULL};
    for (int i = 0; i < 8000; i++) {
        uint32_t m = 777u;
        if ((i & 31) == 0) w28_mq_push(c->q, 30u);
        unsigned ok = w28_try_pop(c->q, &m);
        if (!ok) assert(m == 777u);
        int w = 0, h = 0;
        oe_fast_def_res(oe_fast_load_int(c->opmode), &w, &h);
        assert((w == 1920 && h == 1080) || (w == 1280 && h == 720));
        int reg = 0;
        uint64_t u = w28_fast_lang(c->s, c->mu, tags[i & 3], &reg);
        assert(u != 0u && reg >= 0 && reg <= 4);
    }
    return NULL;
}

static void run_parity_checks(void) {
    w28_mq_t q;
    w28_mq_init(&q);
    static oe_fast_lang_slot s;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    for (int mode = 0; mode < 2; mode++) {
        oe_fast_reset_for_test(mode);
        memset(&s, 0, sizeof s);
        uint32_t m = 999u;
        assert(w28_try_pop(&q, &m) == 0u && m == 999u);
        w28_mq_push(&q, 15u);
        w28_mq_push(&q, 30u);
        assert(w28_try_pop(&q, &m) == 1u && m == 15u);
        atomic_store_explicit(&q.ev_on, 1, memory_order_release);
        assert(w28_try_pop(&q, &m) == 1u && m == 30u);
        m = 888u;
        assert(w28_try_pop(&q, &m) == 0u && m == 888u);
        atomic_store_explicit(&q.ev_on, 0, memory_order_release);
        int w = 0, h = 0;
        oe_fast_def_res(0, &w, &h);
        assert(w == 1280 && h == 720);
        oe_fast_def_res(1, &w, &h);
        assert(w == 1920 && h == 1080);
        int reg = 0;
        (void)w28_fast_lang(&s, &mu, "fr-FR", &reg);
        assert(w28_fast_lang(&s, &mu, "fr-FR", &reg) != 0u && reg == 2);
        (void)w28_fast_lang(&s, &mu, "1234567890123456789012345678901", &reg);
        assert(reg == 1);
    }
}

int main(void) {
    int f = oe_w28_fault();
    if (f > 0) {
        int rc = run_fault(f);
        printf("t_oe_fast_w28: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    run_parity_checks();

    oe_fast_reset_for_test(1);
    w28_mq_t q;
    w28_mq_init(&q);
    static oe_fast_lang_slot s;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    memset(&s, 0, sizeof s);
    _Atomic int opm = 1;
    w28_stress_t ctx = {.q = &q, .s = &s, .mu = &mu, .opmode = &opm};
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w28_worker, &ctx) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);
    uint32_t drain = 0u;
    while (w28_try_pop(&q, &drain)) {}

    static const char *volatile bench_tags[4] = {"fr-FR", "fr-FR", "fr-FR", "fr-FR"};
    memset(&s, 0, sizeof s);
    for (int mode = 0; mode < 2; mode++) {
        oe_fast_reset_for_test(mode);
        volatile uint64_t sink = 0;
        uint64_t t0 = w28_now_ns();
        for (int i = 0; i < 400000; i++) {
            uint32_t m = 0u;
            unsigned ok = w28_try_pop(&q, &m);
            int w = 0, h = 0, reg = 0;
            oe_fast_def_res(oe_fast_load_int(&opm), &w, &h);
            uint64_t u = w28_fast_lang(&s, &mu, bench_tags[i & 3], &reg);
            sink += (uint64_t)ok + (uint64_t)w + (uint64_t)h + (uint64_t)reg + (u & 0xffu);
        }
        printf("BENCH aoi_w28_%s oe_poll=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(w28_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    return 0;
}
