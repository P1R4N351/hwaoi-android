// Wave 30 unit, ASan/UBSan, TSan, fault-injection, and microbenchmark test for vka_worker_fast.h + vka_qsched.h.
#include "../../gfx/vk_android/vka_worker_fast.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { W30_LIST_BYTES = 16384, W30_CALL_DEPTH = 18 };
typedef struct { uint8_t b[W30_LIST_BYTES]; size_t n; } w30_list;

static uint64_t w30_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static uint8_t *w30_put(w30_list *l, uint32_t op, const void *pl, uint32_t plen) {
    uint32_t sz = 8u + ((plen + 7u) & ~7u);
    assert(l->n + sz <= sizeof(l->b));
    uint8_t *r = l->b + l->n;
    op_hdr h = {op, sz};
    memcpy(r, &h, sizeof h);
    if (plen > 0u && pl) memcpy(r + 8, pl, plen);
    l->n += sz;
    return r;
}

static qs g_w30_qs;
static uint64_t w30_q_done_cb(void *ctx) { (void)ctx; return 0u; }
static uint64_t w30_q_now_cb(void *ctx) { (void)ctx; return 1000u; }
static int w30_q_run_cb(void *ctx, qs_sub *s) { (void)ctx; qs_commit(&g_w30_qs, s->fence, 1); return 0; }

static void w30_build_scan_lists(w30_list *main_l, w30_list *jmp_l, w30_list sub_l[W30_CALL_DEPTH],
                                 sync_t *syn_f, sync_t *syn_w0, sync_t *syn_w1,
                                 volatile uint32_t *ev_w, const uint8_t *wptr[12]) {
    const uint8_t *dummy = NULL;
    w30_put(main_l, OP_DRAW, &dummy, 8u);
    w30_put(main_l, OP_BIND_TEX, &dummy, 8u);
    w30_put(main_l, OP_PUSH_CONST, &dummy, 8u);
    w30_put(main_l, OP_FENCE_SYNC, &syn_f, 8u);
    wptr[0] = w30_put(main_l, OP_WAIT_SYNC, &syn_w0, 8u);
    for (int d = W30_CALL_DEPTH - 1; d >= 0; d--) {
        uint32_t tag = (uint32_t)d;
        w30_put(&sub_l[d], OP_DRAW_IDX, &tag, sizeof tag);
        if (d == 15) wptr[1] = w30_put(&sub_l[d], OP_WAIT_SYNC, &syn_f, 8u);
        if (d == 16) w30_put(&sub_l[d], OP_FENCE_SYNC, &syn_w1, 8u);
        if (d + 1 < W30_CALL_DEPTH) {
            uint64_t nxt = (uint64_t)(uintptr_t)sub_l[d + 1].b;
            w30_put(&sub_l[d], OP_CALL, &nxt, 8u);
        }
        w30_put(&sub_l[d], OP_END, NULL, 0u);
    }
    uint64_t call0 = (uint64_t)(uintptr_t)sub_l[0].b;
    w30_put(main_l, OP_CALL, &call0, 8u);
    w30_put(main_l, OP_SIGNAL_EVENT, &ev_w, 8u);
    p_wait_event evp = {ev_w, 1u, 9u};
    w30_put(main_l, OP_WAIT_EVENT, &evp, sizeof evp);
    uint64_t jtgt = (uint64_t)(uintptr_t)jmp_l->b;
    w30_put(main_l, OP_JUMP, &jtgt, 8u);
    for (int i = 0; i < 8; i++) wptr[2 + i] = w30_put(jmp_l, OP_WAIT_SYNC, &syn_w0, 8u);
    w30_put(jmp_l, OP_END, NULL, 0u);
}

