// Set-associative per-draw front-memos for texture views, samplers, and format classes (DM_VK_TEX_FAST=1).
// Preserves spatial locality across contiguous IDs while eliminating 1-way power-of-2 collisions with read-clean hits.
#ifndef VKA_TEX_FAST_H
#define VKA_TEX_FAST_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#ifdef W7_TEST_HOOKS
extern int g_w7_fault;
#define VKA_TEX_FAST_FAULT(n) (g_w7_fault == (n))
#else
#define VKA_TEX_FAST_FAULT(n) 0
#endif

enum { VKA_VM_SETS = 2048, VKA_SM_SETS = 512, VKA_CM_SETS = 2048, VKA_TM_WAYS = 4 };

typedef struct {
    uint32_t key, ver, gen, pad;
    void *im;
    void *v;
} vka_vm_ent;

typedef struct {
    vka_vm_ent e[VKA_TM_WAYS];
    uint32_t rr, pad;
} vka_vm_set_t;

typedef struct {
    vka_vm_set_t s[VKA_VM_SETS];
} vka_vmemo4;

typedef struct {
    int32_t id;
    uint32_t ver;
    void *s;
} vka_sm_ent;

typedef struct {
    vka_sm_ent e[VKA_TM_WAYS];
    uint32_t rr, pad[3];
} vka_sm_set_t;

typedef struct {
    vka_sm_set_t s[VKA_SM_SETS];
} vka_smemo4;

typedef struct {
    uint32_t key_cls, ver;
} vka_cm_ent;

typedef struct {
    vka_cm_ent e[VKA_TM_WAYS];
    uint32_t rr, pad;
} vka_cm_set_t;

typedef struct {
    vka_cm_set_t s[VKA_CM_SETS];
} vka_cmemo4;

static inline uint32_t vka_vm_key(int id, uint8_t dim) {
    assert(id > 0 && id < (1 << 20));
    return ((uint32_t)id & 0xFFFFFu) | ((uint32_t)dim << 20);
}

static inline uint32_t vka_vm_idx(int id, uint8_t dim) {
    return ((uint32_t)id + (uint32_t)dim * 131u) & (VKA_VM_SETS - 1u);
}

static inline const vka_vm_ent *vka_vmemo4_find(const vka_vmemo4 *c, int id, uint8_t dim, uint32_t ver, uint32_t gen) {
    assert(c != NULL);
    if (id <= 0 || id >= (1 << 20)) return NULL;
    uint32_t key = vka_vm_key(id, dim);
    const vka_vm_set_t *set = &c->s[vka_vm_idx(id, dim)];
    for (uint32_t w = 0; w < VKA_TM_WAYS; w++) {
        const vka_vm_ent *e = &set->e[w];
        if (e->key == key && (VKA_TEX_FAST_FAULT(1) || e->ver == ver) && e->gen == gen) return e;
    }
    return NULL;
}

static inline void vka_vmemo4_put(vka_vmemo4 *c, int id, uint8_t dim, uint32_t ver, uint32_t gen, void *im, void *v) {
    assert(c != NULL);
    if (id <= 0 || id >= (1 << 20) || !im || !v) return;
    uint32_t key = vka_vm_key(id, dim);
    vka_vm_set_t *set = &c->s[vka_vm_idx(id, dim)];
    uint32_t slot = VKA_TM_WAYS;
    for (uint32_t w = 0; w < VKA_TM_WAYS; w++) {
        if (set->e[w].key == key || !set->e[w].key) { slot = w; break; }
    }
    if (slot == VKA_TM_WAYS) slot = set->rr++ & (VKA_TM_WAYS - 1u);
    set->e[slot] = (vka_vm_ent){key, ver, gen, 0, im, v};
}

static inline uint32_t vka_sm_idx(int id) {
    return (uint32_t)id & (VKA_SM_SETS - 1u);
}

static inline void *vka_smemo4_get(const vka_smemo4 *c, int id, uint32_t ver) {
    assert(c != NULL);
    if (id <= 0) return NULL;
    const vka_sm_set_t *set = &c->s[vka_sm_idx(id)];
    for (uint32_t w = 0; w < VKA_TM_WAYS; w++) {
        const vka_sm_ent *e = &set->e[w];
        if (e->id == id && (VKA_TEX_FAST_FAULT(2) || e->ver == ver) && e->s) return e->s;
    }
    return NULL;
}

static inline void vka_smemo4_put(vka_smemo4 *c, int id, uint32_t ver, void *sp) {
    assert(c != NULL);
    if (id <= 0 || !sp) return;
    vka_sm_set_t *set = &c->s[vka_sm_idx(id)];
    uint32_t slot = VKA_TM_WAYS;
    for (uint32_t w = 0; w < VKA_TM_WAYS; w++) {
        if (set->e[w].id == id || !set->e[w].id) { slot = w; break; }
    }
    if (slot == VKA_TM_WAYS) slot = set->rr++ & (VKA_TM_WAYS - 1u);
    set->e[slot] = (vka_sm_ent){id, ver, sp};
}

static inline uint32_t vka_cm_idx(int id) {
    return (uint32_t)id & (VKA_CM_SETS - 1u);
}

static inline int vka_cmemo4_get(const vka_cmemo4 *c, int id, uint32_t ver, int *cls_out) {
    assert(c != NULL && cls_out != NULL);
    if (id <= 0 || id >= (1 << 20)) return 0;
    uint32_t uid = (uint32_t)id & 0xFFFFFu;
    const vka_cm_set_t *set = &c->s[vka_cm_idx(id)];
    for (uint32_t w = 0; w < VKA_TM_WAYS; w++) {
        const vka_cm_ent *e = &set->e[w];
        if ((e->key_cls & 0x4FFFFFu) == (uid | 0x400000u) && e->ver == ver) {
            *cls_out = (int)((e->key_cls >> 20) & 3u);
            return 1;
        }
    }
    return 0;
}

static inline void vka_cmemo4_put(vka_cmemo4 *c, int id, uint32_t ver, int cls) {
    assert(c != NULL);
    if (id <= 0 || id >= (1 << 20)) return;
    if (VKA_TEX_FAST_FAULT(3)) cls = 0;
    uint32_t uid = (uint32_t)id & 0xFFFFFu;
    uint32_t kc = uid | ((uint32_t)(cls & 3) << 20) | 0x400000u;
    vka_cm_set_t *set = &c->s[vka_cm_idx(id)];
    uint32_t slot = VKA_TM_WAYS;
    for (uint32_t w = 0; w < VKA_TM_WAYS; w++) {
        if ((set->e[w].key_cls & 0x4FFFFFu) == (uid | 0x400000u) || !(set->e[w].key_cls & 0x400000u)) {
            slot = w; break;
        }
    }
    if (slot == VKA_TM_WAYS) slot = set->rr++ & (VKA_TM_WAYS - 1u);
    set->e[slot] = (vka_cm_ent){kc, ver};
}

#endif
