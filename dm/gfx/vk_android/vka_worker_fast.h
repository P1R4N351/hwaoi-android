// Fast replay-worker queue/sync helpers (DM_VK_WORKER_FAST=1, default on; =0 disables).
// Portable ISO C11 (no Android headers) so host unit/ASan/TSan suites exercise the exact helpers.
#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vka_qsched.h"
#include "../nvn/nvn_obj.h"

enum {
    VKA_WORKER_INLINE_H = 16,
    VKA_WORKER_INLINE_WS = 8,
    VKA_WORKER_SLAB_CAP = 64,
    VKA_SUB_SLAB_MAGIC = 0x57333053u,
    VKA_SYNC_SCAN_MAX_STEPS = 1u << 22
};

_Static_assert(OP_COUNT_ <= 64, "NVN opcodes must fit in 64-bit sync-scan bitmask");
_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "64-bit host pointers");
#define VKA_SYNC_SCAN_MASK \
    ((UINT64_C(1) << OP_END) | (UINT64_C(1) << OP_JUMP) | (UINT64_C(1) << OP_CALL) | \
     (UINT64_C(1) << OP_FENCE_SYNC) | (UINT64_C(1) << OP_WAIT_SYNC) | \
     (UINT64_C(1) << OP_WAIT_EVENT) | (UINT64_C(1) << OP_SIGNAL_EVENT))

static _Atomic int g_vka_worker_fast_state;

static inline int vka_worker_fast_on(void) {
    int s = atomic_load_explicit(&g_vka_worker_fast_state, memory_order_acquire);
    if (!s) {
        const char *e = getenv("DM_VK_WORKER_FAST");
        s = (e && *e == '0' && !e[1]) ? 1 : 2;
        atomic_store_explicit(&g_vka_worker_fast_state, s, memory_order_release);
    }
    return s == 2;
}

#ifdef T_W30_TEST_HOOKS
static _Atomic int g_vka_w30_fault;
static inline void vka_worker_fast_reset_for_test(int forced) {
    atomic_store_explicit(&g_vka_worker_fast_state, forced ? 2 : 1, memory_order_release);
}
static inline void vka_w30_set_fault(int mode) {
    atomic_store_explicit(&g_vka_w30_fault, mode, memory_order_release);
}
static inline int vka_w30_fault_mode(void) {
    return atomic_load_explicit(&g_vka_w30_fault, memory_order_acquire);
}
#else
static inline int vka_w30_fault_mode(void) { return 0; }
#endif

typedef struct {
    const uint8_t **h;
    int nh;
    const uint8_t *h_in[VKA_WORKER_INLINE_H];
    qs_wsnap *ws;
    int nws;
    qs_wsnap ws_in[VKA_WORKER_INLINE_WS];
    int w0done, w0kind;
    volatile uint32_t *w0word;
    uint32_t w0mode, w0value;
    uint64_t w0fence;
} vka_job_buf;

static inline const uint8_t *vka_job_handle(const vka_job_buf *jb, int idx) {
    assert(jb != NULL && idx >= 0 && idx < jb->nh);
    return jb->h ? jb->h[idx] : jb->h_in[idx];
}

static inline int vka_job_set_handles(vka_job_buf *jb, const uint8_t *const *src, int nh, int fast) {
    assert(jb != NULL && nh >= 0);
    jb->nh = nh;
    if (fast && nh <= VKA_WORKER_INLINE_H) {
        jb->h = NULL;
        if (nh > 0) {
            assert(src != NULL);
            memcpy(jb->h_in, src, (size_t)nh * sizeof(src[0]));
        }
        return 0;
    }
    size_t cnt = nh ? (size_t)nh : 1u;
    const uint8_t **buf = (const uint8_t **)malloc(cnt * sizeof(buf[0]));
    if (!buf) return -1;
    if (nh > 0) {
        assert(src != NULL);
        memcpy(buf, src, (size_t)nh * sizeof(buf[0]));
    }
    jb->h = buf;
    return 0;
}

static inline int vka_job_push_ws(vka_job_buf *jb, int *cap, qs_wsnap snap, int fast) {
    assert(jb != NULL && cap != NULL && jb->nws >= 0);
    if (fast && !jb->ws) {
        if (jb->nws < VKA_WORKER_INLINE_WS) {
            jb->ws_in[jb->nws++] = snap;
            return 0;
        }
        qs_wsnap *buf = (qs_wsnap *)malloc(16u * sizeof(buf[0]));
        if (!buf) return -1;
        memcpy(buf, jb->ws_in, sizeof(jb->ws_in));
        if (vka_w30_fault_mode() == 4) buf[0].fence ^= UINT64_C(1);
        *cap = 16;
        jb->ws = buf;
        jb->ws[jb->nws++] = snap;
        return 0;
    }
    if (jb->nws == *cap) {
        int nc = *cap ? (*cap * 2) : 8;
        qs_wsnap *nb = (qs_wsnap *)realloc(jb->ws, (size_t)nc * sizeof(nb[0]));
        if (!nb) return -1;
        *cap = nc;
        jb->ws = nb;
    }
    jb->ws[jb->nws++] = snap;
    return 0;
}