static void w30_verify_signal_before_wait(void) {
    w30_list sig_l = {0};
    volatile uint32_t ev_w = 0u;
    uint64_t ev_ptr = (uint64_t)(uintptr_t)&ev_w;
    w30_put(&sig_l, OP_DRAW, &ev_ptr, 8u);
    w30_put(&sig_l, OP_SIGNAL_EVENT, &ev_ptr, 8u);
    p_wait_event evp = {&ev_w, 0u, 3u};
    w30_put(&sig_l, OP_WAIT_EVENT, &evp, sizeof evp);
    w30_put(&sig_l, OP_END, NULL, 0u);
    const uint8_t *hdl[1] = {sig_l.b};
    vka_job_buf slow = {0}, fast = {0};
    int cap_s = 0, cap_f = 0;
    assert(vka_job_set_handles(&slow, hdl, 1, 0) == 0);
    vka_worker_fence_syncs(vka_job_handle(&slow, 0), 11u, &slow, &cap_s, 0);
    assert(vka_job_set_handles(&fast, hdl, 1, 1) == 0);
    vka_worker_fence_syncs(vka_job_handle(&fast, 0), 11u, &fast, &cap_f, 1);
    assert(slow.w0done == 1 && slow.w0kind == 0);
    assert(fast.w0done == slow.w0done && fast.w0kind == slow.w0kind);
    vka_job_buf_free(&slow);
    vka_job_buf_free(&fast);
}

static void w30_verify_scan_and_inline_parity(void) {
    w30_list main_l = {0}, jmp_l = {0}, sub_l[W30_CALL_DEPTH];
    memset(sub_l, 0, sizeof sub_l);
    sync_t syn_f = {0}, syn_w0 = {.fenced = 1u, .fence = 41u}, syn_w1 = {.fenced = 1u, .fence = 77u};
    volatile uint32_t ev_w = 0u;
    const uint8_t *wptr[12];
    w30_build_scan_lists(&main_l, &jmp_l, sub_l, &syn_f, &syn_w0, &syn_w1, &ev_w, wptr);
    w30_verify_signal_before_wait();

    const uint8_t *handles[24];
    for (int i = 0; i < 24; i++) handles[i] = main_l.b;
    int ncases[5] = {0, 1, VKA_WORKER_INLINE_H, VKA_WORKER_INLINE_H + 1, 24};
    for (int ci = 0; ci < 5; ci++) {
        int nh = ncases[ci], cap_s = 0, cap_f = 0;
        vka_job_buf slow = {0}, fast = {0};
        syn_f.fence = 0u; syn_f.fenced = 0u;
        assert(vka_job_set_handles(&slow, handles, nh, 0) == 0);
        for (int i = 0; i < nh; i++) vka_worker_fence_syncs(vka_job_handle(&slow, i), 99u, &slow, &cap_s, 0);
        assert(syn_w1.fence == 77u);
        syn_f.fence = 0u; syn_f.fenced = 0u;
        assert(vka_job_set_handles(&fast, handles, nh, 1) == 0);
        assert((fast.h == NULL) == (nh <= VKA_WORKER_INLINE_H));
        for (int i = 0; i < nh; i++) vka_worker_fence_syncs(vka_job_handle(&fast, i), 99u, &fast, &cap_f, 1);
        assert(slow.nws == fast.nws && slow.w0done == fast.w0done && slow.w0kind == fast.w0kind && slow.w0fence == fast.w0fence);
        for (int i = 0; i < nh; i++) assert(vka_job_handle(&slow, i) == vka_job_handle(&fast, i));
        const qs_wsnap *sw = slow.ws, *fw = fast.ws ? fast.ws : fast.ws_in;
        if (slow.nws > 0) assert(memcmp(sw, fw, (size_t)slow.nws * sizeof(sw[0])) == 0);
        assert((nh == 0) || syn_f.fenced == 1u);
        if (nh >= 1) assert(wptr[0] == sw[0].rec && wptr[1] == sw[1].rec && sw[1].fence == 99u);
        vka_job_buf_free(&slow);
        vka_job_buf_free(&fast);
    }
}

