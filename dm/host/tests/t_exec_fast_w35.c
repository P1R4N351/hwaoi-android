// Unit, parity, TSan & benchmark harness for Wave 35 replay exec fast paths.
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    OP_END, OP_JUMP, OP_BEGIN, OP_CALL,
    OP_SIGNAL_EVENT, OP_REPORT_COUNTER, OP_COPY_B2B, OP_UPDATE_UB,
    OP_SET_RT, OP_CLEAR_COLOR, OP_CLEAR_DS, OP_VIEWPORTS, OP_SCISSOR, OP_DEPTH_RANGE,
    OP_BIND_PROGRAM, OP_BIND_STATE, OP_BIND_VATTR, OP_BIND_VSTREAM, OP_BIND_VBUF,
    OP_BIND_UBO, OP_BIND_SSBO, OP_BIND_TEX, OP_BIND_SAMP,
    OP_STENCIL, OP_POLY_OFFSET, OP_DRAW, OP_DRAW_IDX, OP_DISPATCH, OP_BARRIER,
    OP_SET_TEX_POOL, OP_SET_SAMP_POOL, OP_COPY_B2T, OP_COPY_STRIDE, OP_BLEND_COLOR,
    OP_COPY_T2B, OP_BIND_IMG, OP_COPY_T2T, OP_RESET_COUNTER, OP_CLEAR_TEX, OP_CLEAR_BUF,
    OP_DEPTH_CLAMP, OP_RENDER_COND, OP_FENCE_SYNC, OP_TF_BIND, OP_TF_STATE,
    OP_RAST_DISCARD, OP_VP_SWIZZLE, OP_BIND_TEX1, OP_PRIM_RESTART, OP_BIND_UBO_SMALL,
    OP_BIND_VBUF1, OP_DRAW_TEX, OP_ALPHA_REF, OP_PUSH_CONST,
    OP_WAIT_EVENT, OP_WAIT_SYNC, OP_DLSS, OP_COUNT_
};
#define VKA_EXEC_HAVE_OPS 1
#include "../../gfx/vk_android/vka_exec_fast.h"

_Atomic int g_vka_exec_fast_state;
_Atomic int g_vka_w35_fault;

static int w35_legacy_memu_allowed(uint32_t op) {
    switch (op) {
    case OP_END: case OP_JUMP: case OP_BEGIN: case OP_CALL: case OP_DRAW: case OP_DRAW_IDX: case OP_SCISSOR: case OP_COPY_STRIDE: case OP_CLEAR_COLOR: case OP_CLEAR_DS:
    case OP_REPORT_COUNTER: case OP_RESET_COUNTER: case OP_FENCE_SYNC: case OP_VIEWPORTS: case OP_DEPTH_RANGE: case OP_BIND_PROGRAM: case OP_BIND_STATE: case OP_BIND_VATTR:
    case OP_BIND_VSTREAM: case OP_BIND_VBUF: case OP_BIND_VBUF1: case OP_BIND_UBO: case OP_BIND_UBO_SMALL: case OP_BIND_SSBO: case OP_BIND_TEX: case OP_BIND_TEX1: case OP_BIND_SAMP:
    case OP_BIND_IMG: case OP_STENCIL: case OP_POLY_OFFSET: case OP_SET_TEX_POOL: case OP_SET_SAMP_POOL: case OP_BLEND_COLOR: case OP_DEPTH_CLAMP: case OP_TF_BIND: case OP_TF_STATE:
    case OP_RAST_DISCARD: case OP_VP_SWIZZLE: case OP_PRIM_RESTART: case OP_ALPHA_REF: case OP_PUSH_CONST: return 1;
    default: return 0;
    }
}

static int w35_legacy_settle(uint32_t op) {
    switch (op) {
    case OP_DRAW: case OP_DRAW_IDX: case OP_DRAW_TEX: case OP_CLEAR_COLOR: case OP_CLEAR_DS: case OP_CLEAR_TEX: case OP_CLEAR_BUF: case OP_COPY_B2B: case OP_COPY_B2T:
    case OP_COPY_T2B: case OP_COPY_T2T: case OP_UPDATE_UB: case OP_REPORT_COUNTER: return 1;
    default: return 0;
    }
}

