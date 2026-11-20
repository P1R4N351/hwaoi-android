// Standalone host unit, benchmark, and Delta #27 fault-injection harness for vka_tex_fast.h (DM_VK_TEX_FAST=1).
#define _POSIX_C_SOURCE 200809L
#include "../vk_android/vka_tex_fast.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int g_w7_fault = 0;

static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) abort();
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct { int id; uint8_t dim; uint32_t ver, gen; void *im; void *v; } leg_vm;
typedef struct { int id; uint32_t ver; void *s; } leg_sm;
typedef struct { int id; uint32_t ver; int cls; } leg_cm;

static leg_vm g_leg_vm[2048];
static leg_sm g_leg_sm[512];
static leg_cm g_leg_cm[2048];
static uint32_t g_ver[8192];
static pthread_mutex_t g_sim_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_slow_sink;

static void *sim_resolve(uintptr_t tag, uint32_t ver) {
    pthread_mutex_lock(&g_sim_mu);
    g_slow_sink += tag ^ ver;
    pthread_mutex_unlock(&g_sim_mu);
    return (void *)(tag | ((uintptr_t)(ver + 1u) << 32));
}

static int test_vmemo4_correctness(void) {
    vka_vmemo4 *vm = calloc(1, sizeof *vm);
    assert(vm != NULL);
    int ids[4] = { 64, 64 + 2048, 64 + 4096, 64 + 6144 };
    void *ims[4], *vws[4];
    for (int i = 0; i < 4; i++) {
        ims[i] = (void *)(uintptr_t)(0x10000u + (unsigned)ids[i]);
        vws[i] = (void *)(uintptr_t)(0x20000u + (unsigned)ids[i]);
        vka_vmemo4_put(vm, ids[i], 1, 10, 5, ims[i], vws[i]);
    }
    for (int r = 0; r < 8; r++) {
        int idx = r & 3;
        const vka_vm_ent *e = vka_vmemo4_find(vm, ids[idx], 1, 10, 5);
        if (!e || e->im != ims[idx] || e->v != vws[idx]) { free(vm); return 1; }
    }
    void *v_arr = (void *)(uintptr_t)0x90001u;
    vka_vmemo4_put(vm, ids[0], 0x11, 10, 5, ims[0], v_arr);
    const vka_vm_ent *e0 = vka_vmemo4_find(vm, ids[0], 1, 10, 5);
    const vka_vm_ent *e1 = vka_vmemo4_find(vm, ids[0], 0x11, 10, 5);
    if (!e0 || !e1 || e0->v != vws[0] || e1->v != v_arr) { free(vm); return 2; }
    if (vka_vmemo4_find(vm, ids[0], 1, 11, 5) != NULL) { free(vm); return 3; }
    if (vka_vmemo4_find(vm, ids[0], 1, 10, 6) != NULL) { free(vm); return 4; }
    free(vm);
    return 0;
}

static int test_smemo4_cmemo4_correctness(void) {
    vka_smemo4 *sm = calloc(1, sizeof *sm);
    vka_cmemo4 *cm = calloc(1, sizeof *cm);
    assert(sm != NULL && cm != NULL);
    int sids[4] = { 17, 17 + 512, 17 + 1024, 17 + 1536 };
    for (int i = 0; i < 4; i++) vka_smemo4_put(sm, sids[i], 3, (void *)(uintptr_t)(0x4000u + (unsigned)sids[i]));
    for (int i = 0; i < 4; i++) {
        void *got = vka_smemo4_get(sm, sids[i], 3);
        if (got != (void *)(uintptr_t)(0x4000u + (unsigned)sids[i])) { free(sm); free(cm); return 1; }
    }
    if (vka_smemo4_get(sm, sids[0], 4) != NULL) { free(sm); free(cm); return 2; }
    int cids[4] = { 9, 9 + 2048, 9 + 4096, 9 + 6144 };
    int classes[4] = { 0, 1, 2, 1 };
    for (int i = 0; i < 4; i++) vka_cmemo4_put(cm, cids[i], 7, classes[i]);
    for (int i = 0; i < 4; i++) {
        int c = -1;
        if (!vka_cmemo4_get(cm, cids[i], 7, &c) || c != classes[i]) { free(sm); free(cm); return 3; }
    }
    int dummy = -1;
    if (vka_cmemo4_get(cm, cids[0], 8, &dummy) != 0) { free(sm); free(cm); return 4; }
    free(sm);
    free(cm);
    return 0;
}

