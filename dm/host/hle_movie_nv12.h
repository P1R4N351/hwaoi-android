// Fast I420 -> NV12 packing helper (continguous Y copy + border-only black/neutral padding + NEON UV interleave).
#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "movie_fast.h"
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static inline void dm_nv12_pack_y(const uint8_t *y_src, int y_stride, int cw, int ch, uint8_t *dst, int w, int h,
                                  int fast, int fault_mode) {
    assert(y_src != NULL && dst != NULL && w > 0 && h > 0);
    if (fast && cw == w && y_stride == w) {
        memcpy(dst, y_src, (size_t)w * (size_t)ch);
    } else {
        for (int y = 0; y < ch; y++) {
            uint8_t *row = dst + (size_t)y * (size_t)w;
            if (cw > 0) memcpy(row, y_src + (size_t)y * (size_t)y_stride, (size_t)cw);
            if (cw < w && fault_mode != 1) memset(row + cw, 16, (size_t)(w - cw));
        }
    }
    if (ch < h && fault_mode != 1) memset(dst + (size_t)ch * (size_t)w, 16, (size_t)(h - ch) * (size_t)w);
}

static inline void dm_nv12_uv_row(const uint8_t * restrict u, const uint8_t * restrict v,
                                  int cw2, uint8_t * restrict o, int w, int fast, int fault_mode) {
    int limit = w / 2;
    if (cw2 < limit) limit = cw2;
    int x = 0, fm = fast ? dm_w31_fault_mode() : 0;
    if (fast) x = dm_nv12_uv_swar_span(u, v, o, x, limit, fm);
    for (; x < limit; x++) {
        int sw = fm == 1;
        o[2 * x] = sw ? v[x] : u[x];
        o[2 * x + 1] = sw ? u[x] : v[x];
    }
    int written = 2 * limit;
    if (written < w && fault_mode != 1) memset(o + written, 128, (size_t)(w - written));
}

static inline void dm_nv12_pack_uv(const uint8_t *u_src, int u_stride, const uint8_t *v_src, int v_stride,
                                   int cw, int ch, uint8_t *uv, int w, int h, int fast, int fault_mode) {
    assert(u_src != NULL && v_src != NULL && uv != NULL && w > 0 && h > 0);
    int cw2 = (cw + 1) / 2, ch2 = (ch + 1) / 2, h2 = h / 2;
    if (ch2 > h2) ch2 = h2;
    for (int y = 0; y < ch2; y++) {
        const uint8_t *u = u_src + (size_t)y * (size_t)u_stride;
        const uint8_t *v = v_src + (size_t)y * (size_t)v_stride;
        if (fault_mode == 2) { const uint8_t *tmp = u; u = v; v = tmp; }
        dm_nv12_uv_row(u, v, cw2, uv + (size_t)y * (size_t)w, w, fast, fault_mode);
    }
    size_t done = (size_t)ch2 * (size_t)w, total = (size_t)w * (size_t)h / 2;
    if (done < total && fault_mode != 1) memset(uv + done, 128, total - done);
}

static inline void dm_movie_nv12_pack(const uint8_t *y_src, int y_stride, const uint8_t *u_src, int u_stride,
                                      const uint8_t *v_src, int v_stride, int src_w, int src_h,
                                      uint8_t *dst, int w, int h, int fast, int fault_mode) {
    if (!dst || !y_src || !u_src || !v_src || w <= 0 || h <= 0) return;
    int cw = src_w < w ? src_w : w, ch = src_h < h ? src_h : h;
    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;
    if (!fast) {
        if (cw < w || ch < h) { memset(dst, 16, (size_t)w * (size_t)h); memset(dst + (size_t)w * (size_t)h, 128, (size_t)w * (size_t)h / 2); }
        for (int y = 0; y < ch; y++) memcpy(dst + (size_t)y * (size_t)w, y_src + (size_t)y * (size_t)y_stride, (size_t)cw);
        int cw2 = (cw + 1) / 2, ch2 = (ch + 1) / 2;
        for (int y = 0; y < ch2 && y < h / 2; y++) {
            const uint8_t *u = u_src + (size_t)y * (size_t)u_stride, *v = v_src + (size_t)y * (size_t)v_stride;
            dm_nv12_uv_row(u, v, cw2, dst + (size_t)w * (size_t)h + (size_t)y * (size_t)w, w, 0, 0);
        }
        return;
    }
    dm_nv12_pack_y(y_src, y_stride, cw, ch, dst, w, h, fast, fault_mode);
    dm_nv12_pack_uv(u_src, u_stride, v_src, v_stride, cw, ch, dst + (size_t)w * (size_t)h, w, h, fast, fault_mode);
}