static int w35_legacy_rp_nop(uint32_t op) {
    switch (op) {
    case OP_END: case OP_BEGIN: case OP_JUMP: case OP_CALL: case OP_SIGNAL_EVENT: case OP_REPORT_COUNTER: case OP_FENCE_SYNC: case OP_RENDER_COND: case OP_RESET_COUNTER: case OP_WAIT_EVENT: case OP_WAIT_SYNC:
    case OP_SCISSOR: case OP_COPY_STRIDE: case OP_UPDATE_UB: case OP_DRAW: case OP_DRAW_IDX: case OP_CLEAR_COLOR: case OP_CLEAR_DS: return 1;
    default: return 0;
    }
}

static int w35_legacy_ind_write(uint32_t op) {
    return op == OP_DISPATCH || op == OP_COPY_B2B || op == OP_CLEAR_BUF || op == OP_COPY_T2B || op == OP_UPDATE_UB;
}

static vka_rc_ent_w35 *w35_legacy_find(vka_rc_ent_w35 *tab, uint32_t cap, uint64_t a) {
    if (!cap || !a || !tab) return NULL;
    for (uint32_t i = (uint32_t)(a >> 4) & (cap - 1u), st = 0; st < cap; st++, i = (i + 1u) & (cap - 1u)) {
        if (tab[i].addr == a) return &tab[i];
        if (!tab[i].addr) return NULL;
    }
    return NULL;
}

static void w35_legacy_note(vka_rc_ent_w35 **tab, uint32_t *cap, uint32_t *cnt, uint64_t a, uint64_t sub, uint32_t kind) {
    vka_rc_ent_w35 *e = w35_legacy_find(*tab, *cap, a);
    if (!e) {
        if ((*cnt + 1u) * 2u > *cap) {
            uint32_t nc = *cap ? *cap * 2u : 4096u;
            vka_rc_ent_w35 *t = (vka_rc_ent_w35 *)calloc(nc, sizeof *t);
            if (!t) abort();
            for (uint32_t k = 0; k < *cap; k++) if ((*tab)[k].addr) {
                uint32_t i = (uint32_t)((*tab)[k].addr >> 4) & (nc - 1u);
                while (t[i].addr) i = (i + 1u) & (nc - 1u);
                t[i] = (*tab)[k];
            }
            free(*tab); *tab = t; *cap = nc;
        }
        if (!*tab || !*cap) abort();
        uint32_t i = (uint32_t)(a >> 4) & (*cap - 1u); while ((*tab)[i].addr) i = (i + 1u) & (*cap - 1u);
        e = &(*tab)[i]; e->addr = a; (*cnt)++;
    }
    e->sub = sub; e->kind = kind;
}

static uint64_t w35_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int w35_slow_env_zero(void) { return 0; }
static int w35_slow_env_one(void) { return 1; }

