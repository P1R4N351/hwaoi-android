/* Wave 23 (Opt 25) host test, TSan stress, microbench, and fault injection for DM_HID_FAST. */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef __APPLE__
#include "../hle_hid.c"
#include "../pad_slots.h"
#else
#include "../motion_fusion.h"
#define HID_FAST_MOTION 1
#include "../hid_fast.h"
#endif
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>

extern void dm_haptics_fast_reset_for_test(int v);
static _Atomic uint64_t g_test_now = 2000000000ull;

static _Atomic int g_sink_calls;
static void test_sink(dm_haptic_state st) { (void)st; atomic_fetch_add(&g_sink_calls, 1); }

static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#ifdef __APPLE__
static _Atomic uint64_t g_test_guest = 2000000000ull;
static _Atomic uint64_t g_test_frame;
int dm_opmode(void) { return 1; }
uint64_t now_ns(void) { return atomic_load_explicit(&g_test_now, memory_order_relaxed); }
uint64_t guest_ns(void) { return atomic_load_explicit(&g_test_guest, memory_order_relaxed); }
uint64_t dm_guest_frame(void) { return atomic_load_explicit(&g_test_frame, memory_order_relaxed); }
void (*_Atomic dm_touch_observer)(const dm_touch *t, int n);
void dm_touch_set(const dm_touch *t, int n) { (void)t; (void)n; }
void dm_touch_script(const dm_touch *t, int n) { (void)t; (void)n; }
int touch_fill_n(void *out, int max, uint64_t now) { (void)out; (void)max; (void)now; return 0; }
int gesture_fill(void *out, int n, uint64_t now) { (void)out; (void)n; (void)now; return 0; }
void dm_gfx_shot_at(long frame, const char *path) { (void)frame; (void)path; }
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }
void hle_signal_system_event(void *g) { (void)g; }
int hle_signal_bound_system_event(void *g) { (void)g; return 1; }
void dm_gfx_texlist(int target) { (void)target; }
void dm_gfx_texraw(uint64_t serial, const char *path) { (void)serial; (void)path; }
void dm_ctl_peek(uint64_t a, int n) { (void)a; (void)n; }
void crash_dump_now(const char *why) { (void)why; }
void dm_ctl_memdump(uint64_t a, uint64_t n, const char *path) { (void)a; (void)n; (void)path; }
void dm_ctl_cvar_i32(uint64_t off, int32_t v) { (void)off; (void)v; }
void dm_fps_target_set(int t, const char *src) { (void)t; (void)src; }
void dm_ctl_cvar_f32(uint64_t off, float v) { (void)off; (void)v; }
void dm_gfx_texdump_next(const char *path) { (void)path; }
void dm_gfx_passdump_next(long n) { (void)n; }
void dm_applet_enter(const char *name) { (void)name; }
void dm_applet_leave(void) {}

static void set_both_fast(int v) {
    dm_hid_fast_reset_for_test(v);
    dm_haptics_fast_reset_for_test(v);
}

static void reset_motion_state(void) {
    for (int p = 0; p < DM_PLAYERS; p++) { dm_motion_clear(p); dm_motion_clear_hid(p); }
    pthread_mutex_lock(&g_phone_motion.mu);
    atomic_store(&g_phone_motion.last_ns, 0);
    hid_fast_motion_clear_slot(&g_motion_fast[16]);
    atomic_fetch_and(&g_motion_active_mask, ~(1u << 16));
    pthread_mutex_unlock(&g_phone_motion.mu);
    atomic_store(&g_shake_t0, 0); atomic_store(&g_shake_end, 0);
}

static int six_states_eq(const SixAxisState *a, const SixAxisState *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i].delta_ns != b[i].delta_ns || a[i].sampling != b[i].sampling ||
            a[i].attributes != b[i].attributes) return 0;
        for (int j = 0; j < 3; j++)
            if (a[i].acc[j] != b[i].acc[j] || a[i].gyro[j] != b[i].gyro[j] ||
                a[i].angle[j] != b[i].angle[j]) return 0;
        for (int j = 0; j < 9; j++)
            if (a[i].dir[j] != b[i].dir[j]) return 0;
    }
    return 1;
}

