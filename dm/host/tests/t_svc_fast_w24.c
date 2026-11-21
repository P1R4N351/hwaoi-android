/* Wave 24 (Opt 26) host test, TSan stress, microbench, and fault injection for DM_SVC_FAST. */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef __APPLE__
#include "../dm.h"
#include "../hle_thread.c"
struct dm_flags g_flag;
uint64_t g_t0_ns;
module_t *g_mod;
int g_nmod;
__thread _Atomic int dm_host_work;
pthread_key_t g_key18;
void dm_audren_dump(void) {}
void crash_thread_init(void) {}
void guest_thread_init(void) {}
void guest_thread_set_id(uint64_t id) { (void)id; }
void guest_gate_stack(int own_stack) { (void)own_stack; }
void dm_thread_enter(uint64_t entry, uint64_t arg, uint64_t sp) {
    (void)entry; (void)arg; (void)sp; abort();
}
void *guest_sym(const char *name) { (void)name; return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
void dm_backtrace(uint64_t fp) { (void)fp; }
void guest_clock_read_wait(void) {}
uint64_t guest_ns(void) { return 0; }
uint64_t guest_ticks(void) { return 0; }
uint64_t dm_entropy64(void) { return 0; }
void hlecount_svc(int num) { (void)num; }
int dm_ue_heap_owns(uint64_t a, uint64_t n) { (void)a; (void)n; return 0; }
void dm_ue_heap_why(uint64_t a, uint64_t n, char *buf, size_t len) {
    (void)a; (void)n; snprintf(buf, len, "none");
}
void dm_guest_write_prepare(void *addr, size_t size) { (void)addr; (void)size; }
void gate_obs_set(int id, void (*fn)(uint64_t *, int)) { (void)id; (void)fn; }
int dm_detclock_enabled(void) { return 0; }
uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
extern uint64_t svc_dispatch(int num, uint64_t *regs, uint64_t orig_addr);
#else
#include "../svc_fast.h"
#endif

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#ifdef __APPLE__
static int qmem_eq(const svc_fast_qmem_info *a, const svc_fast_qmem_info *b) {
    return a->base == b->base && a->size == b->size && a->state == b->state &&
           a->attr == b->attr && a->perm == b->perm;
}

typedef struct { uint32_t hw, hr; _Atomic int done; int64_t wait_res; } wsync_arg;
static void *wsync_waiter(void *arg) {
    wsync_arg *a = arg;
    thr_t self = {0}; pthread_cond_init(&self.cv, NULL); pthread_cond_init(&self.pcv, NULL);
    self.prio = 44; self.core = 0; self.mask = 1;
    pthread_mutex_lock(&G);
    self.handle = h_new(O_THREAD, &self);
    self.id = 0x900;
    svc_fast_thr_pub(&g_thr_fast, &self.fast_meta, self.handle, self.prio, self.core, self.mask, self.id);
    pthread_mutex_unlock(&G);
    pthread_setspecific(g_keythr, &self);
    uint64_t r[32] = {0, (uint64_t)&a->hr, 1, 250000000ull};
    svc_thread(0x18, r);
    a->wait_res = (int64_t)r[0];
    atomic_store(&a->done, 1);
    pthread_mutex_lock(&G);
    g_h[self.handle - 0x1000].type = O_FREE; g_h[self.handle - 0x1000].obj = NULL;
    svc_fast_thr_drop(&g_thr_fast, self.handle);
    pthread_mutex_unlock(&G);
    pthread_cond_destroy(&self.cv); pthread_cond_destroy(&self.pcv);
    return NULL;
}

static void test_thr_and_event_parity(void) {
    svc_fast_reset_for_test(1);
    uint64_t r[32] = {0, 24};
    svc_dispatch(0x29, r, 0);
    assert(r[0] == 0 && r[1] == 95);
    uint64_t p_off[32] = {0, H_CUR_THREAD}, p_on[32] = {0, H_CUR_THREAD};
    svc_fast_reset_for_test(0); svc_thread(0x0C, p_off);
    svc_fast_reset_for_test(1); svc_thread(0x0C, p_on);
    assert(p_off[0] == 0 && p_on[0] == 0 && p_on[1] == p_off[1]);
    uint64_t s_set[32] = {0x1001, 41}; svc_thread(0x0D, s_set); assert(s_set[0] == 0);
    svc_fast_reset_for_test(0); p_off[1] = 0x1001; svc_thread(0x0C, p_off);
    svc_fast_reset_for_test(1); p_on[1] = 0x1001; svc_thread(0x0C, p_on);
    assert(p_off[1] == 41 && p_on[1] == 41);
    s_set[0] = 0x1001; s_set[1] = 44; svc_thread(0x0D, s_set);
    uint64_t c0[32] = {0, 0, H_CUR_THREAD}, c1[32] = {0, 0, H_CUR_THREAD};
    svc_fast_reset_for_test(0); svc_thread(0x0E, c0);
    svc_fast_reset_for_test(1); svc_thread(0x0E, c1);
    assert(c0[0] == 0 && c1[0] == 0 && c0[1] == c1[1] && c0[2] == c1[2]);
    uint64_t i0[32] = {0, 0x1001}, i1[32] = {0, 0x1001};
    svc_fast_reset_for_test(0); svc_thread(0x25, i0);
    svc_fast_reset_for_test(1); svc_thread(0x25, i1);
    assert(i0[0] == 0 && i1[0] == 0 && i0[1] == i1[1]);
    uint64_t bad[32] = {0, 0x1fff}; svc_thread(0x0C, bad); assert(bad[0] == R_BAD_HANDLE);
    uint64_t ev[32] = {0}; svc_thread(0x45, ev); assert(ev[0] == 0);
    uint32_t hw = (uint32_t)ev[1], hr = (uint32_t)ev[2];
    uint64_t er[32] = {hw}; svc_thread(0x11, er); assert(er[0] == 0);
    er[0] = hr; svc_thread(0x12, er); assert(er[0] == 0);
    er[0] = hr; svc_thread(0x17, er); assert(er[0] == R_BAD_STATE);
    er[0] = hw; svc_thread(0x11, er); assert(er[0] == 0);
    er[0] = hr; svc_thread(0x17, er); assert(er[0] == 0);
    wsync_arg wa = {.hw = hw, .hr = hr, .wait_res = -1};
    pthread_t th; assert(!pthread_create(&th, NULL, wsync_waiter, &wa));
    for (int s = 0; s < 100 && !atomic_load(&g_ev_fast.wsync_count); s++) usleep(1000);
    er[0] = hw; svc_thread(0x11, er); assert(er[0] == 0);
    pthread_join(th, NULL);
    assert(atomic_load(&wa.done) == 1 && wa.wait_res == 0);
    er[0] = hw; svc_thread(0x16, er); er[0] = hr; svc_thread(0x16, er);
}

static void test_qmem_and_getinfo_parity(void) {
    uint64_t ra[32] = {0, 4}; svc_dispatch(0x29, ra, 0);
    uintptr_t hbase = (uintptr_t)ra[1];
    uint64_t rs[32] = {0, 0x200000}; svc_dispatch(0x01, rs, 0); assert(rs[0] == 0);
    svc_fast_qmem_info m0 = {0}, m1 = {0};
    svc_fast_reset_for_test(0);
    uint64_t q0[32] = {(uint64_t)&m0, 0, hbase + 0x1000}; svc_dispatch(0x06, q0, 0);
    svc_fast_reset_for_test(1);
    uint64_t q1[32] = {(uint64_t)&m1, 0, hbase + 0x1000}; svc_dispatch(0x06, q1, 0);
    assert(qmem_eq(&m0, &m1) && m1.size == 0x200000 && m1.state == 5);
    uint64_t q2[32] = {(uint64_t)&m1, 0, hbase + 0x180000}; svc_dispatch(0x06, q2, 0);
    assert(m1.size == 0x200000 && m1.state == 5);
    rs[0] = 0; rs[1] = 0x400000; svc_dispatch(0x01, rs, 0); assert(rs[0] == 0);
    uint64_t q3[32] = {(uint64_t)&m1, 0, hbase + 0x180000}; svc_dispatch(0x06, q3, 0);
    assert(m1.size == 0x400000 && m1.state == 5);
    uint64_t gi0[32] = {0, 7}, gi1[32] = {0, 7};
    svc_fast_reset_for_test(0); svc_dispatch(0x29, gi0, 0);
    svc_fast_reset_for_test(1); svc_dispatch(0x29, gi1, 0);
    assert(gi0[0] == 0 && gi1[0] == 0 && gi0[1] == gi1[1]);
    rs[0] = 0; rs[1] = 0; svc_dispatch(0x01, rs, 0); assert(rs[0] == 0);
}

static _Atomic int g_st_go;
static _Atomic uint32_t g_st_hw, g_st_hr;
static void *stress_ev_worker(void *arg) {
    int role = *(int *)arg;
    for (int i = 0; i < 1000000 && !atomic_load(&g_st_go); i++) sched_yield();
    for (int i = 0; i < 400; i++) {
        uint32_t hw = atomic_load(&g_st_hw), hr = atomic_load(&g_st_hr);
        uint64_t r[32] = {role ? hw : hr};
        svc_thread(role ? 0x11 : 0x12, r);
        uint64_t q0[32] = {0, 0x1001}; svc_thread(0x0C, q0);
        uint64_t gi[32] = {0, 24}; svc_dispatch(0x29, gi, 0);
    }
    return NULL;
}

static void test_tsan_stress(void) {
    svc_fast_reset_for_test(1);
    uint64_t ev[32] = {0}; svc_thread(0x45, ev);
    atomic_store(&g_st_hw, (uint32_t)ev[1]); atomic_store(&g_st_hr, (uint32_t)ev[2]);
    atomic_store(&g_st_go, 0);
    pthread_t th[4]; int roles[4] = {0, 1, 0, 1};
    for (int i = 0; i < 4; i++) assert(!pthread_create(&th[i], NULL, stress_ev_worker, &roles[i]));
    atomic_store(&g_st_go, 1);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    uint64_t c[32] = {ev[1]}; svc_thread(0x16, c); c[0] = ev[2]; svc_thread(0x16, c);
}

static void run_benchmarks(void) {
    uint64_t ev[32] = {0}; svc_thread(0x45, ev);
    uint32_t hw = (uint32_t)ev[1], hr = (uint32_t)ev[2];
    uint64_t ra[32] = {0, 4}; svc_dispatch(0x29, ra, 0);
    uint64_t hbase = ra[1];
    uint64_t rs[32] = {0, 0x200000}; svc_dispatch(0x01, rs, 0);
    for (int mode = 0; mode <= 1; mode++) {
        svc_fast_reset_for_test(mode);
        uint64_t sink = 0, t0 = mono_ns();
        for (int i = 0; i < 30000; i++) {
            uint64_t r[32] = {0, H_CUR_THREAD}; svc_thread(0x0C, r); sink += r[1];
            uint64_t g[32] = {0, 24}; svc_dispatch(0x29, g, 0); sink += g[1];
        }
        double ms1 = (double)(mono_ns() - t0) / 1e6;
        t0 = mono_ns();
        for (int i = 0; i < 30000; i++) {
            uint64_t e1[32] = {hw}; svc_thread(0x11, e1);
            uint64_t e2[32] = {hr}; svc_thread(0x12, e2);
            sink += (uint64_t)e2[0];
        }
        double ms2 = (double)(mono_ns() - t0) / 1e6;
        svc_fast_qmem_info mi; t0 = mono_ns();
        for (int i = 0; i < 30000; i++) {
            uint64_t q[32] = {(uint64_t)&mi, 0, hbase + ((uint64_t)(i & 63) << 12)};
            svc_dispatch(0x06, q, 0);
            uint64_t g[32] = {0, 7}; svc_dispatch(0x29, g, 0); sink += g[1] + mi.size;
        }
        double ms3 = (double)(mono_ns() - t0) / 1e6;
        printf("BENCH aoi_w24_%s thr=%.3f ms ev=%.3f ms mem=%.3f ms sink=%llu\n",
            mode ? "on" : "off", ms1, ms2, ms3, (unsigned long long)sink);
    }
    rs[0] = 0; rs[1] = 0; svc_dispatch(0x01, rs, 0);
    uint64_t c[32] = {hw}; svc_thread(0x16, c); c[0] = hr; svc_thread(0x16, c);
}

static int run_fault_check(int f) {
    svc_fast_reset_for_test(1);
    if (f == 1) {
        uint64_t p0[32] = {0, 0x1001}; svc_thread(0x0C, p0);
        return p0[1] == 44 ? 0 : 1;
    } else if (f == 2) {
        return thr_limit_used() == 1 ? 0 : 1;
    } else if (f == 3) {
        uint64_t ev[32] = {0}; svc_thread(0x45, ev);
        uint64_t r[32] = {ev[1]}; svc_thread(0x11, r);
        r[0] = ev[2]; svc_thread(0x12, r);
        r[0] = ev[2]; svc_thread(0x17, r);
        uint64_t c[32] = {ev[1]}; svc_thread(0x16, c); c[0] = ev[2]; svc_thread(0x16, c);
        return r[0] == R_BAD_STATE ? 0 : 1;
    } else if (f == 4) {
        uint64_t ev[32] = {0}; svc_thread(0x45, ev);
        wsync_arg wa = {.hw = (uint32_t)ev[1], .hr = (uint32_t)ev[2], .wait_res = -1};
        pthread_t th; assert(!pthread_create(&th, NULL, wsync_waiter, &wa));
        for (int s = 0; s < 100 && !atomic_load(&g_ev_fast.wsync_count); s++) usleep(1000);
        uint64_t r[32] = {ev[1]}; svc_thread(0x11, r);
        pthread_join(th, NULL);
        uint64_t c[32] = {ev[1]}; svc_thread(0x16, c); c[0] = ev[2]; svc_thread(0x16, c);
        return wa.wait_res == 0 ? 0 : 1;
    } else if (f == 5) {
        uint64_t ra[32] = {0, 4}; svc_dispatch(0x29, ra, 0);
        uintptr_t hb = (uintptr_t)ra[1];
        uint64_t rs[32] = {0, 0x200000}; svc_dispatch(0x01, rs, 0);
        svc_fast_qmem_info mi = {0};
        uint64_t q[32] = {(uint64_t)&mi, 0, hb + 0x1000}; svc_dispatch(0x06, q, 0);
        rs[0] = 0; rs[1] = 0x400000; svc_dispatch(0x01, rs, 0);
        svc_dispatch(0x06, q, 0);
        int ok = mi.size == 0x400000;
        rs[0] = 0; rs[1] = 0; svc_dispatch(0x01, rs, 0);
        return ok ? 0 : 1;
    }
    return 1;
}

int main(void) {
    svc_thread_main_init();
    int f = svc_fast_w24_fault();
    if (f > 0) {
        int rc = run_fault_check(f);
        printf("t_svc_fast_w24: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    test_thr_and_event_parity();
    test_qmem_and_getinfo_parity();
    test_tsan_stress();
    run_benchmarks();
    printf("t_svc_fast_w24: PASS\n");
    return 0;
}
#else
static svc_fast_thr_table g_lin_thr;
static svc_fast_ev_sys g_lin_ev;
static _Atomic uint32_t g_lin_seq = 2;
static _Atomic int g_lin_stop;
static _Thread_local svc_fast_qmem_tls g_lin_tls;

static void *lin_stress_wr(void *arg) {
    (void)arg;
    for (int i = 1; i <= 500 && !atomic_load(&g_lin_stop); i++) {
        svc_fast_thr_pub(&g_lin_thr, NULL, 0x1001u, 30 + (i & 15), i & 3, 7ull, 0x51ull);
        int set = 0; int64_t res = -1;
        svc_fast_ev_signal_try(&g_lin_ev, 0x1002u, &set, &res);
        svc_fast_ev_clear(&g_lin_ev, 0x1003u, &res);
    }
    return NULL;
}

static int run_linux_fault(int f) {
    svc_fast_reset_for_test(1);
    svc_fast_thr_pub(&g_lin_thr, NULL, 0x1001u, 44, 0, 1ull, 0x51ull);
    svc_fast_thr_live_add(&g_lin_thr);
    svc_fast_ev *e = svc_fast_ev_alloc(&g_lin_ev, 2);
    svc_fast_ev_h_bind(&g_lin_ev, 0x1002u, SVC_FAST_EV_W, e);
    svc_fast_ev_h_bind(&g_lin_ev, 0x1003u, SVC_FAST_EV_R, e);
    int ok = 1;
    if (f == 1) {
        int32_t p = 0; svc_fast_thr_lookup(&g_lin_thr, 0x1001u, &p, NULL, NULL, NULL);
        ok = (p == 44);
    } else if (f == 2) {
        ok = (svc_fast_thr_live_get(&g_lin_thr) == 1);
    } else if (f == 3) {
        int set = 0; int64_t r = 0; svc_fast_ev_signal_try(&g_lin_ev, 0x1002u, &set, &r);
        svc_fast_ev_clear(&g_lin_ev, 0x1003u, &r);
        ok = !svc_fast_ev_is_sig(e);
    } else if (f == 4) {
        atomic_store(&g_lin_ev.wsync_count, 1u);
        int set = 0; int64_t r = 0;
        ok = (svc_fast_ev_signal_try(&g_lin_ev, 0x1002u, &set, &r) == 0 && set == 1);
        atomic_store(&g_lin_ev.wsync_count, 0u);
    } else if (f == 5) {
        svc_fast_qmem_info in = {0x100000, 0x2000, 5, 0, 3, 0, 0, 0}, out = {0};
        svc_fast_qmem_store(&g_lin_tls, 2u, &in);
        ok = (svc_fast_qmem_lookup(&g_lin_tls, 0x100500, 4u, &out) == 0);
    }
    svc_fast_ev_h_unbind(&g_lin_ev, 0x1002u); svc_fast_ev_h_unbind(&g_lin_ev, 0x1003u);
    svc_fast_ev_free(&g_lin_ev, e);
    svc_fast_thr_live_sub(&g_lin_thr);
    return ok ? 0 : 1;
}

int main(void) {
    int f = svc_fast_w24_fault();
    if (f > 0) {
        int rc = run_linux_fault(f);
        printf("t_svc_fast_w24 (linux): fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    svc_fast_thr_pub(&g_lin_thr, NULL, 0x1001u, 44, 0, 1ull, 0x51ull);
    svc_fast_thr_live_add(&g_lin_thr);
    svc_fast_ev *e = svc_fast_ev_alloc(&g_lin_ev, 2);
    svc_fast_ev_h_bind(&g_lin_ev, 0x1002u, SVC_FAST_EV_W, e);
    svc_fast_ev_h_bind(&g_lin_ev, 0x1003u, SVC_FAST_EV_R, e);
    pthread_t wr; atomic_store(&g_lin_stop, 0);
    assert(!pthread_create(&wr, NULL, lin_stress_wr, NULL));
    for (int i = 0; i < 600; i++) {
        int32_t p = 0, c = 0; uint64_t m = 0, tid = 0;
        if (svc_fast_thr_lookup(&g_lin_thr, 0x1001u, &p, &c, &m, &tid)) assert(tid == 0x51ull);
        (void)svc_fast_thr_live_get(&g_lin_thr);
        int64_t r = 0; (void)svc_fast_ev_reset(&g_lin_ev, 0x1003u, 0xFA01u, &r);
    }
    pthread_join(wr, NULL);
    svc_fast_qmem_info in = {0x200000000ULL, 0x200000ULL, 5, 0, 3, 0, 0, 0}, out = {0};
    svc_fast_qmem_store(&g_lin_tls, 2u, &in);
    assert(svc_fast_qmem_lookup(&g_lin_tls, 0x200010000ULL, 2u, &out) == 1 && out.size == 0x200000ULL);
    svc_fast_seq_begin(&g_lin_seq); svc_fast_seq_end(&g_lin_seq);
    assert(svc_fast_qmem_lookup(&g_lin_tls, 0x200010000ULL, atomic_load(&g_lin_seq), &out) == 0);
    for (int mode = 0; mode <= 1; mode++) {
        svc_fast_reset_for_test(mode);
        pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        uint64_t sink = 0, t0 = mono_ns();
        for (int i = 0; i < 60000; i++) {
            if (!svc_fast_enabled()) pthread_mutex_lock(&mu);
            int32_t p = 0; svc_fast_thr_lookup(&g_lin_thr, 0x1001u, &p, NULL, NULL, NULL);
            int set = 0; int64_t r = 0; svc_fast_ev_signal_try(&g_lin_ev, 0x1002u, &set, &r);
            svc_fast_ev_clear(&g_lin_ev, 0x1003u, &r);
            if (!svc_fast_enabled()) pthread_mutex_unlock(&mu);
            sink += (uint64_t)p;
        }
        printf("BENCH aoi_w24_linux_%s thr_ev=%.3f ms sink=%llu\n",
            mode ? "on" : "off", (double)(mono_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    svc_fast_ev_h_unbind(&g_lin_ev, 0x1002u); svc_fast_ev_h_unbind(&g_lin_ev, 0x1003u);
    svc_fast_ev_free(&g_lin_ev, e); svc_fast_thr_live_sub(&g_lin_thr);
    printf("t_svc_fast_w24 (linux): PASS\n");
    return 0;
}
#endif