static void w35_check_parity(void) {
    for (uint32_t op = 0; op <= (uint32_t)OP_COUNT_ + 8u; op++) {
        assert(vka_op_memu_allowed_fast(op, 0) == w35_legacy_memu_allowed(op));
        assert(vka_op_need_settle_fast(op, 0) == w35_legacy_settle(op));
        assert(vka_op_rp_nop_fast(op, 0) == w35_legacy_rp_nop(op));
        assert(vka_op_ind_write_fast(op) == w35_legacy_ind_write(op));
    }
    assert(vka_exec_idx_skip_pre(0, 0, 0) == 0 && vka_exec_idx_skip_pre(0, 1, 0) == 0);
    assert(vka_exec_idx_skip_pre(1, 1, 0) == 1 && vka_exec_idx_skip_pre(1, 0, 0) == 2);
    assert(vka_rc_qmem_sync_on(1u, w35_slow_env_zero) == 0);
    assert(vka_rc_qmem_sync_on(2u, w35_slow_env_one) == 1);
    assert(vka_rc_qmem_sync_on(2u, w35_slow_env_zero) == 1);
    vka_rc_ent_w35 *t0 = NULL, *t1 = NULL; uint32_t c0 = 0, n0 = 0, c1 = 0, n1 = 0;
    vka_rcm_front64 fc = {0}; int oom = 0;
    for (uint64_t i = 1u; i <= 3000u; i++) {
        uint64_t a = UINT64_C(0x800000000) + (i << 16);
        w35_legacy_note(&t0, &c0, &n0, a, i * 3u, (uint32_t)((i % 3u) + 1u));
        assert(vka_rcm_note_fast(&t1, &c1, &n1, 1u << 20, &fc, a, i * 3u, (uint32_t)((i % 3u) + 1u), 0, &oom) != NULL);
        assert(!oom);
    }
    assert(c0 == 8192u && c1 == 8192u && n0 == n1);
    for (uint64_t i = 1u; i <= 3000u; i++) {
        uint64_t a = UINT64_C(0x800000000) + (i << 16);
        const vka_rc_ent_w35 *e0 = w35_legacy_find(t0, c0, a), *e1 = vka_rcm_find_fast(t1, c1, &fc, a, 0);
        assert(e0 && e1 && e0->sub == e1->sub && e0->kind == e1->kind);
    }
    assert(vka_rcm_find_fast(t1, c1, &fc, UINT64_C(0x12340010), 0) == NULL);
    free(t0); free(t1);
}

static void w35_check_fault(int fm) {
    vka_w35_set_fault(fm);
    if (fm == 1) {
        vka_rc_ent_w35 *t = NULL; uint32_t c = 0, n = 0; vka_rcm_front64 fc = {0}; int oom = 0;
        uint64_t a1 = UINT64_C(0x10000010);
        for (uint32_t s = 0; s < 64u && !(vka_rcm_hash64(a1) & 4096u); s++) a1 += 16u;
        assert((vka_rcm_hash64(a1) & 4096u) != 0u);
        assert(vka_rcm_note_fast(&t, &c, &n, 1u << 20, &fc, a1, 1u, 1u, fm, &oom) != NULL);
        for (uint64_t i = 1u; i < 2048u; i++) {
            uint64_t a = UINT64_C(0x20000000) + (i << 4);
            assert(vka_rcm_note_fast(&t, &c, &n, 1u << 20, &fc, a, i + 1u, 1u, fm, &oom) != NULL);
        }
        assert(c == 4096u && n == 2048u);
        assert(vka_rcm_find_fast(t, c, &fc, a1, 0) != NULL);
        uint64_t ag = UINT64_C(0x30000000);
        while ((vka_rcm_hash64(ag) & 63u) == (vka_rcm_hash64(a1) & 63u)) ag += 16u;
        assert(vka_rcm_note_fast(&t, &c, &n, 1u << 20, &fc, ag, 9999u, 1u, fm, &oom) != NULL);
        assert(c == 8192u);
        const vka_rc_ent_w35 *e = vka_rcm_find_fast(t, c, &fc, a1, fm);
        assert(e && e->addr == a1 && e->sub == 1u);
        free(t);
    } else if (fm == 2) {
        assert(vka_op_need_settle_fast(OP_COPY_T2B, fm) == 1);
    } else if (fm == 3) {
        assert(vka_op_memu_allowed_fast(OP_DISPATCH, fm) == 0);
    } else if (fm == 4) {
        assert(vka_op_rp_nop_fast(OP_SET_RT, fm) == 0);
    } else if (fm == 5) {
        assert(vka_exec_idx_skip_pre(0, 1, fm) == 0);
    }
    vka_w35_set_fault(0);
}