static int imu_samples_eq(const NpadImuSample *a, const NpadImuSample *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i].sampling != b[i].sampling || a[i].delta_ns != b[i].delta_ns) return 0;
        for (int j = 0; j < 3; j++)
            if (a[i].acc[j] != b[i].acc[j] || a[i].gyro[j] != b[i].gyro[j]) return 0;
    }
    return 1;
}

static int test_motion_and_six_parity(void) {
    reset_motion_state();
    dm_pad_players(1); h_hid_SetIds(&(uint32_t){0}, 1); h_hid_SetStyles(STYLE_FULLKEY | STYLE_HANDHELD); g_phone_mode = -1;
    uint32_t h = 0; SixAxisState off_st[16], on_st[16];
    float acc[3] = {0.4f, -0.2f, -0.89f}, rot[3] = {0.1f, -0.2f, 1.5f};
    set_both_fast(0);
    assert(h_hid_SixStates(off_st, 16, &h) == 16 && h_hid_SixRest(&h));
    set_both_fast(1);
    assert(h_hid_SixStates(on_st, 16, &h) == 16 && six_states_eq(off_st, on_st, 16));
    dm_motion_set(0, acc, rot);
    set_both_fast(0); h_hid_SixStates(off_st, 16, &h);
    set_both_fast(1); h_hid_SixStates(on_st, 16, &h);
    assert(six_states_eq(off_st, on_st, 16));
    dm_motion_clear(0);
    dm_motion_set_switch(0, (float[]){0, -1, 0}, (float[]){0, 0, 0.25f});
    set_both_fast(0); h_hid_SixStates(off_st, 16, &h);
    set_both_fast(1); h_hid_SixStates(on_st, 16, &h);
    assert(six_states_eq(off_st, on_st, 16));
    atomic_fetch_add(&g_test_now, MOTION_STALE_NS + 100);
    set_both_fast(0); h_hid_SixStates(off_st, 16, &h);
    set_both_fast(1); h_hid_SixStates(on_st, 16, &h);
    assert(six_states_eq(off_st, on_st, 16));
    assert(on_st[0].acc[2] == -1.0f && on_st[0].acc[1] == 0.0f);
    dm_motion_set_switch(0, (float[]){0, -1, 0}, (float[]){0, 0, 0});
    g_phone_motion.last_ns = 0;
    assert(h_hid_SixStates(on_st, 16, &h) == 16 && on_st[0].acc[2] == -1.0f);
    uint64_t t0 = 4000000000ull;
    atomic_store(&g_shake_t0, t0); atomic_store(&g_shake_end, t0 + 160000000ull);
    atomic_store(&g_test_guest, t0 + 20000000ull);
    set_both_fast(0); h_hid_SixStates(off_st, 16, &h);
    set_both_fast(1); h_hid_SixStates(on_st, 16, &h);
    assert(six_states_eq(off_st, on_st, 16));
    assert(fabsf(on_st[0].acc[0] - on_st[1].acc[0]) > 1e-4f);
    return 0;
}

static int test_imu_and_haptics_parity(void) {
    reset_motion_state();
    NpadImuSample imu_off[16], imu_on[16];
    uint64_t buf[16]; uint32_t h = 0;
    assert(!h_hid_ImuIs());
    assert(h_hid_ImuEnable(buf, sizeof buf) == 0 && h_hid_ImuIs());
    assert(h_hid_ImuEnable(buf, sizeof buf) == RES_NPAD_IMU_ENABLED);
    set_both_fast(0); int n0 = h_hid_ImuSamples(imu_off, 16, h);
    set_both_fast(1); int n1 = h_hid_ImuSamples(imu_on, 16, h);
    assert(n0 == 16 && n1 == 16 && imu_samples_eq(imu_off, imu_on, 16));
    assert(h_hid_ImuDisable() == 0 && !h_hid_ImuIs());
    dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1); atomic_store(&g_sink_calls, 0);
    dm_vibration_update batch[2] = {{0, 0, {0.3f, 160, 0, 320}}, {0, 1, {0.5f, 160, 0, 320}}};
    dm_haptics_send(batch, 2);
    assert(atomic_load(&g_sink_calls) == 1);
    dm_haptics_send(batch, 2);
    assert(atomic_load(&g_sink_calls) == 1 && dm_haptics_actual(0, 1).amplitude_low == 0.5f);
    dm_vibration_update toggle[2] = {{0, 1, {0.9f, 160, 0, 320}}, {0, 1, {0.5f, 160, 0, 320}}};
    dm_haptics_send(toggle, 2);
    assert(atomic_load(&g_sink_calls) == 2);
    dm_haptics_suspend(DM_HAPTICS_INACTIVE, 1);
    assert(dm_haptics_enabled() == 1 && dm_haptics_available() == 1u);
    assert(dm_haptics_actual(0, 0).amplitude_low == 0.0f);
    dm_haptics_suspend(DM_HAPTICS_INACTIVE, 0);
    return 0;
}

