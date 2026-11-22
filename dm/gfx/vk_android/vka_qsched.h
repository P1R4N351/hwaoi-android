// NVN queue dependencies on the one Vulkan queue: the replay worker (vka_worker.c) replays the game's submissions in the order it made them, which is one valid order only
// while no queue waits for another. nvnCommandBufferWaitEvent / WaitSync and nvnQueueWaitSync make a queue's later work wait for a producer on another queue, which the game may
// submit later: Ocarina of Time submits its ambient-cache pass on queue 5 (it waits for an event) before the SH lighting pass on queue 1 that signals it, every frame. Replayed
// in submission order, the consumer read last frame's lighting (a pale outline around Link, scene-wide lighting flashes: upstream ea0c98df on the Mac).
//
// Here an unresolved wait parks only its own logical queue, and the other queues' submissions go on being replayed. A submission whose first wait comes before any event
// signal in it (qs_sub.w0kind) and does not hold when the worker reaches it is parked whole, before any of it is replayed: no cut, no extra commit (Ocarina of Time's
// waits). Otherwise the submission is cut at the wait (the part before it goes to the GPU); the rest and that queue's later submissions wait in its list. Once the producer has been replayed, the parked queue goes on, after it on the one
// in-order Vulkan queue, so the GPU runs them in the producer-consumer order.
// - Event waits use the SDK's predicates: EQUAL is exact equality, GEQUAL_WRAP is (uint32_t)(current - target) < 0x80000000. The value they see is the word as the replay
//   leaves it: a signal replayed but not completed yet counts with the value it writes (WRITE) or adds (ADD) (qs_ev), so a wait on a queued signal goes on at once (upstream
//   04419bc2); otherwise the word in memory (a completed signal, nvnEventSignal on the CPU, or a plain store through mapped pool memory, polled every millisecond while parked).
// - A sync wait looks at what the sync held when its submission was submitted (qs_wsnap): never fenced, the wait passes at once (DRAGON BALL Sparking! ZERO waits on a sync
//   before its own queue fences it, upstream 9b69d27d); fenced, it goes on once that fence's submission has been handed to the queue whole.
// - Fences stay one timeline, reserved in submission order (mtl_last_fence, the syncs). A submission replayed while a lower fence is parked is handed to the queue without a
//   signal; the next submission after which every fence up to some value v has been handed signals v (qs_commit). Its completion effects (events, counter reports) are posted
//   with the signal that covers them, in the order they were recorded, so they never land before their GPU work.
// - Safety valves, each counted (the stats line): a present, a queue finalization, a CPU wait (SyncWait, Finish, a pool's release) for a fence a park holds once the
//   worker has nothing else to replay (the producer is not coming while that thread waits), a parked queue's
//   backlog past QS_BACKLOG_MAX, and QS_DEFER_MAX unsignalled submissions (their completion effects wait for a covering signal) release every parked queue in
//   fence order with their waits passed, as the replay ran before. A park longer than the park time (DM_VK_EVENT_PARK_MS, default 200) is released too, with a [RED] line:
//   its producer did not come.
// - Waits for the CPU (gates): a wait whose event is set by the CPU (nvnEventSignal, or a store through mapped memory) has no producer in the replay. Parking it held the
//   replay of the waiting queue, and of every queue waiting on it, until the CPU's signal: Ocarina of Time begins each frame's main submission (queue 1) with a wait on an
//   event its CPU sets right after its SyncWait on the previous frame's main fence, so the next frame's 20 ms of recording began only once the GPU had finished the last one
//   (22 fps instead of 37 on the Honor, 2026-11-10). An event word whose parks were released twice in a row by memory, with no signal of the replay in flight (QS_GATE_LEARN),
//   is a CPU event: a later wait on it that does not hold is a gate (qs.gate): the replay goes on at once and the GPU waits (vka_dev.c "Gates": the next submission waits
//   for a semaphore value the host signals once the word holds). A gate that runs out of its time bans its word (back to parking).
// Pure C (no Vulkan): the unit test dm/host/tests/t_android_qsched.c runs the scheduler with a stand-in replay.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vka_qsched_fast.h"

