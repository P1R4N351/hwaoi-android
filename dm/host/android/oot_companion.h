// SPDX-License-Identifier: MIT
// Ocarina of Time (Switch 2 v131072, build e0f3ccfa) adapter feeding companion_state.h.
#pragma once

#include "companion_state.h"
#include "oot_companion_tap.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OOT_MAIN_BUILD "e0f3ccfaf50065ae4ea01eb5aeb13b1a"
#define OOT_SCREEN_VT_PAUSE 0x4BE4FB0ULL
#define OOT_PAUSE_HOLD_FRAMES 3U
#define OOT_COMPANION_SAMPLE_INTERVAL_NS 50000000ULL

typedef struct OotCompanionCtx {
    CompanionState state;
    OotCompanionSnapshot snap;
    uint64_t last_mem_ns;
    uint64_t state_ns;
    uint32_t pause_hold;
    int32_t user_page;
    int have_snap;
} OotCompanionCtx;

static inline void oot_companion_ctx_init(OotCompanionCtx *c) {
    if (!c) return;
    companion_state_clear(&c->state);
    memset(&c->snap, 0, sizeof(c->snap));
    c->last_mem_ns = 0ULL;
    c->state_ns = 0ULL;
    c->pause_hold = 0U;
    c->user_page = -1;
    c->have_snap = 0;
}

static inline int oot_companion_is_title(const OotCompanionSnapshot *s) {
    if (!s) return 1;
    if (s->max_life == 0U || s->scene[0] == '\0') return 1;
    return strcmp(s->scene, "Title") == 0;
}

static inline void oot_companion_eval_snap(
    int enabled, const char *main_build, int have_snap, const OotCompanionSnapshot *snap,
    uint32_t pause_hold, int32_t user_page, CompanionState *out) {
    if (!out) return;
    companion_state_clear(out);
    if (!enabled) return;
    if (!main_build || main_build[0] == '\0') { out->status = COMPANION_STATE_STARTING; return; }
    if (strcmp(main_build, OOT_MAIN_BUILD) != 0) { out->status = COMPANION_STATE_UNSUPPORTED; return; }
    if (!have_snap || !snap || !snap->valid) { out->status = COMPANION_STATE_STARTING; return; }
    if (snap->is_loading || snap->in_transition) { out->status = COMPANION_STATE_LOADING; return; }
    if (oot_companion_is_title(snap)) { out->status = COMPANION_STATE_TITLE; return; }
    out->status = COMPANION_STATE_READY;
    out->in_game = 1;
    out->menu_open = (pause_hold > 0U) ? 1 : 0;
    out->tablet_owned = 1;
    out->life = (int32_t)snap->life;
    out->max_life = (int32_t)snap->max_life;
    out->page = snap->playing_ocarina ? COMPANION_PAGE_OCARINA : user_page;
}

static inline void oot_companion_eval(
    int enabled, const char *main_build, const OotTapMem *mem, uint64_t main_addr,
    uint32_t pause_hold, int32_t user_page, CompanionState *out) {
    if (!out) return;
    if (!enabled) { companion_state_clear(out); return; }
    if (!main_build || main_build[0] == '\0') {
        companion_state_clear(out);
        out->status = COMPANION_STATE_STARTING;
        return;
    }
    if (strcmp(main_build, OOT_MAIN_BUILD) != 0) {
        companion_state_clear(out);
        out->status = COMPANION_STATE_UNSUPPORTED;
        return;
    }
    if (main_addr < 0x10000ULL || !mem || !mem->read) {
        companion_state_clear(out);
        out->status = COMPANION_STATE_STARTING;
        return;
    }
    OotCompanionSnapshot snap;
    int ok = oot_companion_sample(mem, main_addr, &snap);
    oot_companion_eval_snap(enabled, main_build, ok, &snap, pause_hold, user_page, out);
}

static inline void oot_companion_ctx_note_canvas(OotCompanionCtx *c, uint64_t vtable_rel) {
    if (c && vtable_rel == OOT_SCREEN_VT_PAUSE) c->pause_hold = OOT_PAUSE_HOLD_FRAMES;
}

static inline void oot_companion_ctx_present(
    OotCompanionCtx *c, int enabled, const char *main_build, const OotTapMem *mem,
    uint64_t main_addr, uint64_t now_ns) {
    if (!c) return;
    c->state_ns = now_ns;
    if (!enabled) {
        companion_state_clear(&c->state);
        c->have_snap = 0;
        c->pause_hold = 0U;
        return;
    }
    if (!main_build || main_build[0] == '\0') {
        companion_state_clear(&c->state);
        c->state.status = COMPANION_STATE_STARTING;
        c->have_snap = 0;
        c->pause_hold = 0U;
        return;
    }
    if (strcmp(main_build, OOT_MAIN_BUILD) != 0) {
        companion_state_clear(&c->state);
        c->state.status = COMPANION_STATE_UNSUPPORTED;
        c->have_snap = 0;
        c->pause_hold = 0U;
        return;
    }
    if (main_addr < 0x10000ULL || !mem || !mem->read) {
        companion_state_clear(&c->state);
        c->state.status = COMPANION_STATE_STARTING;
        c->have_snap = 0;
        if (c->pause_hold > 0U) c->pause_hold--;
        return;
    }
    if (!c->have_snap || c->last_mem_ns == 0ULL || now_ns < c->last_mem_ns ||
        (now_ns - c->last_mem_ns) >= OOT_COMPANION_SAMPLE_INTERVAL_NS) {
        c->last_mem_ns = now_ns;
        c->have_snap = oot_companion_sample(mem, main_addr, &c->snap) ? 1 : 0;
    }
    oot_companion_eval_snap(enabled, main_build, c->have_snap, &c->snap, c->pause_hold, c->user_page, &c->state);
    if (c->pause_hold > 0U) c->pause_hold--;
}

void oot_companion_note_canvas(uint64_t vtable_rel);
void dm_companion_present(uint64_t frame);

#ifdef __cplusplus
}
#endif