static _Atomic int g_stress_go, g_stress_stop;
static void *stress_writer(void *arg) {
    (void)arg;
    for (int sp = 0; sp < 1000000 && !atomic_load(&g_stress_go); sp++) sched_yield();
    for (int i = 0; i < 600 && !atomic_load(&g_stress_stop); i++) {
        dm_motion_set(0, (float[]){0.1f, 0, -1.0f}, (float[]){0, 0, 0.2f});
        dm_vibration_update u = {0, i & 1, {(i % 3) * 0.2f, 160, 0, 320}};
        dm_haptics_send(&u, 1);
        if ((i % 40) == 0) dm_motion_clear(0);
    }
    return NULL;
}

static void *stress_reader(void *arg) {
    (void)arg; SixAxisState st[16]; NpadImuSample imu[16]; uint32_t h = 0;
    for (int sp = 0; sp < 1000000 && !atomic_load(&g_stress_go); sp++) sched_yield();
    for (int i = 0; i < 600 && !atomic_load(&g_stress_stop); i++) {
        h_hid_SixStates(st, 16, &h);
        (void)h_hid_ImuIs();
        h_hid_ImuSamples(imu, 16, h);
        (void)dm_haptics_available();
        (void)dm_haptics_actual(0, i & 1);
    }
    return NULL;
}

static int test_tsan_stress(void) {
    reset_motion_state();
    uint64_t buf[16]; h_hid_ImuEnable(buf, sizeof buf);
    dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
    atomic_store(&g_stress_go, 0); atomic_store(&g_stress_stop, 0);
    pthread_t wr, rd[3];
    assert(!pthread_create(&wr, NULL, stress_writer, NULL));
    for (int i = 0; i < 3; i++) assert(!pthread_create(&rd[i], NULL, stress_reader, NULL));
    atomic_store(&g_stress_go, 1);
    pthread_join(wr, NULL);
    for (int i = 0; i < 3; i++) pthread_join(rd[i], NULL);
    h_hid_ImuDisable(); reset_motion_state();
    return 0;
}