enum { QS_NRET = 17, QS_NQ = 16, QS_NMARK = 256, QS_NDEF = 64, QS_NEV0 = 256, QS_DEFER_MAX = 32, QS_BACKLOG_MAX = 256, QS_NGW = 32, QS_GATE_LEARN = 2 };
enum { QS_WHY_PRESENT, QS_WHY_FORGET, QS_WHY_CPU_WAIT, QS_WHY_SLOTS, QS_WHY_BACKLOG, QS_WHY_TIMEOUT, QS_WHY_PROG, QS_NWHY };
static const char *const qs_why_name[QS_NWHY] = {"present", "queue finalized", "CPU wait", "command buffers", "backlog", "park time", "program finalized"};

// Where a submission cut at a wait goes on: the record after the wait, and the return points of the command lists that called it (innermost last)
typedef struct qs_cont { const uint8_t *p; int nret; const uint8_t *ret[QS_NRET]; } qs_cont;
typedef struct { const uint8_t *rec; uint64_t fence; } qs_wsnap;   // a WAIT_SYNC record and the fence its sync held when submitted (0: not fenced, the wait passes)

typedef struct qs_sub {
    struct qs_sub *next;
    uint64_t serial, fence, frame, seq; int origin_ll;   // fence 0: a queue-level wait (wait_fence), no commands
    const uint8_t **h; int nh;                            // the command handles (owned: freed with the submission)
    qs_wsnap *ws; int nws, wsi;                           // its WAIT_SYNC snapshots in replay order (owned), the next one
    uint64_t wait_fence;                                  // nvnQueueWaitSync: the fence its queue's later work waits for
    int hi, resumed; qs_cont cont;                        // replay goes on at handle hi (at cont when resumed)
    int wkind; volatile uint32_t *word; uint32_t mode, value; uint64_t wfence, t_park;   // the wait it is parked at: 1 event, 2 fence
    int w0kind; volatile uint32_t *w0word; uint32_t w0mode, w0value; uint64_t w0fence;   // its first wait when no event signal comes before it (1 event, 2 sync; 0 none): asked before replay
    volatile uint32_t *gword; uint32_t gmode, gvalue;                                    // the wait it was gated at last (asked again when the replay reaches it: passes)
    const volatile uint32_t *aword; uint32_t amode, avalue;                              // the wait asked last (a second ask of the same wait, after its park or before the replay, is not counted again)
} qs_sub;
typedef struct { uint64_t serial; qs_sub *head, *tail; int n; } qs_queue;   // head: parked at its wait; the rest wait behind it
typedef struct { volatile uint32_t *word; uint32_t value; uint64_t fence, frame; } qs_ev;   // an event word as the replay leaves it, until fence completes (frame: the game's frame of the submission that replayed the signal)
typedef struct { const volatile uint32_t *word; int cpu; } qs_gw;                     // an event word's releases by memory in a row (QS_GATE_LEARN: a CPU event)

typedef struct qs {
    qs_queue q[QS_NQ]; int nq;
    uint64_t lo, sig;                         // every fence <= lo handed to the queue whole; the highest fence signalled
    uint64_t mark[QS_NMARK]; int nmark;       // fences above lo handed whole
    uint64_t def[QS_NDEF]; int ndef;          // fences of the commits no signal covers yet (one per commit)
    qs_ev *ev; uint32_t evcap, nev, evpeak;   // the words a replayed signal is in flight for: open addressing by word, no deletions (stale entries go at a rebuild); evpeak: the most in flight at a commit (qs_commit)
    qs_gw gw[QS_NGW]; int ngw, ngw_next;
    int force; uint64_t seq;
    qs_sub *cur;                              // the submission being replayed (qs_wait_* and qs_event_replayed refer to it)
    int (*run)(void *ctx, qs_sub *s);
    uint64_t (*done)(void *ctx);
    uint64_t (*now)(void *ctx);
    void (*timeout)(void *ctx, const qs_sub *s);
    void (*note)(void *ctx, int what, const qs_sub *s);
    int (*gate)(void *ctx, qs_sub *s, volatile uint32_t *w, uint32_t mode, uint32_t value);
    void (*free_sub)(void *ctx, qs_sub *s);
    void *ctx;
    uint64_t park_ns;
    uint64_t n_park, n_whole, n_release, n_gate, n_force[QS_NWHY];
    uint64_t n_pass_queued, n_pass_old, n_pass_mem;
    const volatile uint32_t *pass_word; uint32_t pass_mode, pass_value; int pass_src;
    int force_why;
    uint64_t mark_bits[4];
    uint64_t ev_tracked_done;
    uint64_t ev_f_tag[256];
    uint32_t ev_f_cnt[256];
    uint32_t ev_live_exact;
    int ev_exact_bad;
    uint16_t park_mask;
    uint8_t gw_hash[64];
    qs_ev ev_inline[QS_NEV0];
} qs;