static void w30_verify_qsched_slab(void) {
    w30_list lst = {0};
    sync_t sw = {.fenced = 0u, .fence = 55u};
    const sync_t *psw = &sw;
    w30_put(&lst, OP_WAIT_SYNC, &psw, 8u);
    w30_put(&lst, OP_END, NULL, 0u);
    const uint8_t *hdls[VKA_WORKER_INLINE_H];
    for (int i = 0; i < VKA_WORKER_INLINE_H; i++) hdls[i] = lst.b;

    vka_sub_pool pool = {0};
    memset(&g_w30_qs, 0, sizeof g_w30_qs);
    g_w30_qs.run = w30_q_run_cb; g_w30_qs.done = w30_q_done_cb; g_w30_qs.now = w30_q_now_cb;
    g_w30_qs.free_sub = vka_sub_release; g_w30_qs.ctx = &pool;
    for (uint64_t it = 0u; it < 200u; it++) {
        sw.fenced = (it >= 60u) ? 1u : 0u;
        vka_job_buf jb = {0};
        int cap = 0;
        int nh = (it & 1u) ? VKA_WORKER_INLINE_H : 4;
        assert(vka_job_set_handles(&jb, hdls, nh, 1) == 0);
        vka_worker_fence_syncs(vka_job_handle(&jb, 0), it + 1u, &jb, &cap, 1);
        qs_sub *q = vka_sub_alloc(&pool);
        assert(q != NULL);
        vka_sub_bind_job(q, &jb, 1);
        q->serial = 1u; q->fence = it + 1u;
        assert(q->nh == nh && q->h[nh - 1] == lst.b && q->ws != NULL && q->ws[0].fence == (it >= 60u ? 55u : 0u));
        qs_submit(&g_w30_qs, q);
        vka_job_buf_free(&jb);
    }
    assert(pool.misses == 1u && pool.hits == 199u);
    vka_sub_pool_destroy(&pool);
    qs_ev_free(&g_w30_qs);
}

enum { W30_QCAP = 64, W30_NJ = 2000 };
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    vka_worker_wait_state ws;
    uint64_t q[W30_QCAP];
    uint64_t qin, qout, sum, done;
    int kick, parked, fast;
} w30_ring;

static void *w30_worker_fn(void *arg) {
    w30_ring *r = (w30_ring *)arg;
    for (;;) {
        pthread_mutex_lock(&r->mu);
        uint64_t v = 0u; int have = 0;
        for (;;) {
            if (r->qout != r->qin) { v = r->q[r->qout % W30_QCAP]; have = 1; break; }
            if (r->done) { pthread_mutex_unlock(&r->mu); return NULL; }
            if (!r->parked) { r->ws.worker_idle_wait++; pthread_cond_wait(&r->cv, &r->mu); r->ws.worker_idle_wait--; continue; }
            if (r->kick) break;
            struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl); dl.tv_nsec += 1000000L;
            if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
            r->ws.worker_parked_wait++; pthread_cond_timedwait(&r->cv, &r->mu, &dl); r->ws.worker_parked_wait--;
            break;
        }
        r->kick = 0;
        pthread_mutex_unlock(&r->mu);
        if (!have) continue;
        r->sum += v;
        pthread_mutex_lock(&r->mu);
        r->qout++;
        vka_worker_wake_on_done(&r->ws, &r->cv, r->fast);
        pthread_mutex_unlock(&r->mu);
    }
}

static void w30_verify_wait_ring(int fast) {
    w30_ring r = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .fast = fast};
    pthread_t th;
    assert(pthread_create(&th, NULL, w30_worker_fn, &r) == 0);
    for (uint64_t i = 1u; i <= W30_NJ; i++) {
        pthread_mutex_lock(&r.mu);
        while (r.qin - r.qout >= W30_QCAP) { r.ws.prod_waiters++; pthread_cond_wait(&r.cv, &r.mu); r.ws.prod_waiters--; }
        r.q[r.qin % W30_QCAP] = i;
        r.qin++;
        vka_worker_wake_on_push(&r.ws, &r.cv, fast);
        r.kick = 1;
        vka_worker_wake_on_kick(&r.ws, &r.cv, fast);
        pthread_mutex_unlock(&r.mu);
    }
    pthread_mutex_lock(&r.mu);
    r.done = 1u;
    r.kick = 1;
    vka_worker_wake_on_push(&r.ws, &r.cv, fast);
    pthread_cond_broadcast(&r.cv);
    pthread_mutex_unlock(&r.mu);
    assert(pthread_join(th, NULL) == 0);
    assert(r.sum == (uint64_t)W30_NJ * (W30_NJ + 1u) / 2u);
    assert(fast ? (r.ws.elided > 0u) : (r.ws.broadcasts > 0u));
}

