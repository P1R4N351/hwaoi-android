#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define T_W26_TEST_HOOKS 1
#include "../gate_fast.h"

static uint64_t w26_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

typedef struct {
    uintptr_t b, e, pb, pe;
    char name[16];
} fake_mod_t;

static fake_mod_t g_fm[6];

static fake_mod_t *slow_mod_of(const void *p) {
    uintptr_t a = (uintptr_t)p;
    for (int i = 0; i < 6; i++) {
        if (gate_fast_mod_has(a, g_fm[i].b, g_fm[i].e, g_fm[i].pb, g_fm[i].pe))
            return &g_fm[i];
    }
    return NULL;
}

static fake_mod_t *fast_mod_of(gate_fast_mod_idx *idx, const void *p) {
    if (!p) return NULL;
    void *hit = NULL;
    if (gate_fast_active() && gate_fast_mod_try(idx, p, &hit))
        return (fake_mod_t *)hit;
    fake_mod_t *r = slow_mod_of(p);
    if (r && gate_fast_active())
        gate_fast_mod_write(idx, p, r, r->b, r->e, r->pb, r->pe);
    return r;
}

static void init_fake_mods(gate_fast_mod_idx *idx) {
    memset(idx, 0, sizeof *idx);
    uintptr_t base = 0x100000000ull;
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    for (int i = 0; i < 6; i++) {
        snprintf(g_fm[i].name, sizeof g_fm[i].name, "mod%d", i);
        g_fm[i].b = base + (uintptr_t)i * 0x200000ull;
        g_fm[i].e = g_fm[i].b + (i == 5 ? 0x108000ull : 0x120000ull);
        if (i == 2) {
            g_fm[i].pb = g_fm[i].b - 0x40000ull;
            g_fm[i].pe = g_fm[i].b - 0x10000ull;
        } else {
            g_fm[i].pb = 0;
            g_fm[i].pe = 0;
        }
        uintptr_t ml = g_fm[i].pe > g_fm[i].pb && g_fm[i].pb < g_fm[i].b ? g_fm[i].pb : g_fm[i].b;
        if (ml < lo) lo = ml;
        if (g_fm[i].e > hi) hi = g_fm[i].e;
    }
    gate_fast_mod_publish_span(idx, lo, hi);
}

static int run_fault(int f) {
    gate_fast_reset_for_test(1);
    if (f == 1 || f == 2) {
        gate_fast_om_cache oc = {0};
        int val = 42;
        uint64_t g = atomic_load_explicit(&oc.gen, memory_order_relaxed);
        gate_fast_om_put(&oc, (const void *)0x2000u, &val, g);
        if (f == 2) {
            (void)gate_fast_om_invalidate(&oc, (const void *)0x2000u);
            gate_fast_om_put(&oc, (const void *)0x2000u, &val, g);
        }
        void *out = NULL;
        int ok = gate_fast_om_get(&oc, (const void *)0x2000u, &out);
        if (f == 1) return (ok && out == &val) ? 0 : 1;
        return ok == 0 ? 0 : 1;
    }
    if (f == 3) {
        gate_fast_ounce_slot oz = {0};
        gate_fast_ounce_write(&oz, 3, 1);
        int out = 0;
        return (gate_fast_ounce_read(&oz, 3, &out) && out == 1) ? 0 : 1;
    }
    gate_fast_mod_idx idx;
    init_fake_mods(&idx);
    if (f == 4) {
        fake_mod_t *m = fast_mod_of(&idx, (const void *)(g_fm[5].b + 0x1000u));
        return m == &g_fm[5] ? 0 : 1;
    }
    if (f == 5) {
        const void *p = (const void *)(g_fm[3].b + 0x2000u);
        assert(fast_mod_of(&idx, p) == &g_fm[3]);
        return fast_mod_of(&idx, p) == &g_fm[3] ? 0 : 1;
    }
    return 1;
}

typedef struct {
    gate_fast_om_cache *oc;
    gate_fast_mod_idx *idx;
    int vals[4];
} stress_ctx_t;