static inline int qs_pred(uint32_t cur, uint32_t mode, uint32_t target) { return mode == 0 ? cur == target : (uint32_t)(cur - target) < 0x80000000u; }   // NVNeventWaitMode

static inline int qs_whole(const qs *sc, uint64_t f) {
    if (f <= sc->lo) return 1;
    if (vka_qsched_fast_on() && f - sc->lo <= 256u) {
        return vka_qs_mark_test(sc->mark_bits, sc->lo, f);
    }
    for (int i = 0; i < sc->nmark; i++) if (sc->mark[i] == f) return 1;
    return 0;
}

static inline void qs_ev_peak(qs *sc);

static inline void qs_commit_mark(qs *sc, uint64_t f) {
    if (vka_qsched_fast_on()) {
        vka_qs_mark_set_and_drain(sc->mark_bits, &sc->lo, sc->mark, &sc->nmark, QS_NMARK, f);
        return;
    }
    if (sc->nmark < QS_NMARK) sc->mark[sc->nmark++] = f;
    for (int moved = 1; moved;) {
        moved = 0;
        for (int i = 0; i < sc->nmark; i++) {
            if (sc->mark[i] == sc->lo + 1u) { sc->lo++; sc->mark[i] = sc->mark[--sc->nmark]; moved = 1; break; }
        }
    }
}

static inline uint64_t qs_commit(qs *sc, uint64_t f, int final) {
    qs_ev_peak(sc);
    if (final && f > sc->lo && !qs_whole(sc, f)) qs_commit_mark(sc, f);
    if (final && sc->lo >= f && sc->lo > sc->sig) {
        sc->sig = sc->lo;
        int k = 0; for (int i = 0; i < sc->ndef; i++) if (sc->def[i] > sc->sig) sc->def[k++] = sc->def[i];
        sc->ndef = k;
        return sc->sig;
    }
    if (sc->ndef < QS_NDEF) sc->def[sc->ndef++] = f;
    return 0;
}

static inline uint64_t qs_park_min(const qs *sc) {
    uint64_t m = UINT64_MAX;
    if (vka_qsched_fast_on()) {
        if (!sc->park_mask && !sc->ndef) return UINT64_MAX;
        uint16_t pm = sc->park_mask;
        while (pm) {
            int qi = __builtin_ctz((unsigned)pm);
            pm &= (uint16_t)(pm - 1u);
            for (const qs_sub *s = sc->q[qi].head; s; s = s->next) if (s->fence && s->fence < m) m = s->fence;
        }
    } else {
        for (int i = 0; i < sc->nq; i++) for (const qs_sub *s = sc->q[i].head; s; s = s->next) if (s->fence && s->fence < m) m = s->fence;
    }
    for (int i = 0; i < sc->ndef; i++) if (sc->def[i] < m) m = sc->def[i];
    return m;
}

static inline int qs_parked(const qs *sc) {
    if (vka_qsched_fast_on()) return sc->park_mask != 0u;
    for (int i = 0; i < sc->nq; i++) if (sc->q[i].head) return 1;
    return 0;
}

static inline uint32_t qs_ev_slot(const volatile uint32_t *w, uint32_t cap) { uint64_t x = ((uint64_t)(uintptr_t)w >> 2) * 0x9e3779b97f4a7c15ull; return (uint32_t)(x >> 40) & (cap - 1); }