static inline void vka_job_buf_free(vka_job_buf *jb) {
    if (!jb) return;
    free((void *)jb->h);
    free(jb->ws);
    jb->h = NULL;
    jb->ws = NULL;
    jb->nh = 0;
    jb->nws = 0;
}

typedef struct vka_sub_node {
    qs_sub sub;
    const uint8_t *h_in[VKA_WORKER_INLINE_H];
    qs_wsnap ws_in[VKA_WORKER_INLINE_WS];
    struct vka_sub_node *free_next;
    uint32_t slab_magic;
} vka_sub_node;

_Static_assert(__builtin_offsetof(vka_sub_node, sub) == 0, "qs_sub must be first member");

typedef struct {
    vka_sub_node *free_head;
    uint32_t nfree;
    uint64_t hits;
    uint64_t misses;
} vka_sub_pool;

static inline qs_sub *vka_sub_alloc(vka_sub_pool *pool) {
    if (pool && pool->free_head) {
        vka_sub_node *nd = pool->free_head;
        assert(nd->slab_magic == VKA_SUB_SLAB_MAGIC);
        pool->free_head = nd->free_next;
        pool->nfree--;
        pool->hits++;
        memset(&nd->sub, 0, sizeof(nd->sub));
        nd->free_next = NULL;
        return &nd->sub;
    }
    if (pool) pool->misses++;
    vka_sub_node *nd = (vka_sub_node *)calloc(1u, sizeof(*nd));
    if (!nd) return NULL;
    nd->slab_magic = VKA_SUB_SLAB_MAGIC;
    return &nd->sub;
}

static inline void vka_sub_bind_job(qs_sub *q, vka_job_buf *jb, int fast) {
    assert(q != NULL && jb != NULL);
    vka_sub_node *nd = (vka_sub_node *)q;
    if (jb->h) {
        q->h = jb->h;
    } else if (fast) {
        assert(nd->slab_magic == VKA_SUB_SLAB_MAGIC && jb->nh <= VKA_WORKER_INLINE_H);
        if (jb->nh > 0) memcpy(nd->h_in, jb->h_in, (size_t)jb->nh * sizeof(nd->h_in[0]));
        if (vka_w30_fault_mode() == 5 && jb->nh == VKA_WORKER_INLINE_H) nd->h_in[jb->nh - 1] = NULL;
        q->h = nd->h_in;
    } else {
        q->h = NULL;
    }
    if (jb->ws) {
        q->ws = jb->ws;
    } else if (fast && jb->nws > 0) {
        assert(nd->slab_magic == VKA_SUB_SLAB_MAGIC && jb->nws <= VKA_WORKER_INLINE_WS);
        memcpy(nd->ws_in, jb->ws_in, (size_t)jb->nws * sizeof(nd->ws_in[0]));
        q->ws = nd->ws_in;
    } else {
        q->ws = NULL;
    }
    q->nh = jb->nh;
    q->nws = jb->nws;
    q->w0kind = jb->w0kind;
    q->w0word = jb->w0word;
    q->w0mode = jb->w0mode;
    q->w0value = jb->w0value;
    q->w0fence = jb->w0fence;
    jb->h = NULL;
    jb->ws = NULL;
}

static inline void vka_sub_release(void *ctx, qs_sub *s) {
    if (!s) return;
    vka_sub_node *nd = (vka_sub_node *)s;
    assert(nd->slab_magic == VKA_SUB_SLAB_MAGIC);
    if (s->h && s->h != nd->h_in) free((void *)s->h);
    if (s->ws && s->ws != nd->ws_in) free(s->ws);
    s->h = NULL;
    s->ws = NULL;
    vka_sub_pool *pool = (vka_sub_pool *)ctx;
    if (pool && pool->nfree < VKA_WORKER_SLAB_CAP) {
        nd->free_next = pool->free_head;
        pool->free_head = nd;
        pool->nfree++;
        return;
    }
    free(nd);
}

static inline void vka_sub_pool_destroy(vka_sub_pool *pool) {
    if (!pool) return;
    uint32_t bound = pool->nfree + 1u;
    for (uint32_t i = 0u; i < bound && pool->free_head; i++) {
        vka_sub_node *nd = pool->free_head;
        pool->free_head = nd->free_next;
        free(nd);
    }
    pool->nfree = 0u;
    assert(pool->free_head == NULL);
}

static inline int vka_sync_pop(const uint8_t **pp, const uint8_t *const ret[16], int *sp) {
    while (*sp > 0) {
        const uint8_t *nxt = ret[--(*sp)];
        if (nxt) {
            *pp = nxt;
            return 1;
        }
    }
    return 0;
}

