#ifndef VKA_IMG_FAST_H
#define VKA_IMG_FAST_H
/* Wave 22 (Opt 24): Lock-free fence reads, storage-view/metadata front-memos, steady gpu_owned elision. */
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef W22_TEST_HOOKS
extern int g_w22_fault;
extern int g_w22_fast_override;
#define VKA_IMG_FAULT(n) (g_w22_fault == (n))
static inline int vka_img_fast_on(void) { return g_w22_fast_override != 0; }
#else
#define VKA_IMG_FAULT(n) 0
static inline int vka_img_fast_on(void) {
#if defined(VKA_ENV)
    return VKA_ENV("DM_VK_IMG_FAST", 1) != 0;
#else
    const char *e = getenv("DM_VK_IMG_FAST");
    return !(e && e[0] == '0' && !e[1]);
#endif
}
#endif

enum {
    VKA_SV_SETS = 64u,
    VKA_TM_SETS = 64u,
    VKA_IMG_WAYS = 4u
};

static inline uint32_t vka_img_hash32(uint32_t a, uint32_t b, uint32_t c) {
    uint64_t x = (uint64_t)a * UINT64_C(0x9e3779b97f4a7c15) ^
                 ((uint64_t)b * UINT64_C(0xbf58476d1ce4e5b9)) ^
                 ((uint64_t)c * UINT64_C(0x94d049bb133111eb));
    x ^= x >> 31;
    x *= UINT64_C(0xff51afd7ed558ccd);
    return (uint32_t)(x ^ (x >> 33));
}

typedef struct {
    uint64_t pfin;
    void *im;
    void *v;
    uint32_t id, ver, gen;
    uint8_t dim, bind_view, valid, pad;
} vka_sv_ent;

typedef struct {
    vka_sv_ent e[VKA_SV_SETS][VKA_IMG_WAYS];
} vka_sview_memo4;

static inline const vka_sv_ent *vka_sview_memo4_find(vka_sview_memo4 *m, uint32_t id,
                                                     uint8_t dim, uint8_t bind_view,
                                                     uint32_t ver, uint32_t gen,
                                                     uint64_t pfin) {
    assert(m != NULL);
    if (!id || !ver) return NULL;
    uint32_t s = vka_img_hash32(id, dim, bind_view) & (VKA_SV_SETS - 1u);
    vka_sv_ent *ways = m->e[s];
    for (uint32_t w = 0; w < VKA_IMG_WAYS; w++) {
        vka_sv_ent *e = &ways[w];
        if (!e->valid || e->id != id || e->dim != dim || e->bind_view != bind_view) continue;
        if (!VKA_IMG_FAULT(2) && e->ver != ver) continue;
        if (!VKA_IMG_FAULT(1) && e->gen != gen) continue;
        if (e->pfin != pfin) continue;
        if (w > 0u) {
            vka_sv_ent hit = ways[w];
            for (uint32_t k = w; k > 0u; k--) ways[k] = ways[k - 1u];
            ways[0] = hit;
        }
        return &ways[0];
    }
    return NULL;
}

static inline void vka_sview_memo4_put(vka_sview_memo4 *m, uint32_t id, uint8_t dim,
                                       uint8_t bind_view, uint32_t ver, uint32_t gen,
                                       uint64_t pfin, void *im, void *v) {
    assert(m != NULL);
    if (!id || !ver || !im || !v) return;
    uint32_t s = vka_img_hash32(id, dim, bind_view) & (VKA_SV_SETS - 1u);
    vka_sv_ent *ways = m->e[s];
    uint32_t pos = VKA_IMG_WAYS - 1u;
    for (uint32_t w = 0; w < VKA_IMG_WAYS; w++) {
        if (!ways[w].valid || (ways[w].id == id && ways[w].dim == dim && ways[w].bind_view == bind_view)) {
            pos = w; break;
        }
    }
    for (uint32_t k = pos; k > 0u; k--) ways[k] = ways[k - 1u];
    ways[0] = (vka_sv_ent){pfin, im, v, id, ver, gen, dim, bind_view, 1u, 0u};
}

typedef struct {
    uint64_t pfin, buf_addr;
    uint32_t id, ver, gen, buf_elems;
    int32_t buf_fmt;
    int16_t target;
    uint8_t valid, storage_ready, target_known, buf_known, is_buf, pad;
} vka_tm_ent;

typedef struct {
    vka_tm_ent e[VKA_TM_SETS][VKA_IMG_WAYS];
} vka_tmeta_memo4;