static inline qs_ev *qs_ev_find(qs *sc, const volatile uint32_t *w) {
    if (!sc->evcap) return NULL;
    for (uint32_t i = qs_ev_slot(w, sc->evcap);; i = (i + 1) & (sc->evcap - 1)) {
        if (sc->ev[i].word == w) return &sc->ev[i];
        if (!sc->ev[i].word) return NULL;
    }
}

static inline int qs_ev_rebuild_inline(qs *sc, uint32_t cap, uint64_t d, uint32_t live) {
    if (sc->evcap == 0u && cap == QS_NEV0) {
        sc->ev = sc->ev_inline;
        memset(sc->ev, 0, sizeof sc->ev_inline);
        sc->evcap = QS_NEV0; sc->nev = 0u;
        vka_qs_ev_resync(sc->ev_f_tag, sc->ev_f_cnt, &sc->ev_live_exact, &sc->ev_exact_bad,
                         &sc->ev_tracked_done, d, sc->ev, sizeof *sc->ev, sc->evcap,
                         __builtin_offsetof(qs_ev, word), __builtin_offsetof(qs_ev, fence));
        return 1;
    }
    if (cap == sc->evcap && live <= QS_NEV0) {
        qs_ev keep[QS_NEV0];
        uint32_t kn = 0u;
        for (uint32_t i = 0u; i < sc->evcap; i++) {
            if (sc->ev[i].word && sc->ev[i].fence > d) keep[kn++] = sc->ev[i];
        }
        memset(sc->ev, 0, (size_t)cap * sizeof *sc->ev);
        for (uint32_t i = 0u; i < kn; i++) {
            uint32_t k = qs_ev_slot(keep[i].word, cap);
            while (sc->ev[k].word) k = (k + 1u) & (cap - 1u);
            sc->ev[k] = keep[i];
        }
        sc->nev = kn;
        vka_qs_ev_resync(sc->ev_f_tag, sc->ev_f_cnt, &sc->ev_live_exact, &sc->ev_exact_bad,
                         &sc->ev_tracked_done, d, sc->ev, sizeof *sc->ev, sc->evcap,
                         __builtin_offsetof(qs_ev, word), __builtin_offsetof(qs_ev, fence));
        return 1;
    }
    return 0;
}

static inline int qs_ev_rebuild(qs *sc, uint32_t cap) {
    uint64_t d = sc->done ? sc->done(sc->ctx) : 0u;
    uint32_t live = 0u;
    for (uint32_t i = 0; i < sc->evcap; i++) if (sc->ev[i].word && sc->ev[i].fence > d) live++;
    if (live > sc->evpeak) sc->evpeak = live;
    if (cap < QS_NEV0) cap = QS_NEV0;
    while ((uint64_t)(live + 1u) * 4u > cap) cap *= 2u;
    if (vka_qsched_fast_on() && qs_ev_rebuild_inline(sc, cap, d, live)) return 1;
    qs_ev *n = calloc(cap, sizeof *n);
    if (!n) return 0;
    for (uint32_t i = 0; i < sc->evcap; i++) {
        qs_ev *e = &sc->ev[i];
        if (!e->word || e->fence <= d) continue;
        uint32_t k = qs_ev_slot(e->word, cap);
        while (n[k].word) k = (k + 1u) & (cap - 1u);
        n[k] = *e;
    }
    if (sc->ev && sc->ev != sc->ev_inline) free(sc->ev);
    sc->ev = n; sc->evcap = cap; sc->nev = live;
    if (vka_qsched_fast_on()) {
        vka_qs_ev_resync(sc->ev_f_tag, sc->ev_f_cnt, &sc->ev_live_exact, &sc->ev_exact_bad,
                         &sc->ev_tracked_done, d, sc->ev, sizeof *sc->ev, sc->evcap,
                         __builtin_offsetof(qs_ev, word), __builtin_offsetof(qs_ev, fence));
    }
    return 1;
}

