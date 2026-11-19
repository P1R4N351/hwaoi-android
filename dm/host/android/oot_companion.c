// SPDX-License-Identifier: MIT
// Runtime glue between OoT's guest memory / present hook and the common CompanionState waist.
#include "oot_companion.h"
#include "input_android.h"
#include "dm.h"
#include "plat/plat.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t g_companion_mu = PTHREAD_MUTEX_INITIALIZER;
static OotCompanionCtx g_companion_ctx;
static int g_companion_inited;

static void companion_ensure_init_locked(void) {
    if (!g_companion_inited) {
        oot_companion_ctx_init(&g_companion_ctx);
        g_companion_inited = 1;
    }
}

static int companion_enabled(void) {
    const char *e = getenv("DM_COMPANION");
    return !(e && strcmp(e, "0") == 0);
}

static int companion_plat_read(void *ctx, uint64_t addr, void *out, size_t len) {
    (void)ctx;
    return plat_mem_read(addr, out, len) == 0 ? 1 : 0;
}

static uint64_t companion_main_base(void) {
    for (int i = 0; i < g_nmod; i++) {
        if (strcmp(g_mod[i].name, "main") == 0) return (uint64_t)(uintptr_t)g_mod[i].base;
    }
    return 0ULL;
}

void oot_companion_note_canvas(uint64_t vtable_rel) {
    if (pthread_mutex_lock(&g_companion_mu) != 0) return;
    companion_ensure_init_locked();
    oot_companion_ctx_note_canvas(&g_companion_ctx, vtable_rel);
    (void)pthread_mutex_unlock(&g_companion_mu);
}

void dm_companion_present(uint64_t frame) {
    (void)frame;
#ifdef DM_GAME_ID
    if (strcmp(DM_GAME_ID, "0400486024DEC000") != 0) return;
#endif
    int en = companion_enabled();
    const char *build = dm_main_build();
    uint64_t base = companion_main_base();
    uint64_t now_ns = plat_clock_ns(CLOCK_MONOTONIC);
    OotTapMem mem = {companion_plat_read, NULL};
    if (pthread_mutex_lock(&g_companion_mu) != 0) return;
    companion_ensure_init_locked();
    oot_companion_ctx_present(&g_companion_ctx, en, build, &mem, base, now_ns);
    (void)pthread_mutex_unlock(&g_companion_mu);
}

int nxhost_companion_state(int64_t out[COMPANION_STATE_FIELDS]) {
    if (!out) return 0;
    CompanionState st;
    uint64_t st_ns = 0ULL;
    if (pthread_mutex_lock(&g_companion_mu) != 0) return 0;
    companion_ensure_init_locked();
    st = g_companion_ctx.state;
    st_ns = g_companion_ctx.state_ns;
    (void)pthread_mutex_unlock(&g_companion_mu);
    if (!companion_enabled()) {
        companion_state_clear(&st);
        companion_state_words(&st, 0, out);
        return 0;
    }
    int64_t age_ms = -1;
    if (st_ns > 0ULL) {
        uint64_t now_ns = plat_clock_ns(CLOCK_MONOTONIC);
        age_ms = (now_ns >= st_ns) ? (int64_t)((now_ns - st_ns) / 1000000ULL) : 0;
    }
    companion_state_words(&st, age_ms, out);
    return (st.status != COMPANION_STATE_NONE && st_ns > 0ULL) ? 1 : 0;
}

void nxhost_companion_set_page(int page) {
    if (pthread_mutex_lock(&g_companion_mu) != 0) return;
    companion_ensure_init_locked();
    g_companion_ctx.user_page = (int32_t)page;
    (void)pthread_mutex_unlock(&g_companion_mu);
}
