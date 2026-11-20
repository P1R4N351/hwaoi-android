// O(1) Horizon kernel synchronization fast paths (DM_SYNC_FAST=1):
//   1) 64-word bitmap allocator/counter for MAX_H=4096 handle table (preserves lowest-free-slot semantics)
//   2) 64-bucket hash index + priority/FIFO comparison helper for wait queues
//   3) 64-bit handle-object Bloom filter for WaitSynchronization selective wakes
//   4) 64x4 set-associative front-index for SystemEvent (ev -> wh, id -> wh) with linear fallback
#ifndef HLE_SYNC_FAST_H
#define HLE_SYNC_FAST_H

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W7_TEST_HOOKS
extern int g_sync_fault;
#define HLE_SYNC_FAULT(n) (g_sync_fault == (n))
#else
#define HLE_SYNC_FAULT(n) 0
#endif

enum { HLE_HMAP_WORDS = 64, HLE_WB_BUCKETS = 64, HLE_SE_SETS = 64, HLE_SE_WAYS = 4 };

typedef struct {
    uint64_t used[HLE_HMAP_WORDS];
} hle_hmap;

typedef struct {
    void *ev;
    uint32_t wh;
    uint32_t pad;
    uint64_t id;
} hle_se_slot;

typedef struct {
    hle_se_slot way[HLE_SE_WAYS];
} hle_se_set;

typedef struct {
    hle_se_set set[HLE_SE_SETS];
    uint32_t overflow;
} hle_se_idx;

static inline unsigned hle_hmap_alloc(hle_hmap *m) {
    assert(m != NULL);
    m->used[0] |= 1ull;
    for (unsigned w = 0; w < HLE_HMAP_WORDS; w++) {
        uint64_t free_bits = ~m->used[w];
        if (!free_bits) continue;
        unsigned bit = (unsigned)__builtin_ctzll(free_bits);
        if (!HLE_SYNC_FAULT(1)) m->used[w] |= 1ull << bit;
        return (w << 6) | bit;
    }
    return 0;
}

static inline void hle_hmap_mark(hle_hmap *m, unsigned idx) {
    assert(m != NULL && idx > 0 && idx < 4096u);
    m->used[0] |= 1ull;
    m->used[idx >> 6] |= 1ull << (idx & 63u);
}

static inline void hle_hmap_free(hle_hmap *m, unsigned idx) {
    assert(m != NULL && idx < 4096u);
    if (!idx) return;
    m->used[idx >> 6] &= ~(1ull << (idx & 63u));
}

static inline int hle_hmap_count(const hle_hmap *m) {
    assert(m != NULL);
    int n = 0;
    for (unsigned w = 0; w < HLE_HMAP_WORDS; w++) {
        uint64_t bits = w ? m->used[w] : (m->used[0] & ~1ull);
        n += __builtin_popcountll(bits);
    }
    return n;
}

static inline unsigned hle_wb_hash(int kind, uint64_t addr) {
    uint64_t x = (addr >> 2) ^ ((uint64_t)(unsigned)kind * 0x9E3779B97F4A7C15ull);
    return (unsigned)(((x * 0xBF58476D1CE4E5B9ull) >> 58) & (HLE_WB_BUCKETS - 1u));
}

static inline int hle_wb_prio_better(int prio_a, uint64_t seq_a, int prio_b, uint64_t seq_b) {
    if (HLE_SYNC_FAULT(2)) return prio_a > prio_b || (prio_a == prio_b && seq_a > seq_b);
    return prio_a < prio_b || (prio_a == prio_b && seq_a < seq_b);
}

static inline uint64_t hle_obj_bloom_ins(const void *obj) {
    uint64_t b = (((uintptr_t)obj >> 4) * 0x9E3779B97F4A7C15ull) >> 58;
    return 1ull << (b & 63u);
}

static inline uint64_t hle_obj_bloom_qry(const void *obj) {
    uint64_t s = HLE_SYNC_FAULT(3) ? 57u : 58u;
    uint64_t b = (((uintptr_t)obj >> 4) * 0x9E3779B97F4A7C15ull) >> s;
    return 1ull << (b & 63u);
}

static inline unsigned hle_se_bucket(const void *ev) {
    uint64_t x = ((uintptr_t)ev >> 3) * 0x9E3779B97F4A7C15ull;
    return (unsigned)((x >> 58) & (HLE_SE_SETS - 1u));
}

static inline void hle_se_put(hle_se_idx *ix, void *ev, uint32_t wh, uint64_t id) {
    assert(ix != NULL && ev != NULL && wh != 0);
    hle_se_set *s = &ix->set[hle_se_bucket(ev)];
    for (unsigned w = 0; w < HLE_SE_WAYS; w++) {
        if (s->way[w].ev == ev || !s->way[w].ev) {
            s->way[w] = (hle_se_slot){ev, wh, 0, id};
            return;
        }
    }
    ix->overflow++;
}

static inline void hle_se_del_wh(hle_se_idx *ix, uint32_t wh) {
    assert(ix != NULL);
    if (!wh || HLE_SYNC_FAULT(4)) return;
    for (unsigned b = 0; b < HLE_SE_SETS; b++) {
        for (unsigned w = 0; w < HLE_SE_WAYS; w++) {
            if (ix->set[b].way[w].wh == wh) memset(&ix->set[b].way[w], 0, sizeof ix->set[b].way[w]);
        }
    }
}

static inline uint32_t hle_se_find_ev(const hle_se_idx *ix, const void *ev, uint64_t *id_out) {
    assert(ix != NULL && ev != NULL);
    const hle_se_set *s = &ix->set[hle_se_bucket(ev)];
    for (unsigned w = 0; w < HLE_SE_WAYS; w++) {
        if (s->way[w].ev == ev) {
            if (id_out) *id_out = s->way[w].id;
            return s->way[w].wh;
        }
    }
    return 0;
}

static inline uint32_t hle_se_find_id(const hle_se_idx *ix, uint64_t id) {
    assert(ix != NULL);
    if (!id) return 0;
    for (unsigned b = 0; b < HLE_SE_SETS; b++) {
        for (unsigned w = 0; w < HLE_SE_WAYS; w++) {
            if (ix->set[b].way[w].id == id && ix->set[b].way[w].wh) return ix->set[b].way[w].wh;
        }
    }
    return 0;
}

#endif