static inline void vka_sync_record_op(uint32_t op, const uint8_t *p, const void *a,
                                      uint64_t fence, vka_job_buf *jb, int *cap, int fast) {
    if (op == OP_FENCE_SYNC) {
        uint64_t raw = 0u;
        memcpy(&raw, a, sizeof raw);
        sync_t *s = (sync_t *)(uintptr_t)raw;
        if (s) { s->fence = fence; s->fenced = 1u; }
    } else if (op == OP_WAIT_SYNC && jb) {
        uint64_t raw = 0u;
        memcpy(&raw, a, sizeof raw);
        const sync_t *s = (const sync_t *)(uintptr_t)raw;
        uint64_t wf = (s && s->fenced) ? s->fence : 0u;
        int rc = vka_job_push_ws(jb, cap, (qs_wsnap){p, wf}, fast);
        assert(rc == 0);
        if (!jb->w0done) { jb->w0done = 1; jb->w0kind = 2; jb->w0fence = wf; }
    } else if (op == OP_WAIT_EVENT && jb && !jb->w0done) {
        p_wait_event w;
        memcpy(&w, a, sizeof w);
        jb->w0done = 1;
        if (w.val) { jb->w0kind = 1; jb->w0word = w.val; jb->w0mode = w.mode; jb->w0value = w.value; }
    } else if (op == OP_SIGNAL_EVENT && jb) {
        jb->w0done = 1;
    }
}

static inline void vka_worker_fence_syncs(const uint8_t *p, uint64_t fence,
                                          vka_job_buf *jb, int *cap, int fast) {
    assert(p != NULL);
    const uint8_t *ret[16];
    int sp = 0;
    int fmode = vka_w30_fault_mode();
    int max_call = (fast && fmode == 3) ? 15 : 16;
    uint64_t mask = VKA_SYNC_SCAN_MASK;
    if (fast && fmode == 1) mask &= ~(UINT64_C(1) << OP_FENCE_SYNC);
    if (fast && fmode == 2) mask &= ~(UINT64_C(1) << OP_SIGNAL_EVENT);
    for (uint32_t step = 0u; step < VKA_SYNC_SCAN_MAX_STEPS; step++) {
        op_hdr h;
        memcpy(&h, p, sizeof h);
        if (fast && (h.op >= 64u || !((mask >> h.op) & UINT64_C(1)))) {
            if (h.size < 8u) { if (!vka_sync_pop(&p, ret, &sp)) return; continue; }
            p += h.size;
            continue;
        }
        const void *a = p + 8;
        if (h.op == OP_END) { if (!vka_sync_pop(&p, ret, &sp)) return; continue; }
        if (h.op == OP_JUMP) { uint64_t j = 0u; memcpy(&j, a, sizeof j); p = (const uint8_t *)(uintptr_t)j; continue; }
        if (h.op == OP_CALL && sp < max_call) {
            uint64_t t = 0u;
            memcpy(&t, a, sizeof t);
            ret[sp++] = h.size >= 8u ? (p + h.size) : NULL;
            p = (const uint8_t *)(uintptr_t)t;
            continue;
        }
        vka_sync_record_op(h.op, p, a, fence, jb, cap, fast);
        if (h.size < 8u) { if (!vka_sync_pop(&p, ret, &sp)) return; continue; }
        p += h.size;
    }
    assert(0 && "command list exceeded VKA_SYNC_SCAN_MAX_STEPS");
}

typedef struct {
    uint32_t worker_idle_wait;
    uint32_t worker_parked_wait;
    uint32_t prod_waiters;
    uint64_t signals;
    uint64_t broadcasts;
    uint64_t elided;
} vka_worker_wait_state;

static inline void vka_worker_wake_on_push(vka_worker_wait_state *ws, pthread_cond_t *cv, int fast) {
    assert(ws != NULL && cv != NULL);
    if (!fast) { ws->broadcasts++; pthread_cond_broadcast(cv); return; }
    if (ws->worker_idle_wait + ws->worker_parked_wait == 0u) { ws->elided++; return; }
    if (ws->prod_waiters == 0u) { ws->signals++; pthread_cond_signal(cv); return; }
    ws->broadcasts++;
    pthread_cond_broadcast(cv);
}

static inline void vka_worker_wake_on_done(vka_worker_wait_state *ws, pthread_cond_t *cv, int fast) {
    assert(ws != NULL && cv != NULL);
    if (!fast) { ws->broadcasts++; pthread_cond_broadcast(cv); return; }
    if (ws->prod_waiters == 0u) { ws->elided++; return; }
    ws->broadcasts++;
    pthread_cond_broadcast(cv);
}

static inline void vka_worker_wake_on_kick(vka_worker_wait_state *ws, pthread_cond_t *cv, int fast) {
    assert(ws != NULL && cv != NULL);
    if (!fast) { ws->broadcasts++; pthread_cond_broadcast(cv); return; }
    if (ws->worker_parked_wait == 0u) { ws->elided++; return; }
    if (ws->prod_waiters == 0u) { ws->signals++; pthread_cond_signal(cv); return; }
    ws->broadcasts++;
    pthread_cond_broadcast(cv);
}
