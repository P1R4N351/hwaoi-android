// SPDX-License-Identifier: MIT
/* The second screen's common game state: the narrow MIT-licensed waist shared between the per-game native reader and
   consumers above it (Java CompanionState, stick-LED Bifrost bridge, future second-screen shell). Words are append-only
   in fixed order so readers and consumers never drift across games. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* status */
enum {
    COMPANION_STATE_NONE = 0,        /* nothing read: no game yet, no sample, companion off */
    COMPANION_STATE_STARTING = 1,    /* the game is running, its state is not readable yet */
    COMPANION_STATE_UNSUPPORTED = 2, /* another build of the game than the reader knows */
    COMPANION_STATE_TITLE = 3,       /* the title screen */
    COMPANION_STATE_LOADING = 4,     /* scene / save load transition */
    COMPANION_STATE_READY = 5,       /* a save is loaded */
};

/* Well-known page indices exposed across the waist (-1 = unknown / unchanged). */
enum {
    COMPANION_PAGE_OCARINA = 4,      /* automatic companion switch while playing the ocarina */
};

typedef struct CompanionState {
    int32_t status;                  /* COMPANION_STATE_* */
    int32_t in_game;                 /* 1: a save is loaded and gameplay (or pause menu) is active */
    int32_t menu_open;               /* 1: the game's pause menu canvas is open */
    int32_t tablet_owned;            /* 1 for OoT (0 only where a companion slate can be missing at start) */
    int32_t life, max_life;          /* quarter hearts; -1 when not read */
    int32_t page;                    /* second screen's current page; -1 unknown */
} CompanionState;

static inline void companion_state_clear(CompanionState *s) {
    s->status = COMPANION_STATE_NONE;
    s->in_game = 0;
    s->menu_open = 0;
    s->tablet_owned = 1;
    s->life = -1;
    s->max_life = -1;
    s->page = -1;
}

/* Word order exported through JNI (Native.companionState): the struct fields above, then sample age in milliseconds. */
enum { COMPANION_STATE_FIELDS = 8 };

static inline void companion_state_words(const CompanionState *s, int64_t age_ms, int64_t out[COMPANION_STATE_FIELDS]) {
    out[0] = s->status;
    out[1] = s->in_game;
    out[2] = s->menu_open;
    out[3] = s->tablet_owned;
    out[4] = s->life;
    out[5] = s->max_life;
    out[6] = s->page;
    out[7] = age_ms;
}

#ifdef __cplusplus
}
#endif
