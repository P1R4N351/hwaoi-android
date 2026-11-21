// The frame rate the launcher's overlay shows (nxhost_fps, vk_android.h): the present thread counts each image it hands to the swapchain (vka_fps_note), the game's own
// frames apart from frames the frame generation made (vka_framegen.c), and publishes the counts once a second. Nothing is drawn here: the launcher reads the numbers through JNI,
// draws its text into a bitmap and hands it back (nxhost_overlay_set), and the present draws that into the game's images (vka_present.c ovl_draw). The log gets the same
// numbers every 15 s ("[vk] fps: ...")
#if defined(__ANDROID__)
#include "vka.h"
#include "vk_android.h"
#include "vka_ovl_fast.h"
#include <pthread.h>

static uint64_t g_win_t0, g_base_n, g_out_n, g_base_ns_sum, g_last_base, g_log_t;   // present thread only
static _Atomic uint32_t g_pub_base, g_pub_out, g_pub_ft;   // published: fps x 100, fps x 100, frame time in µs
static _Atomic uint64_t g_pub_t;                          // when they were published (now_ns)
static _Atomic int g_fg_state, g_fg_mult;                 // frame generation: 0 off, 1 generating, 2 on but not generating (warming up, paced out), -1 unavailable; frames per game frame

void vka_fps_fg(int state, int mult) { atomic_store(&g_fg_state, state); atomic_store(&g_fg_mult, mult); }

void vka_fps_note(int shown, int generated) {
    if (!shown) return;
    uint64_t t = now_ns();
    if (!g_win_t0) g_win_t0 = t;
    g_out_n++;
    if (!generated) {
        if (g_last_base && t - g_last_base < 1000000000ull) g_base_ns_sum += t - g_last_base;
        g_last_base = t; g_base_n++;
    }
    uint64_t dt = t - g_win_t0;
    if (dt < 1000000000ull) return;
    double s = dt / 1e9;
    atomic_store(&g_pub_base, (uint32_t)(g_base_n / s * 100 + 0.5));
    atomic_store(&g_pub_out, (uint32_t)(g_out_n / s * 100 + 0.5));
    atomic_store(&g_pub_ft, g_base_n ? (uint32_t)(g_base_ns_sum / g_base_n / 1000) : 0);
    atomic_store(&g_pub_t, t);
    if (t - g_log_t >= 15000000000ull) {
        g_log_t = t;
        int st = atomic_load(&g_fg_state);
        VLOG("fps: %.1f game frames a second (%.1f ms), %.1f presented%s", g_base_n / s, g_base_n ? g_base_ns_sum / g_base_n / 1e6 : 0.0, g_out_n / s,
             st == 1 ? " (frame generation on)" : st == 2 ? " (frame generation on, not generating)" : st < 0 ? " (frame generation unavailable)" : "");
    }
    g_win_t0 = t; g_base_n = g_out_n = g_base_ns_sum = 0;
}

// The GPU's busy time a frame, averaged over up to 32 recent frames whose time is in (vka_exec.c reads two timestamps per submission; a frame's time is in 8 frames
// later): only when the overlay asks (twice a second), a few atomic loads a frame. -1 without GPU timestamps
uint64_t vka_frame(void); double mtl_gpu_ms(uint64_t f);
static double fps_gpu_sample(uint64_t frame, void *ctx) { (void)ctx; return mtl_gpu_ms(frame); }
static float gpu_ms(void) {
    static pthread_mutex_t g_gpu_mu = PTHREAD_MUTEX_INITIALIZER;
    static vka_ovl_gpu_avg_memo g_gpu_memo;
    pthread_mutex_lock(&g_gpu_mu);
    float r = vka_ovl_gpu_avg(&g_gpu_memo, vka_frame(), fps_gpu_sample, NULL);
    pthread_mutex_unlock(&g_gpu_mu);
    return r;
}

int nxhost_fps(float *out, int n) {
    float v[6];
    uint64_t t = atomic_load(&g_pub_t);
    int fresh = t && now_ns() - t < 2500000000ull;   // no present for 2.5 s (a load, the app in the background): nothing to show
    v[0] = fresh ? atomic_load(&g_pub_base) / 100.0f : 0;
    v[1] = fresh ? atomic_load(&g_pub_out) / 100.0f : 0;
    v[2] = fresh ? atomic_load(&g_pub_ft) / 1000.0f : 0;
    v[3] = (float)atomic_load(&g_fg_state);
    v[4] = (float)atomic_load(&g_fg_mult);
    v[5] = fresh && n > 5 ? gpu_ms() : -1;
    for (int i = 0; i < n && i < 6; i++) out[i] = v[i];
    return fresh;
}
#endif
