// KTGL separable-program (512x4 = 2,048) & pipeline-layout (256x4 = 1,024 / 32x4 = 128 CS) front-caches + narrow indirect QUADS barriers.
#pragma once
#include <stdint.h>
#include <string.h>

enum {
    VKA_KTGL_NSTAGES = 8,
    VKA_KTGL_LAY_BYTES = 32,
    VKA_KTGL_PM_SETS = 512,
    VKA_KTGL_PM_WAYS = 4,
    VKA_KTGL_LAY_SETS = 512,
    VKA_KTGL_CS_SETS = 32,
    VKA_KTGL_LAY_WAYS = 4
};

typedef struct {
    uint64_t serial;
    uint32_t gen;
    uint32_t bad;
    void *sh[VKA_KTGL_NSTAGES];
} vka_ktgl_pm_ent;

typedef struct {
    vka_ktgl_pm_ent ways[VKA_KTGL_PM_WAYS];
    uint8_t next;
} vka_ktgl_pm_set_t;

typedef struct {
    vka_ktgl_pm_set_t sets[VKA_KTGL_PM_SETS];
} vka_ktgl_pm_cache;

typedef struct {
    const void *sh[VKA_KTGL_NSTAGES];
    uint8_t lay[VKA_KTGL_LAY_BYTES];
    uint8_t valid;
} vka_ktgl_lay_ent;

typedef struct {
    vka_ktgl_lay_ent ways[VKA_KTGL_LAY_WAYS];
    uint8_t next;
} vka_ktgl_lay_set_t;

typedef struct {
    vka_ktgl_lay_set_t sets[VKA_KTGL_LAY_SETS];
} vka_ktgl_lay_cache;

typedef struct {
    const void *cs;
    uint8_t lay[VKA_KTGL_LAY_BYTES];
    uint8_t valid;
} vka_ktgl_cs_ent;

typedef struct {
    vka_ktgl_cs_ent ways[VKA_KTGL_LAY_WAYS];
    uint8_t next;
} vka_ktgl_cs_set_t;

typedef struct {
    vka_ktgl_cs_set_t sets[VKA_KTGL_CS_SETS];
} vka_ktgl_cs_cache;

typedef struct {
    uint32_t src_stage, dst_stage, src_access, dst_access;
} vka_ktgl_bar_spec;

static inline uint32_t vka_ktgl_pm_set(uint64_t serial) {
    return (uint32_t)(((serial * 11400714819323198485ull) >> 55) & (VKA_KTGL_PM_SETS - 1));
}

static inline uint32_t vka_ktgl_lay_hash(const void *const sh[VKA_KTGL_NSTAGES]) {
    uint64_t h = (uint64_t)(uintptr_t)sh[0] * 11400714819323198485ull + (uint64_t)(uintptr_t)sh[1];
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    return (uint32_t)((h ^ (h >> 31)) >> 55) & (VKA_KTGL_LAY_SETS - 1);
}

static inline int vka_ktgl_pm_get(const vka_ktgl_pm_cache *c, uint64_t serial, uint32_t gen,
                                  uint32_t bad_missing_bit, void *sh_out[VKA_KTGL_NSTAGES],
                                  uint32_t *bad_out, int ignore_gen_fault) {
    if (!c || !serial || !sh_out || !bad_out) return 0;
    const vka_ktgl_pm_ent *ways = c->sets[vka_ktgl_pm_set(serial)].ways;
    for (int w = 0; w < VKA_KTGL_PM_WAYS; w++) {
        const vka_ktgl_pm_ent *e = &ways[w];
        if (e->serial != serial) continue;
        if (!ignore_gen_fault && (e->bad & bad_missing_bit) && e->gen != gen) continue;
        memcpy(sh_out, e->sh, sizeof e->sh);
        *bad_out = e->bad;
        return 1;
    }
    return 0;
}

static inline void vka_ktgl_pm_put(vka_ktgl_pm_cache *c, uint64_t serial, uint32_t gen,
                                   void *const sh_in[VKA_KTGL_NSTAGES], uint32_t bad) {
    if (!c || !serial || !sh_in) return;
    vka_ktgl_pm_set_t *s = &c->sets[vka_ktgl_pm_set(serial)];
    int slot = -1;
    for (int w = 0; w < VKA_KTGL_PM_WAYS; w++) {
        if (s->ways[w].serial == serial || !s->ways[w].serial) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VKA_KTGL_PM_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot].serial = serial;
    s->ways[slot].gen = gen;
    s->ways[slot].bad = bad;
    memcpy(s->ways[slot].sh, sh_in, sizeof s->ways[slot].sh);
}