static void w30_run_bench(void) {
    static w30_list lst;
    const uint64_t dummy = 0u;
    sync_t syn_f = {0}, syn_w = {0};
    sync_t *pf = &syn_f; const sync_t *pw = &syn_w;
    for (int i = 0; i < 120; i++) w30_put(&lst, (uint32_t)(OP_DRAW + (i & 3)), &dummy, 8u);
    w30_put(&lst, OP_FENCE_SYNC, &pf, 8u);
    w30_put(&lst, OP_WAIT_SYNC, &pw, 8u);
    w30_put(&lst, OP_END, NULL, 0u);
    const uint8_t *hdls[4] = {lst.b, lst.b, lst.b, lst.b};
    for (int fast = 0; fast < 2; fast++) {
        vka_worker_fast_reset_for_test(fast);
        vka_sub_pool pool = {0};
        memset(&g_w30_qs, 0, sizeof g_w30_qs);
        g_w30_qs.run = w30_q_run_cb; g_w30_qs.done = w30_q_done_cb; g_w30_qs.now = w30_q_now_cb;
        g_w30_qs.free_sub = fast ? vka_sub_release : NULL; g_w30_qs.ctx = &pool;
        pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
        vka_worker_wait_state ws = {0};
        volatile uint64_t sink = 0u;
        uint64_t t0 = w30_now_ns();
        for (uint64_t it = 0u; it < 20000u; it++) {
            vka_job_buf jb = {0}; int cap = 0;
            assert(vka_job_set_handles(&jb, hdls, 4, fast) == 0);
            for (int h = 0; h < 4; h++) vka_worker_fence_syncs(vka_job_handle(&jb, h), it + 1u, &jb, &cap, fast);
            pthread_mutex_lock(&mu);
            vka_worker_wake_on_push(&ws, &cv, fast);
            pthread_mutex_unlock(&mu);
            qs_sub *q = fast ? vka_sub_alloc(&pool) : (qs_sub *)calloc(1u, sizeof(*q));
            assert(q != NULL);
            vka_sub_bind_job(q, &jb, fast);
            q->serial = 1u; q->fence = it + 1u;
            sink += (uint64_t)q->nws;
            qs_submit(&g_w30_qs, q);
            pthread_mutex_lock(&mu);
            vka_worker_wake_on_done(&ws, &cv, fast);
            pthread_mutex_unlock(&mu);
            vka_job_buf_free(&jb);
        }
        double ms = (double)(w30_now_ns() - t0) / 1e6;
        vka_sub_pool_destroy(&pool);
        qs_ev_free(&g_w30_qs);
        printf("BENCH aoi_w30_%s worker_sub_20k=%.3f ms slab_hits=%llu cond_elided=%llu sink=%llu\n",
               fast ? "on" : "off", ms, (unsigned long long)pool.hits, (unsigned long long)ws.elided, (unsigned long long)sink);
    }
}

int main(void) {
    const char *fs = getenv("T_W30_FAULT");
    int f = fs ? atoi(fs) : 0;
    int fast_env = vka_worker_fast_on();
    if (f) vka_worker_fast_reset_for_test(1);
    vka_w30_set_fault(f);
    w30_verify_scan_and_inline_parity();
    w30_verify_qsched_slab();
    if (!f) {
        w30_verify_wait_ring(fast_env);
        w30_run_bench();
    }
    return 0;
}