static inline void qs_ev_prune(qs *sc) { if (sc->evcap) qs_ev_rebuild(sc, sc->evcap); }

static inline void qs_ev_free(qs *sc) {
    if (sc->ev && sc->ev != sc->ev_inline) free(sc->ev);
    sc->ev = NULL; sc->evcap = sc->nev = sc->ev_live_exact = 0u;
    sc->ev_tracked_done = 0u; sc->ev_exact_bad = 0;
    memset(sc->ev_f_cnt, 0, sizeof sc->ev_f_cnt);
}

static inline uint32_t qs_ev_value(qs *sc, const volatile uint32_t *w) {
    qs_ev *e = qs_ev_find(sc, w);
    uint64_t d = sc->done ? sc->done(sc->ctx) : 0u;
    if (e && e->fence > d) return e->value;
    return *w;
}

static inline int qs_ev_inflight(qs *sc, const volatile uint32_t *w) {
    qs_ev *e = qs_ev_find(sc, w);
    uint64_t d = sc->done ? sc->done(sc->ctx) : 0u;
    return e && e->fence > d;
}

static inline uint32_t qs_ev_live(qs *sc) {
    if (!sc->evcap) return 0u;
    uint64_t d = sc->done ? sc->done(sc->ctx) : 0u;
    uint32_t n = 0u;
    for (uint32_t i = 0; i < sc->evcap; i++) if (sc->ev[i].word && sc->ev[i].fence > d) n++;
    if (vka_qsched_fast_on()) {
        vka_qs_ev_resync(sc->ev_f_tag, sc->ev_f_cnt, &sc->ev_live_exact, &sc->ev_exact_bad,
                         &sc->ev_tracked_done, d, sc->ev, sizeof *sc->ev, sc->evcap,
                         __builtin_offsetof(qs_ev, word), __builtin_offsetof(qs_ev, fence));
    }
    return n;
}

static inline void qs_ev_peak(qs *sc) {
    if (vka_qsched_fast_on()) {
        if (vka_qs_ev_can_skip_peak(sc->ev_live_exact, sc->ev_exact_bad, sc->evpeak)) return;
        uint64_t d = sc->done ? sc->done(sc->ctx) : 0u;
        if (vka_qs_ev_advance_done(sc->ev_f_tag, sc->ev_f_cnt, &sc->ev_live_exact,
                                   &sc->ev_exact_bad, &sc->ev_tracked_done, d)) {
            if (sc->ev_live_exact > sc->evpeak) sc->evpeak = sc->ev_live_exact;
            return;
        }
    }
    if (sc->nev > sc->evpeak) { uint32_t n = qs_ev_live(sc); if (n > sc->evpeak) sc->evpeak = n; }
}

static inline qs_gw *qs_gw_find_fast(qs *sc, const volatile uint32_t *w, int add) {
    if (aoi_w43_fault_mode() == 4) return NULL;
    uint32_t b = vka_qs_gw_bucket(w);
    for (uint32_t step = 0u; step < 64u; step++) {
        uint8_t slot_val = sc->gw_hash[(b + step) & 63u];
        if (!slot_val) break;
        if (sc->gw[slot_val - 1u].word == w) return &sc->gw[slot_val - 1u];
    }
    if (!add) return NULL;
    int idx = sc->ngw < QS_NGW ? sc->ngw++ : sc->ngw_next++ % QS_NGW;
    int need_rehash = (sc->gw[idx].word != NULL);
    sc->gw[idx].word = w;
    sc->gw[idx].cpu = 0;
    if (need_rehash) {
        vka_qs_gw_rehash(sc->gw_hash, sc->gw, sizeof sc->gw[0], sc->ngw, __builtin_offsetof(qs_gw, word));
    } else {
        for (uint32_t step = 0u; step < 64u; step++) {
            uint32_t sl = (b + step) & 63u;
            if (!sc->gw_hash[sl]) { sc->gw_hash[sl] = (uint8_t)(idx + 1); break; }
        }
    }
    return &sc->gw[idx];
}