static inline int vka_ktgl_lay_get(const vka_ktgl_lay_cache *c, const void *const sh[VKA_KTGL_NSTAGES],
                                   void *lay_out, size_t lay_sz) {
    if (!c || !sh || !lay_out || lay_sz > VKA_KTGL_LAY_BYTES) return 0;
    const vka_ktgl_lay_ent *ways = c->sets[vka_ktgl_lay_hash(sh)].ways;
    for (int w = 0; w < VKA_KTGL_LAY_WAYS; w++) {
        const vka_ktgl_lay_ent *e = &ways[w];
        if (!e->valid || e->sh[0] != sh[0] || e->sh[1] != sh[1]) continue;
        if (memcmp(&e->sh[2], &sh[2], 6 * sizeof(void *)) != 0) continue;
        memcpy(lay_out, e->lay, lay_sz);
        return 1;
    }
    return 0;
}

static inline void vka_ktgl_lay_put(vka_ktgl_lay_cache *c, const void *const sh[VKA_KTGL_NSTAGES],
                                    const void *lay_in, size_t lay_sz) {
    if (!c || !sh || !lay_in || lay_sz > VKA_KTGL_LAY_BYTES) return;
    vka_ktgl_lay_set_t *s = &c->sets[vka_ktgl_lay_hash(sh)];
    int slot = -1;
    for (int w = 0; w < VKA_KTGL_LAY_WAYS; w++) {
        if (!s->ways[w].valid || memcmp(s->ways[w].sh, sh, sizeof s->ways[w].sh) == 0) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VKA_KTGL_LAY_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    memcpy(s->ways[slot].sh, sh, sizeof s->ways[slot].sh);
    memcpy(s->ways[slot].lay, lay_in, lay_sz);
    s->ways[slot].valid = 1;
}

static inline int vka_ktgl_cs_lay_get(const vka_ktgl_cs_cache *c, const void *cs, void *lay_out, size_t lay_sz) {
    if (!c || !cs || !lay_out || lay_sz > VKA_KTGL_LAY_BYTES) return 0;
    uint32_t idx = (uint32_t)((((uint64_t)(uintptr_t)cs * 11400714819323198485ull) >> 59) & (VKA_KTGL_CS_SETS - 1));
    const vka_ktgl_cs_ent *ways = c->sets[idx].ways;
    for (int w = 0; w < VKA_KTGL_LAY_WAYS; w++) {
        if (!ways[w].valid || ways[w].cs != cs) continue;
        memcpy(lay_out, ways[w].lay, lay_sz);
        return 1;
    }
    return 0;
}

static inline void vka_ktgl_cs_lay_put(vka_ktgl_cs_cache *c, const void *cs, const void *lay_in, size_t lay_sz) {
    if (!c || !cs || !lay_in || lay_sz > VKA_KTGL_LAY_BYTES) return;
    uint32_t idx = (uint32_t)((((uint64_t)(uintptr_t)cs * 11400714819323198485ull) >> 59) & (VKA_KTGL_CS_SETS - 1));
    vka_ktgl_cs_set_t *s = &c->sets[idx];
    int slot = -1;
    for (int w = 0; w < VKA_KTGL_LAY_WAYS; w++) {
        if (!s->ways[w].valid || s->ways[w].cs == cs) { slot = w; break; }
    }
    if (slot < 0) { slot = s->next & (VKA_KTGL_LAY_WAYS - 1); s->next = (uint8_t)(s->next + 1); }
    s->ways[slot].cs = cs;
    memcpy(s->ways[slot].lay, lay_in, lay_sz);
    s->ways[slot].valid = 1;
}

static inline vka_ktgl_bar_spec vka_ktgl_qdx_bar_spec(int phase, int fault_all_cmds) {
    enum {
        KTGL_STAGE_DRAW_INDIRECT = 0x00000002u, KTGL_STAGE_COMPUTE = 0x00000800u,
        KTGL_STAGE_TRANSFER = 0x00001000u, KTGL_STAGE_ALL_COMMANDS = 0x00010000u,
        KTGL_ACC_INDIRECT_READ = 0x00000001u, KTGL_ACC_SHADER_READ = 0x00000020u,
        KTGL_ACC_SHADER_WRITE = 0x00000040u, KTGL_ACC_TRANSFER_WRITE = 0x00001000u
    };
    if (fault_all_cmds) return (vka_ktgl_bar_spec){KTGL_STAGE_ALL_COMMANDS, KTGL_STAGE_ALL_COMMANDS, KTGL_ACC_TRANSFER_WRITE, KTGL_ACC_SHADER_READ};
    if (phase == 1) return (vka_ktgl_bar_spec){KTGL_STAGE_TRANSFER, KTGL_STAGE_COMPUTE, KTGL_ACC_TRANSFER_WRITE, KTGL_ACC_SHADER_READ | KTGL_ACC_SHADER_WRITE};
    return (vka_ktgl_bar_spec){KTGL_STAGE_COMPUTE, KTGL_STAGE_DRAW_INDIRECT, KTGL_ACC_SHADER_WRITE, KTGL_ACC_INDIRECT_READ};
}
