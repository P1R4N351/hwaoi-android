// nn::hid vibration is persistent device state: an unwritten motor keeps its value until stopped.
// No game-specific timing, strength compensation or save-file access belongs here.
#include "hle_haptics.h"
#include "hid_fast.h"
#include <math.h>
#include <pthread.h>
#include <string.h>

_Static_assert(sizeof(dm_vibration) == 16, "VibrationValue");
static const dm_vibration idle = {0, 160, 0, 320};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static dm_haptic_state states[DM_PLAYERS];
static void (*output)(dm_haptic_state);
static uint32_t available;
static unsigned suspended;
static int enabled;
static _Atomic int g_hap_en_fast;
static _Atomic uint32_t g_hap_avail_fast, g_hap_active_fast;
static hid_fast_hap_slot g_hap_fast[DM_PLAYERS];

#ifdef T_W23_TEST_HOOKS
void dm_haptics_fast_reset_for_test(int v) { dm_hid_fast_reset_for_test(v); }
#endif
static int valid(int p) { return p >= 0 && p < DM_PLAYERS; }
static void hap_sync_fast_locked(void) {
    int en = enabled && output != NULL;
    uint32_t av = en ? available : 0;
    atomic_store_explicit(&g_hap_en_fast, en, memory_order_release);
    atomic_store_explicit(&g_hap_avail_fast, av, memory_order_release);
    atomic_store_explicit(&g_hap_active_fast, suspended ? 0u : av, memory_order_release);
}
static void clear_locked(int p) {
    states[p].player = p; states[p].epoch++;
    states[p].motor[0] = states[p].motor[1] = idle;
    hid_fast_hap_write(&g_hap_fast[p], states[p].motor[0], states[p].motor[1]);
    if (output) output(states[p]);
}
void dm_haptics_init(void (*sink)(dm_haptic_state), int on) {
    pthread_mutex_lock(&lock);
    output = sink; enabled = on; available = 0; suspended = 0;
    hap_sync_fast_locked();
    for (int p = 0; p < DM_PLAYERS; p++) clear_locked(p);
    pthread_mutex_unlock(&lock);
}
void dm_haptics_route(int p, int on) {
    if (!valid(p)) return;
    pthread_mutex_lock(&lock);
    if (on) available |= 1u << p; else available &= ~(1u << p);
    hap_sync_fast_locked();
    clear_locked(p);
    pthread_mutex_unlock(&lock);
}
void dm_haptics_clear(int p) {
    if (!valid(p)) return;
    pthread_mutex_lock(&lock); clear_locked(p); pthread_mutex_unlock(&lock);
}
uint64_t dm_haptics_epoch(int p) {
    pthread_mutex_lock(&lock);
    uint64_t e = valid(p) ? states[p].epoch : 0;
    pthread_mutex_unlock(&lock); return e;
}
int dm_haptics_enabled(void) {
    if (dm_hid_fast_on()) return atomic_load_explicit(&g_hap_en_fast, memory_order_acquire);
    pthread_mutex_lock(&lock);
    int on = enabled && output != NULL;
    pthread_mutex_unlock(&lock); return on;
}
uint32_t dm_haptics_available(void) {
    if (dm_hid_fast_on()) {
        if (dm_hid_w23_fault() == 4) return atomic_load_explicit(&g_hap_active_fast, memory_order_acquire);
        return atomic_load_explicit(&g_hap_avail_fast, memory_order_acquire);
    }
    pthread_mutex_lock(&lock);
    uint32_t m = enabled && output ? available : 0;
    pthread_mutex_unlock(&lock); return m;
}
void dm_haptics_suspend(unsigned reason, int on) {
    pthread_mutex_lock(&lock);
    unsigned before = suspended;
    if (on) suspended |= reason; else suspended &= ~reason;
    hap_sync_fast_locked();
    if (before != suspended) for (int p = 0; p < DM_PLAYERS; p++) clear_locked(p);
    pthread_mutex_unlock(&lock);
}
static float amplitude(float x) { return hid_fast_amp(x); }
static float frequency(float x, float fallback) { return hid_fast_freq(x, fallback); }
void dm_haptics_send(const dm_vibration_update *u, int n) {
    if (!u || n <= 0) return;
    if (dm_hid_fast_on()) {
        uint32_t act = atomic_load_explicit(&g_hap_active_fast, memory_order_acquire);
        if (!act || hid_fast_hap_unchanged(g_hap_fast, act, u, n)) return;
    }
    pthread_mutex_lock(&lock);
    if (!enabled || !output || suspended) { pthread_mutex_unlock(&lock); return; }
    uint32_t changed = 0;
    for (int i = 0; i < n; i++) {
        int p = u[i].player, m = u[i].motor;
        if (!valid(p) || m < 0 || m > 1 || !(available >> p & 1)) continue;
        dm_vibration v = u[i].value;
        v.amplitude_low = amplitude(v.amplitude_low); v.amplitude_high = amplitude(v.amplitude_high);
        v.frequency_low = frequency(v.frequency_low, 160); v.frequency_high = frequency(v.frequency_high, 320);
        if (!memcmp(&v, &states[p].motor[m], sizeof v)) continue;
        states[p].motor[m] = v; changed |= 1u << p;
    }
    for (int p = 0; p < DM_PLAYERS; p++) if (changed >> p & 1) {
        hid_fast_hap_write(&g_hap_fast[p], states[p].motor[0], states[p].motor[1]);
        output(states[p]);
    }
    pthread_mutex_unlock(&lock);
}
dm_vibration dm_haptics_actual(int p, int m) {
    if (dm_hid_fast_on()) {
        uint32_t act = atomic_load_explicit(&g_hap_active_fast, memory_order_acquire);
        if (!valid(p) || m < 0 || m >= 2 || !(act >> p & 1u)) return idle;
        dm_vibration fv;
        if (hid_fast_hap_read_one(&g_hap_fast[p], m, &fv) == 0) return fv;
    }
    pthread_mutex_lock(&lock);
    dm_vibration v = enabled && !suspended && valid(p) && m >= 0 && m < 2 && (available >> p & 1) ? states[p].motor[m] : idle;
    pthread_mutex_unlock(&lock); return v;
}
dm_haptic_level dm_haptics_level(const dm_vibration *v, int n, float gain) {
    float power = 0, weighted = 0;
    for (int i = 0; i < n; i++) {
        float lo = amplitude(v[i].amplitude_low), hi = amplitude(v[i].amplitude_high);
        lo *= lo; hi *= hi; power += lo + hi;
        weighted += lo * logf(frequency(v[i].frequency_low, 160) / 40) + hi * logf(frequency(v[i].frequency_high, 320) / 40);
    }
    if (!isfinite(gain) || gain < 0) gain = 0;
    return (dm_haptic_level){fminf(1, sqrtf(power) * gain), power > 0 ? fminf(1, weighted / (power * logf(1250.0f / 40))) : 0};
}