static inline qs_gw *qs_gw_find(qs *sc, const volatile uint32_t *w, int add) {
    if (vka_qsched_fast_on()) return qs_gw_find_fast(sc, w, add);
    for (int i = 0; i < sc->ngw; i++) if (sc->gw[i].word == w) return &sc->gw[i];
    if (!add) return NULL;
    qs_gw *g = sc->ngw < QS_NGW ? &sc->gw[sc->ngw++] : &sc->gw[sc->ngw_next++ % QS_NGW];
    g->word = w; g->cpu = 0; return g;
}

static inline void qs_gw_note(qs *sc, const volatile uint32_t *w, int by_memory) {
    qs_gw *g = qs_gw_find(sc, w, by_memory); if (!g) return;
    g->cpu = by_memory ? g->cpu + 1 : 0;
}

static inline void qs_gate_ban(qs *sc, const volatile uint32_t *w) { qs_gw *g = qs_gw_find(sc, w, 0); if (g) g->cpu = -1000000; }
static inline int qs_gw_cpu(qs *sc, const volatile uint32_t *w) { qs_gw *g = qs_gw_find(sc, w, 0); return g && g->cpu >= QS_GATE_LEARN; }

static inline void qs_event_replayed(qs *sc, volatile uint32_t *w, uint32_t mode, uint32_t v) {
    if (!sc->cur || !sc->cur->fence) return;
    uint32_t base = 0u;
    int existed = 0; uint64_t old_fence = 0u;
    qs_ev *e = qs_ev_find(sc, w);
    if (vka_qsched_fast_on()) {
        uint64_t d = (mode == 1u && sc->done) ? sc->done(sc->ctx) : 0u;
        if (e) { existed = 1; old_fence = e->fence; }
        if (mode == 1u) base = (e && e->fence > d) ? e->value : *w;
    } else {
        base = qs_ev_value(sc, w);
    }
    if (!e) {
        if ((sc->nev + 1u) * 2u > sc->evcap && !qs_ev_rebuild(sc, sc->evcap)) return;
        uint32_t k = qs_ev_slot(w, sc->evcap); while (sc->ev[k].word) k = (k + 1u) & (sc->evcap - 1u);
        e = &sc->ev[k]; e->word = w; sc->nev++;
    }
    e->value = mode == 1u ? base + v : v; e->fence = sc->cur->fence; e->frame = sc->cur->frame;
    if (vka_qsched_fast_on()) {
        vka_qs_ev_note_signal(sc->ev_f_tag, sc->ev_f_cnt, &sc->ev_live_exact, &sc->ev_exact_bad,
                              sc->ev_tracked_done, existed, old_fence, e->fence);
    }
}

static inline int qs_wait_event(qs *sc, volatile uint32_t *w, uint32_t mode, uint32_t value) {
    qs_sub *s = sc->cur;
    if (!s || sc->force) return 1;
    int again = s->aword == w && s->amode == mode && s->avalue == value;
    s->aword = w; s->amode = mode; s->avalue = value;
    qs_ev *e = vka_qsched_fast_on() ? qs_ev_find(sc, w) : NULL;
    uint64_t d = sc->done ? sc->done(sc->ctx) : 0u;
    int queued = e && e->fence > d;
    uint32_t cur = vka_qsched_fast_on() ? (queued ? e->value : *w) : qs_ev_value(sc, w);
    if (qs_pred(cur, mode, value)) {
        if (!again) {
            if (!vka_qsched_fast_on()) { e = qs_ev_find(sc, w); queued = e && e->fence > d; }
            int src = queued ? (e->frame < s->frame ? 2 : 1) : 3;
            if (src == 1) sc->n_pass_queued++; else if (src == 2) sc->n_pass_old++; else sc->n_pass_mem++;
            if (sc->note) { sc->pass_word = w; sc->pass_mode = mode; sc->pass_value = value; sc->pass_src = src; sc->note(sc->ctx, 3, s); }
        }
        return 1;
    }
    if (s->gword == w && s->gmode == mode && s->gvalue == value) return 1;
    if (sc->gate && s->fence && qs_gw_cpu(sc, w) && sc->gate(sc->ctx, s, w, mode, value)) {
        s->gword = w; s->gmode = mode; s->gvalue = value; sc->n_gate++;
        if (sc->note) { s->wkind = 1; s->word = w; s->mode = mode; s->value = value; sc->note(sc->ctx, 2, s); s->wkind = 0; }
        return 1;
    }
    s->wkind = 1; s->word = w; s->mode = mode; s->value = value; s->t_park = sc->now(sc->ctx);
    return 0;
}