static inline vka_tm_ent *vka_tmeta_find_ent(vka_tmeta_memo4 *m, uint32_t id,
                                             uint32_t ver, uint32_t gen, uint64_t pfin,
                                             int chk_ver, int chk_gen, int chk_pfin) {
    assert(m != NULL);
    if (!id || !ver) return NULL;
    uint32_t s = vka_img_hash32(id, 0u, 0u) & (VKA_TM_SETS - 1u);
    vka_tm_ent *ways = m->e[s];
    for (uint32_t w = 0; w < VKA_IMG_WAYS; w++) {
        vka_tm_ent *e = &ways[w];
        if (!e->valid || e->id != id) continue;
        if (chk_ver && e->ver != ver) continue;
        if (chk_gen && e->gen != gen) continue;
        if (chk_pfin && e->pfin != pfin) continue;
        if (w > 0u) {
            vka_tm_ent hit = ways[w];
            for (uint32_t k = w; k > 0u; k--) ways[k] = ways[k - 1u];
            ways[0] = hit;
        }
        return &ways[0];
    }
    return NULL;
}

static inline vka_tm_ent *vka_tmeta_slot(vka_tmeta_memo4 *m, uint32_t id,
                                         uint32_t ver, uint32_t gen, uint64_t pfin) {
    assert(m != NULL && id > 0u && ver > 0u);
    vka_tm_ent *hit = vka_tmeta_find_ent(m, id, ver, gen, pfin, 1, 1, 1);
    if (hit) return hit;
    uint32_t s = vka_img_hash32(id, 0u, 0u) & (VKA_TM_SETS - 1u);
    vka_tm_ent *ways = m->e[s];
    for (uint32_t k = VKA_IMG_WAYS - 1u; k > 0u; k--) ways[k] = ways[k - 1u];
    memset(&ways[0], 0, sizeof ways[0]);
    ways[0].pfin = pfin;
    ways[0].id = id;
    ways[0].ver = ver;
    ways[0].gen = gen;
    ways[0].valid = 1u;
    return &ways[0];
}

static inline int vka_tmeta_memo4_get_storage_ready(vka_tmeta_memo4 *m, uint32_t id,
                                                    uint32_t ver, uint32_t gen, uint64_t pfin) {
    vka_tm_ent *e = vka_tmeta_find_ent(m, id, ver, gen, pfin, 1, 1, 1);
    return e != NULL && e->storage_ready != 0u;
}

static inline void vka_tmeta_memo4_put_storage_ready(vka_tmeta_memo4 *m, uint32_t id,
                                                     uint32_t ver, uint32_t gen, uint64_t pfin) {
    if (!id || !ver) return;
    vka_tmeta_slot(m, id, ver, gen, pfin)->storage_ready = 1u;
}

static inline int vka_tmeta_memo4_get_target(vka_tmeta_memo4 *m, uint32_t id, uint32_t ver,
                                             uint32_t gen, uint64_t pfin, int *out_target) {
    assert(out_target != NULL);
    vka_tm_ent *e = vka_tmeta_find_ent(m, id, ver, gen, pfin, !VKA_IMG_FAULT(4), 1, 1);
    if (!e || !e->target_known) return 0;
    *out_target = (int)e->target;
    return 1;
}

static inline void vka_tmeta_memo4_put_target(vka_tmeta_memo4 *m, uint32_t id, uint32_t ver,
                                              uint32_t gen, uint64_t pfin, int target) {
    if (!id || !ver) return;
    vka_tm_ent *e = vka_tmeta_slot(m, id, ver, gen, pfin);
    e->target = (int16_t)target;
    e->target_known = 1u;
}

static inline int vka_tmeta_memo4_get_buf(vka_tmeta_memo4 *m, uint32_t id, uint32_t ver,
                                          uint32_t gen, uint64_t pfin, int *out_is_buf,
                                          uint64_t *addr, uint32_t *elems, int *fmt) {
    assert(out_is_buf != NULL && addr != NULL && elems != NULL && fmt != NULL);
    vka_tm_ent *e = vka_tmeta_find_ent(m, id, ver, gen, pfin, 1, 1, !VKA_IMG_FAULT(3));
    if (!e || !e->buf_known) return 0;
    *out_is_buf = (int)e->is_buf;
    if (e->is_buf) { *addr = e->buf_addr; *elems = e->buf_elems; *fmt = (int)e->buf_fmt; }
    return 1;
}

static inline void vka_tmeta_memo4_put_buf(vka_tmeta_memo4 *m, uint32_t id, uint32_t ver,
                                           uint32_t gen, uint64_t pfin, int is_buf,
                                           uint64_t addr, uint32_t elems, int fmt) {
    if (!id || !ver) return;
    vka_tm_ent *e = vka_tmeta_slot(m, id, ver, gen, pfin);
    e->buf_addr = is_buf ? addr : 0u;
    e->buf_elems = is_buf ? elems : 0u;
    e->buf_fmt = is_buf ? (int32_t)fmt : 0;
    e->is_buf = is_buf ? 1u : 0u;
    e->buf_known = 1u;
}

static inline int vka_img_steady_gpu_owned(int gpu_owned, int mz, int edge_ix,
                                           int edge_unpinned, int wb_kind) {
    if (VKA_IMG_FAULT(5)) return gpu_owned && !edge_ix && !edge_unpinned;
    return gpu_owned && !mz && !edge_ix && !edge_unpinned && !wb_kind;
}

#endif