static uint64_t run_legacy_bench(const int *tids, const int *sids, int ops, uint64_t *misses_out) {
    memset(g_leg_vm, 0, sizeof g_leg_vm);
    memset(g_leg_sm, 0, sizeof g_leg_sm);
    memset(g_leg_cm, 0, sizeof g_leg_cm);
    uint64_t t0 = now_ns(), misses = 0, acc = 0;
    for (int i = 0; i < ops; i++) {
        int tid = tids[i & 3], sid = sids[i & 3];
        uint32_t ver = g_ver[tid], sv = g_ver[sid];
        leg_vm *vm = &g_leg_vm[((unsigned)tid * 7u + 1u) & 2047u];
        if (!(vm->id == tid && vm->dim == 1 && vm->ver == ver && vm->gen == 1)) {
            void *im = sim_resolve((uintptr_t)tid, ver);
            *vm = (leg_vm){tid, 1, ver, 1, im, im};
            misses++;
        }
        leg_sm *sm = &g_leg_sm[sid & 511];
        if (!(sm->id == sid && sm->ver == sv && sm->s)) {
            *sm = (leg_sm){sid, sv, sim_resolve((uintptr_t)sid + 1u, sv)};
            misses++;
        }
        leg_cm *cm = &g_leg_cm[(unsigned)tid & 2047u];
        if (!(cm->id == tid && cm->ver == ver)) {
            (void)sim_resolve((uintptr_t)tid + 2u, ver);
            *cm = (leg_cm){tid, ver, i & 3};
            misses++;
        }
        acc += (uintptr_t)vm->v ^ (uintptr_t)sm->s ^ (unsigned)cm->cls;
    }
    g_slow_sink ^= acc;
    *misses_out = misses;
    return now_ns() - t0;
}

static uint64_t run_fast_bench(const int *tids, const int *sids, int ops, uint64_t *misses_out) {
    vka_vmemo4 *vm = calloc(1, sizeof *vm);
    vka_smemo4 *sm = calloc(1, sizeof *sm);
    vka_cmemo4 *cm = calloc(1, sizeof *cm);
    assert(vm != NULL && sm != NULL && cm != NULL);
    uint64_t t0 = now_ns(), misses = 0, acc = 0;
    for (int i = 0; i < ops; i++) {
        int tid = tids[i & 3], sid = sids[i & 3];
        uint32_t ver = g_ver[tid], sv = g_ver[sid];
        const vka_vm_ent *ve = vka_vmemo4_find(vm, tid, 1, ver, 1);
        if (!ve) {
            void *im = sim_resolve((uintptr_t)tid, ver);
            vka_vmemo4_put(vm, tid, 1, ver, 1, im, im);
            misses++;
            ve = vka_vmemo4_find(vm, tid, 1, ver, 1);
        }
        void *sp = vka_smemo4_get(sm, sid, sv);
        if (!sp) {
            sp = sim_resolve((uintptr_t)sid + 1u, sv);
            vka_smemo4_put(sm, sid, sv, sp);
            misses++;
        }
        int cls = 0;
        if (!vka_cmemo4_get(cm, tid, ver, &cls)) {
            (void)sim_resolve((uintptr_t)tid + 2u, ver);
            cls = i & 3;
            vka_cmemo4_put(cm, tid, ver, cls);
            misses++;
        }
        acc += (uintptr_t)(ve ? ve->v : NULL) ^ (uintptr_t)sp ^ (unsigned)cls;
    }
    g_slow_sink ^= acc;
    *misses_out = misses;
    free(vm); free(sm); free(cm);
    return now_ns() - t0;
}

int main(void) {
    const char *fe = getenv("T_W7_FAULT");
    g_w7_fault = fe ? atoi(fe) : 0;
    int r1 = test_vmemo4_correctness();
    if (r1) { fprintf(stderr, "FAIL test_vmemo4_correctness rc=%d fault=%d\n", r1, g_w7_fault); return 1; }
    int r2 = test_smemo4_cmemo4_correctness();
    if (r2) { fprintf(stderr, "FAIL test_smemo4_cmemo4_correctness rc=%d fault=%d\n", r2, g_w7_fault); return 1; }

    int tids[4] = { 64, 64 + 2048, 64 + 4096, 64 + 6144 };
    int sids[4] = { 17, 17 + 512, 17 + 1024, 17 + 1536 };
    for (int i = 0; i < 4; i++) { g_ver[tids[i]] = 1; g_ver[sids[i]] = 1; }
    uint64_t lm = 0, fm = 0;
    uint64_t lns = run_legacy_bench(tids, sids, 400000, &lm);
    uint64_t fns = run_fast_bench(tids, sids, 400000, &fm);
    double lms = (double)lns / 1e6, fms = (double)fns / 1e6;
    printf("t_tex_memo_w7: fault=%d 400k_ops legacy=%.2fms (misses=%llu) fast=%.2fms (misses=%llu) speedup=%.2fx sink=%llu\n",
           g_w7_fault, lms, (unsigned long long)lm, fms, (unsigned long long)fm,
           fms > 0 ? lms / fms : 0.0, (unsigned long long)g_slow_sink);
    if (fm != 12) { fprintf(stderr, "FAIL expected 12 cold misses got %llu\n", (unsigned long long)fm); return 1; }
    return 0;
}