static void run_benchmarks(void) {
    reset_motion_state();
    uint64_t buf[16]; h_hid_ImuEnable(buf, sizeof buf);
    dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
    const uint32_t vh[2] = {3, 0x10003};
    const dm_vibration vv[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
    uint32_t sh = 0; SixAxisState st[16]; NpadImuSample imu[16];
    for (int mode = 0; mode <= 1; mode++) {
        set_both_fast(mode);
        uint64_t sink = 0, t0 = mono_ns();
        for (int i = 0; i < 20000; i++) {
            sink += (uint64_t)h_hid_SixStates(st, 16, &sh);
            sink += (uint64_t)st[15].sampling;
            vib_send(vh, vv, 2);
            sink += h_hid_VibPermit();
        }
        double ms1 = (double)(mono_ns() - t0) / 1e6;
        dm_motion_set(0, (float[]){0.2f, 0, -0.98f}, (float[]){0, 0, 0.3f});
        t0 = mono_ns();
        for (int i = 0; i < 10000; i++) {
            sink += (uint64_t)h_hid_ImuSamples(imu, 16, sh);
            sink += (uint64_t)imu[0].sampling;
        }
        double ms2 = (double)(mono_ns() - t0) / 1e6;
        dm_motion_clear(0);
        printf("BENCH aoi_w23_%s idle_six_vib=%.3f ms live_motion_imu=%.3f ms sink=%llu\n",
            mode ? "on" : "off", ms1, ms2, (unsigned long long)sink);
    }
    h_hid_ImuDisable(); reset_motion_state();
}

static int run_fault_check(int f) {
    dm_pad_players(1); h_hid_SetIds(&(uint32_t){0}, 1); h_hid_SetStyles(STYLE_FULLKEY | STYLE_HANDHELD);
    set_both_fast(1);
    int ok = 1;
    if (f == 1) {
        reset_motion_state();
        dm_motion_set(0, (float[]){0.5f, 0, -0.8f}, (float[]){0, 0, 0});
        atomic_fetch_add(&g_test_now, MOTION_STALE_NS + 100);
        uint32_t h = 0; SixAxisState st[1]; h_hid_SixStates(st, 1, &h);
        ok = !(atomic_load(&g_motion_active_mask) & 1u);
    } else if (f == 2) {
        dm_motion_set(0, (float[]){0.5f, 0, -0.8f}, (float[]){0, 0, 0});
        motion_state out; uint64_t ts = 0;
        hid_fast_motion_read(&g_motion_fast[0], now_ns(), MOTION_STALE_NS, &out, &ts);
        ok = fabsf(out.R[0][0] - g_motion[0].st.R[0][0]) <= 0.1f;
    } else if (f == 3) {
        reset_motion_state();
        uint64_t t0 = 4000000000ull;
        atomic_store(&g_shake_t0, t0); atomic_store(&g_shake_end, t0 + 160000000ull);
        atomic_store(&g_test_guest, t0 + 20000000ull);
        uint32_t h = 0; SixAxisState st[16]; h_hid_SixStates(st, 16, &h);
        ok = fabsf(st[0].acc[0] - st[1].acc[0]) >= 1e-6f;
    } else if (f == 4) {
        dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
        dm_haptics_suspend(DM_HAPTICS_INACTIVE, 1);
        ok = (dm_haptics_available() == 1u);
        dm_haptics_suspend(DM_HAPTICS_INACTIVE, 0);
    } else if (f == 5) {
        dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
        dm_vibration_update u[2] = {{0, 0, {0, 160, 0, 320}}, {0, 1, {0.7f, 160, 0, 320}}};
        dm_haptics_send(u, 2);
        ok = fabsf(dm_haptics_actual(0, 1).amplitude_low - 0.7f) <= 1e-4f;
    }
    return ok ? 0 : 1;
}

int main(void) {
    int f = dm_hid_w23_fault();
    if (f > 0) {
        int rc = run_fault_check(f);
        printf("t_hid_fast_w23: fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    assert(test_motion_and_six_parity() == 0);
    assert(test_imu_and_haptics_parity() == 0);
    assert(test_tsan_stress() == 0);
    run_benchmarks();
    printf("t_hid_fast_w23: PASS\n");
    return 0;
}
#else
static hid_fast_motion_slot g_lin_slot;
static _Atomic int g_lin_stop;
static void *lin_motion_wr(void *arg) {
    (void)arg;
    for (int i = 1; i <= 500 && !atomic_load(&g_lin_stop); i++) {
        motion_state ms = {0}; motion_reset(&ms);
        ms.acc[0] = (float)i * 0.001f; ms.R[0][0] = 1.0f;
        hid_fast_motion_write(&g_lin_slot, &ms, (uint64_t)i * 1000000ull, atomic_load(&g_test_now));
        dm_vibration_update u = {0, i & 1, {(i % 3) * 0.25f, 160, 0, 320}};
        dm_haptics_send(&u, 1);
    }
    return NULL;
}
static int run_linux_fault(int f) {
    dm_haptics_fast_reset_for_test(1);
    int ok = 1;
    if (f == 1) {
        _Atomic uint32_t mask = 1u;
        if (dm_hid_w23_fault() != 1) atomic_fetch_and(&mask, ~1u);
        ok = (atomic_load(&mask) == 0);
    } else if (f == 2) {
        motion_state ms = {0}, out = {0}; motion_reset(&ms); ms.R[0][0] = 1.0f;
        hid_fast_motion_write(&g_lin_slot, &ms, 1000, atomic_load(&g_test_now));
        uint64_t ts = 0; hid_fast_motion_read(&g_lin_slot, atomic_load(&g_test_now), 300000000ull, &out, &ts);
        ok = fabsf(out.R[0][0] - 1.0f) < 1e-4f;
    } else if (f == 3) {
        uint64_t t0 = 4000000000ull;
        ok = !hid_fast_no_shake_window((int64_t)((t0 + 20000000ull) / 5000000ull), 16, t0, t0 + 160000000ull, 5000000ull);
    } else if (f == 4) {
        dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
        dm_haptics_suspend(DM_HAPTICS_INACTIVE, 1);
        ok = (dm_haptics_available() == 1u);
        dm_haptics_suspend(DM_HAPTICS_INACTIVE, 0);
    } else if (f == 5) {
        dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
        dm_vibration_update u[2] = {{0, 0, {0, 160, 0, 320}}, {0, 1, {0.7f, 160, 0, 320}}};
        dm_haptics_send(u, 2);
        ok = fabsf(dm_haptics_actual(0, 1).amplitude_low - 0.7f) < 1e-4f;
    }
    return ok ? 0 : 1;
}
int main(void) {
    int f = dm_hid_w23_fault();
    if (f > 0) {
        int rc = run_linux_fault(f);
        printf("t_hid_fast_w23 (linux): fault=%d caught=%d\n", f, rc != 0 ? 1 : 0);
        return rc;
    }
    dm_haptics_init(test_sink, 1); dm_haptics_route(0, 1);
    assert(hid_fast_no_shake_window(1000, 16, 4000000000ull, 4160000000ull, 5000000ull) == 1);
    assert(hid_fast_no_shake_window(804, 16, 4000000000ull, 4160000000ull, 5000000ull) == 0);
    dm_vibration_update batch[2] = {{0, 0, {0.3f, 160, 0, 320}}, {0, 1, {0.5f, 160, 0, 320}}};
    atomic_store(&g_sink_calls, 0); dm_haptics_send(batch, 2); assert(atomic_load(&g_sink_calls) == 1);
    dm_haptics_send(batch, 2); assert(atomic_load(&g_sink_calls) == 1 && dm_haptics_actual(0, 1).amplitude_low == 0.5f);
    dm_haptics_suspend(DM_HAPTICS_INACTIVE, 1);
    assert(dm_haptics_enabled() == 1 && dm_haptics_available() == 1u && dm_haptics_actual(0, 0).amplitude_low == 0.0f);
    dm_haptics_suspend(DM_HAPTICS_INACTIVE, 0);
    pthread_t wr; atomic_store(&g_lin_stop, 0);
    assert(!pthread_create(&wr, NULL, lin_motion_wr, NULL));
    for (int i = 0; i < 400; i++) {
        motion_state out = {0}; uint64_t ts = 0;
        hid_fast_motion_read(&g_lin_slot, atomic_load(&g_test_now), 300000000ull, &out, &ts);
        (void)dm_haptics_available(); (void)dm_haptics_actual(0, i & 1);
    }
    pthread_join(wr, NULL);
    for (int mode = 0; mode <= 1; mode++) {
        dm_haptics_fast_reset_for_test(mode);
        dm_haptics_send(batch, 2);
        uint64_t sink = 0, t0 = mono_ns();
        for (int i = 0; i < 40000; i++) { dm_haptics_send(batch, 2); sink += dm_haptics_available(); }
        printf("BENCH aoi_w23_linux_%s haptics_send_poll=%.3f ms sink=%llu\n",
            mode ? "on" : "off", (double)(mono_ns() - t0) / 1e6, (unsigned long long)sink);
    }
    printf("t_hid_fast_w23 (linux): PASS\n");
    return 0;
}
#endif