static inline int qs_fence_wait(qs *sc, uint64_t f) {
    qs_sub *s = sc->cur;
    if (!s || sc->force || !f || (s->fence && f >= s->fence) || qs_whole(sc, f)) return 1;
    s->wkind = 2; s->wfence = f; s->t_park = sc->now(sc->ctx);
    return 0;
}

static inline int qs_wait_sync(qs *sc, const uint8_t *rec) {
    qs_sub *s = sc->cur; if (!s) return 1;
    int i = s->wsi < s->nws && s->ws[s->wsi].rec == rec ? s->wsi : -1;
    for (int k = 0; i < 0 && k < s->nws; k++) if (s->ws[k].rec == rec) i = k;
    if (i < 0) return 1;
    if (!qs_fence_wait(sc, s->ws[i].fence)) return 0;
    s->wsi = i + 1; return 1;
}

static inline int qs_released(qs *sc, const qs_sub *s) {
    if (s->wkind == 1) return qs_pred(qs_ev_value(sc, s->word), s->mode, s->value);
    if (s->wkind == 2) return qs_whole(sc, s->wfence);
    return 1;
}

static inline qs_queue *qs_queue_of(qs *sc, uint64_t serial) {
    for (int i = 0; i < sc->nq; i++) if (sc->q[i].serial == serial) return &sc->q[i];
    for (int i = 0; i < sc->nq; i++) if (!sc->q[i].head) { sc->q[i].serial = serial; return &sc->q[i]; }
    if (sc->nq == QS_NQ) return NULL;
    qs_queue *q = &sc->q[sc->nq++]; memset(q, 0, sizeof *q); q->serial = serial; return q;
}

static inline void qs_free(qs_sub *s) { free((void *)s->h); free(s->ws); free(s); }
static inline void qs_free_sub(qs *sc, qs_sub *s) { if (sc && sc->free_sub) sc->free_sub(sc->ctx, s); else qs_free(s); }
static inline void qs_force_all(qs *sc, int why);
static inline int qs_room(qs *sc) { if (sc->force || sc->ndef < QS_DEFER_MAX) return 1; qs_force_all(sc, QS_WHY_SLOTS); return 0; }

static inline int qs_run_one(qs *sc, qs_queue *q, qs_sub *s) {
    sc->cur = s; s->wkind = 0;
    int parked = 0;
    if (!s->fence) parked = !qs_fence_wait(sc, s->wait_fence);
    else if (!s->hi && !s->resumed && s->w0kind && !sc->force &&
             !(s->w0kind == 1 ? qs_wait_event(sc, s->w0word, s->w0mode, s->w0value) : qs_fence_wait(sc, s->w0fence))) { parked = 1; sc->n_whole++; }
    else parked = sc->run(sc->ctx, s);
    sc->cur = NULL;
    if (parked) {
        s->next = q->head; q->head = s; if (!q->tail) q->tail = s;
        q->n++; sc->n_park++;
        if (q >= sc->q && q < sc->q + QS_NQ) vka_qs_park_set(&sc->park_mask, (int)(q - sc->q));
        if (sc->note) sc->note(sc->ctx, 0, s);
        return 1;
    }
    qs_free_sub(sc, s); return 0;
}

static inline void qs_drain_queue(qs *sc, qs_queue *q) {
    int qi = (q >= sc->q && q < sc->q + QS_NQ) ? (int)(q - sc->q) : -1;
    while (q->head) {
        if (!qs_room(sc)) return;
        qs_sub *s = q->head; q->head = s->next;
        if (!q->head) { q->tail = NULL; if (qi >= 0) vka_qs_park_sync_clear(&sc->park_mask, qi, NULL); }
        q->n--; s->next = NULL;
        if (qs_run_one(sc, q, s)) return;
    }
}