static uint64_t w35_bench_once(const uint32_t *ops, size_t nops, int fast) {
    vka_rc_ent_w35 *tab = NULL; uint32_t cap = 0, cnt = 0; vka_rcm_front64 fc = {0}; int oom = 0;
    for (uint64_t i = 1u; i <= 2600u; i++) {
        uint64_t a = UINT64_C(0x200000000) + (i << 16);
        if (fast) vka_rcm_note_fast(&tab, &cap, &cnt, 1u << 20, &fc, a, i, (uint32_t)((i % 3u) + 1u), 0, &oom);
        else w35_legacy_note(&tab, &cap, &cnt, a, i, (uint32_t)((i % 3u) + 1u));
    }
    uint64_t sink = 0;
    for (uint32_t r = 0; r < 60u; r++) {
        for (size_t k = 0; k < nops; k++) {
            uint32_t op = ops[k];
            if (fast) {
                sink += (uint64_t)vka_op_need_settle_fast(op, 0) + (uint64_t)vka_op_rp_nop_fast(op, 0) +
                        (uint64_t)vka_op_memu_allowed_fast(op, 0) + (uint64_t)vka_op_ind_write_fast(op);
            } else {
                sink += (uint64_t)w35_legacy_settle(op) + (uint64_t)w35_legacy_rp_nop(op) +
                        (uint64_t)w35_legacy_memu_allowed(op) + (uint64_t)w35_legacy_ind_write(op);
            }
            if ((k & 15u) == 0u) {
                uint64_t a = UINT64_C(0x200000000) + (UINT64_C(2500) << 16);
                const vka_rc_ent_w35 *e = fast ? vka_rcm_find_fast(tab, cap, &fc, a, 0) : w35_legacy_find(tab, cap, a);
                if (e) sink += e->sub + e->kind;
            }
        }
    }
    free(tab);
    return sink;
}

static void *w35_tsan_worker(void *arg) {
    uint64_t id = (uint64_t)(uintptr_t)arg;
    vka_rc_ent_w35 *tab = NULL; uint32_t cap = 0, cnt = 0; vka_rcm_front64 fc = {0}; int oom = 0;
    for (uint64_t i = 1u; i <= 64u; i++) {
        vka_rcm_note_fast(&tab, &cap, &cnt, 1u << 20, &fc, UINT64_C(0x40000000) + ((i + id) << 12), i, 1u, 0, &oom);
    }
    uint64_t s = 0;
    for (int i = 0; i < 4000; i++) {
        s += (uint64_t)vka_exec_fast_on() + (uint64_t)vka_rc_qmem_sync_on(10u, w35_slow_env_zero);
        s += (uint64_t)vka_op_memu_allowed_fast(OP_DRAW_IDX, 0);
    }
    free(tab);
    return (void *)(uintptr_t)s;
}

int main(void) {
    const char *env = getenv("DM_VK_EXEC_FAST");
    const char *flt = getenv("T_W35_FAULT");
    int init_on = vka_exec_fast_on();
    if (env && !strcmp(env, "0")) assert(!init_on); else assert(init_on);
    w35_check_parity();
    if (flt && atoi(flt) > 0) w35_check_fault(atoi(flt));
    pthread_t th[4];
    for (int i = 0; i < 4; i++) assert(pthread_create(&th[i], NULL, w35_tsan_worker, (void *)(uintptr_t)i) == 0);
    for (int i = 0; i < 4; i++) { void *r = NULL; assert(pthread_join(th[i], &r) == 0); }

    uint32_t *ops = (uint32_t *)calloc(4096u, sizeof *ops);
    if (!ops) abort();
    for (size_t i = 0; i < 4096u; i++) ops[i] = (uint32_t)(i % (size_t)OP_COUNT_);
    uint64_t t0 = w35_ns(), s0 = w35_bench_once(ops, 4096u, 0); uint64_t off_ns = w35_ns() - t0;
    uint64_t t1 = w35_ns(), s1 = w35_bench_once(ops, 4096u, 1); uint64_t on_ns = w35_ns() - t1;
    assert(s0 == s1);
    printf("BENCH aoi_w35_off exec=%.3f ms sink=%llu\n", off_ns / 1e6, (unsigned long long)s0);
    printf("BENCH aoi_w35_on exec=%.3f ms sink=%llu\n", on_ns / 1e6, (unsigned long long)s1);
    free(ops);
    return 0;
}