static void *worker_fn(void *p) {
    stress_ctx_t *c = (stress_ctx_t *)p;
    for (int i = 0; i < 4000; i++) {
        void *out = NULL;
        const void *k = (const void *)(uintptr_t)(0x4000000000ull + (unsigned)(i & 3) * 0x1000u);
        if (gate_fast_om_get(c->oc, k, &out))
            assert(out == &c->vals[i & 3]);
        const void *pc = (const void *)(g_fm[i % 6].b + 0x4000u);
        assert(fast_mod_of(c->idx, pc) == &g_fm[i % 6]);
        assert(fast_mod_of(c->idx, (const void *)(uintptr_t)0x7fff12340000ull) == NULL);
    }
    return NULL;
}

int main(void) {
    int f = gate_fast_w26_fault();
    if (f > 0) {
        int rc = run_fault(f);
        printf("t_gate_fast_w26: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    gate_fast_mod_idx idx;
    init_fake_mods(&idx);
    for (int mode = 0; mode < 2; mode++) {
        gate_fast_reset_for_test(mode);
        for (int i = 0; i < 6; i++) {
            const void *p = (const void *)(g_fm[i].b + 0x1234u);
            assert(fast_mod_of(&idx, p) == &g_fm[i]);
            assert(fast_mod_of(&idx, p) == &g_fm[i]);
            assert(fast_mod_of(&idx, (const void *)(g_fm[i].e - 4u)) == &g_fm[i]);
            assert(fast_mod_of(&idx, (const void *)g_fm[i].e) == NULL);
        }
        assert(fast_mod_of(&idx, (const void *)(g_fm[2].pb + 0x100u)) == &g_fm[2]);
        assert(fast_mod_of(&idx, (const void *)(g_fm[2].pe + 0x100u)) == NULL);
        assert(fast_mod_of(&idx, (const void *)(uintptr_t)0x7fff00010000ull) == NULL);
    }

    gate_fast_reset_for_test(1);
    gate_fast_om_cache oc = {0};
    stress_ctx_t ctx = {.oc = &oc, .idx = &idx, .vals = {10, 20, 30, 40}};
    uint64_t g0 = atomic_load_explicit(&oc.gen, memory_order_relaxed);
    for (int i = 0; i < 4; i++)
        gate_fast_om_put(&oc, (const void *)(uintptr_t)(0x4000000000ull + (unsigned)i * 0x1000u), &ctx.vals[i], g0);
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, worker_fn, &ctx) == 0);
    for (int i = 0; i < 4; i++) assert(pthread_join(th[i], NULL) == 0);

    (void)gate_fast_om_invalidate(&oc, (const void *)(uintptr_t)0x4000001000ull);
    void *miss = NULL;
    assert(!gate_fast_om_get(&oc, (const void *)(uintptr_t)0x4000001000ull, &miss));

    gate_fast_ounce_slot oz = {0};
    int oz_v = -1;
    assert(!gate_fast_ounce_read(&oz, 0, &oz_v));
    gate_fast_ounce_write(&oz, 3, 1);
    assert(gate_fast_ounce_read(&oz, 3, &oz_v) && oz_v == 1);
    assert(!gate_fast_ounce_read(&oz, 4, &oz_v));

    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    for (int mode = 0; mode < 2; mode++) {
        volatile uint64_t sink = 0;
        uint64_t t0 = w26_now_ns();
        for (int i = 0; i < 200000; i++) {
            const void *p = (i & 1) ? (const void *)(uintptr_t)0x7fff00010000ull : (const void *)(g_fm[i % 6].b + 0x1000u);
            fake_mod_t *m = NULL;
            void *ov = NULL;
            if (!mode) {
                pthread_mutex_lock(&mu);
                ov = &ctx.vals[i & 3];
                pthread_mutex_unlock(&mu);
                m = slow_mod_of(p);
            } else {
                const void *k = (const void *)(uintptr_t)(0x4000000000ull + (unsigned)(i & 3) * 0x1000u);
                if (!gate_fast_om_get(&oc, k, &ov)) {
                    ov = &ctx.vals[i & 3];
                    gate_fast_om_put(&oc, k, ov, atomic_load_explicit(&oc.gen, memory_order_relaxed));
                }
                m = fast_mod_of(&idx, p);
            }
            sink += (uint64_t)(uintptr_t)ov + (m ? (uint64_t)m->name[3] : 0ull);
        }
        printf("BENCH aoi_w26_%s om_mod=%.3f ms sink=%llu\n", mode ? "on" : "off",
               (double)(w26_now_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    printf("t_gate_fast_w26: PASS\n");
    return 0;
}