static inline void qs_submit(qs *sc, qs_sub *s) {
    s->seq = ++sc->seq;
    qs_queue *q = qs_queue_of(sc, s->serial);
    if (!q) { qs_force_all(sc, QS_WHY_BACKLOG); q = qs_queue_of(sc, s->serial); }
    if (q && q->head) {
        s->next = NULL; q->tail->next = s; q->tail = s; q->n++;
        if (q->n > QS_BACKLOG_MAX) qs_force_all(sc, QS_WHY_BACKLOG);
        return;
    }
    qs_room(sc);
    if (!q) { int f = sc->force; sc->force = 1; qs_queue tmp = {0}; qs_run_one(sc, &tmp, s); sc->force = f; return; }
    qs_run_one(sc, q, s);
}

static inline int qs_poll_one(qs *sc, int qi, int *moved) {
    qs_queue *q = &sc->q[qi];
    qs_sub *s = q->head;
    if (!s) { vka_qs_park_sync_clear(&sc->park_mask, qi, NULL); return 0; }
    int ok = qs_released(sc, s);
    if (!ok && sc->park_ns && sc->now(sc->ctx) - s->t_park > sc->park_ns) {
        if (!qs_room(sc)) { *moved = 1; return 1; }
        if (sc->timeout) sc->timeout(sc->ctx, s);
        if (s->wkind == 1) qs_gw_note(sc, s->word, 0);
        sc->n_force[QS_WHY_TIMEOUT]++;
        q->head = s->next;
        if (!q->head) { q->tail = NULL; vka_qs_park_sync_clear(&sc->park_mask, qi, NULL); }
        q->n--; s->next = NULL;
        int f = sc->force; sc->force = 1; qs_run_one(sc, q, s); sc->force = f;
        qs_drain_queue(sc, q);
        *moved = 1; return 1;
    }
    if (!ok) return 0;
    if (s->wkind == 1) qs_gw_note(sc, s->word, !qs_ev_inflight(sc, s->word));
    sc->n_release++; *moved = 1;
    if (sc->note) sc->note(sc->ctx, 1, s);
    qs_drain_queue(sc, q);
    return 1;
}

static inline int qs_poll(qs *sc) {
    if (vka_qsched_fast_on() && !sc->park_mask) return 0;
    int n = 0;
    for (int moved = 1; moved;) {
        moved = 0;
        if (vka_qsched_fast_on()) {
            uint16_t pm = sc->park_mask;
            while (pm) {
                int qi = __builtin_ctz((unsigned)pm);
                pm &= (uint16_t)(pm - 1u);
                n += qs_poll_one(sc, qi, &moved);
            }
        } else {
            for (int i = 0; i < sc->nq; i++) n += qs_poll_one(sc, i, &moved);
        }
    }
    return n;
}

static inline void qs_force_all(qs *sc, int why) {
    if (sc->force || !qs_parked(sc)) return;
    sc->n_force[why]++;
    if (sc->note) { sc->force_why = why; sc->note(sc->ctx, 4, NULL); }
    sc->force = 1;
    for (;;) {
        qs_queue *best = NULL;
        if (vka_qsched_fast_on()) {
            uint16_t pm = sc->park_mask;
            while (pm) {
                int i = __builtin_ctz((unsigned)pm);
                pm &= (uint16_t)(pm - 1u);
                if (sc->q[i].head && (!best || sc->q[i].head->seq < best->head->seq)) best = &sc->q[i];
            }
        } else {
            for (int i = 0; i < sc->nq; i++) if (sc->q[i].head && (!best || sc->q[i].head->seq < best->head->seq)) best = &sc->q[i];
        }
        if (!best) break;
        qs_sub *s = best->head; best->head = s->next;
        if (!best->head) { best->tail = NULL; vka_qs_park_sync_clear(&sc->park_mask, (int)(best - sc->q), NULL); }
        best->n--; s->next = NULL;
        qs_run_one(sc, best, s);
    }
    sc->force = 0;
}
