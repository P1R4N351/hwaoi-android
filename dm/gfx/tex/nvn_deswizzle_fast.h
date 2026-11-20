#ifndef DM_NVN_DESWIZZLE_FAST_H
#define DM_NVN_DESWIZZLE_FAST_H
/* Wave 6 (DM_DESWIZZLE_FAST=1): Full-GOB 512 B block-linear <-> linear kernels.
 * Processes all 4 row pairs (Tegra X1) or 2 4-row halves (Switch 2) of each 64 B x 8
 * interior GOB with constant-size 16 B copies and sequential 64 B cache-line accesses. */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_deswz_fast_cached;

static inline int deswz_fast_on(void) {
    int v = atomic_load_explicit(&g_deswz_fast_cached, memory_order_relaxed);
    if (!v) {
        const char *e = getenv("DM_DESWIZZLE_FAST");
        v = (e && !strcmp(e, "0")) ? -1 : 1;
        atomic_store_explicit(&g_deswz_fast_cached, v, memory_order_relaxed);
    }
    return v > 0;
}

static inline int deswz_w6_fault(void) {
#ifdef W6_TEST_HOOKS
    const char *e = getenv("T_W6_FAULT");
    return e ? atoi(e) : 0;
#else
    return 0;
#endif
}

static inline void deswz_cp16(void *dst, const void *src) {
    memcpy(dst, src, 16);
}

static inline void deswz_cp16_dir(uint8_t *bl_p, uint8_t *lin_p, int to_linear) {
    if (to_linear) deswz_cp16(lin_p, bl_p);
    else deswz_cp16(bl_p, lin_p);
}

/* Tegra X1 GOB (!nvn_gob_switch2): 4 row pairs, each spanned by g0 = gob+r*64 and g1 = gob+256+r*64. */
static inline void deswz_gob_x1(uint8_t *gob, uint8_t *lin0, size_t pitch, int to_linear, int fault) {
    size_t swap = (fault == 1) ? 16u : 0u;
    for (unsigned r = 0; r < 4u; r++) {
        uint8_t *g0 = gob + ((size_t)r << 6);
        uint8_t *g1 = g0 + 256u;
        uint8_t *re = lin0 + (size_t)(2u * r) * pitch;
        uint8_t *ro = re + pitch;
        deswz_cp16_dir(g0 + swap, re + 0u, to_linear);
        deswz_cp16_dir(g0 + (32u ^ swap), re + 16u, to_linear);
        deswz_cp16_dir(g1 + 0u, re + 32u, to_linear);
        deswz_cp16_dir(g1 + 32u, re + 48u, to_linear);
        deswz_cp16_dir(g0 + (16u ^ swap), ro + 0u, to_linear);
        deswz_cp16_dir(g0 + 48u, ro + 16u, to_linear);
        deswz_cp16_dir(g1 + 16u, ro + 32u, to_linear);
        deswz_cp16_dir(g1 + 48u, ro + 48u, to_linear);
    }
}

/* Switch 2 GOB (nvn_gob_switch2 != 0): two 4-row halves, x%32/16 at +64, y%4*16 contiguous. */
static inline void deswz_gob_sw2(uint8_t *gob, uint8_t *lin0, size_t pitch, int to_linear) {
    for (unsigned h = 0; h < 2u; h++) {
        uint8_t *c0 = gob + ((size_t)h << 7);
        uint8_t *c1 = c0 + 64u, *c2 = c0 + 256u, *c3 = c0 + 320u;
        for (unsigned ry = 0; ry < 4u; ry++) {
            uint8_t *row = lin0 + (size_t)(4u * h + ry) * pitch;
            size_t yo = (size_t)ry << 4;
            deswz_cp16_dir(c0 + yo, row + 0u, to_linear);
            deswz_cp16_dir(c1 + yo, row + 16u, to_linear);
            deswz_cp16_dir(c2 + yo, row + 32u, to_linear);
            deswz_cp16_dir(c3 + yo, row + 48u, to_linear);
        }
    }
}
#endif
