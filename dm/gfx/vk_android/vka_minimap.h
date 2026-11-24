// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan HUD minimap interception and redirection (2-slot staging ring).
// Saves pre-HUD scene patch, captures square minimap region for Display 4, and
// restores the pre-HUD region on Display 0 so the minimap does not duplicate.

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    VKA_MM_MIN_SIDE = 96u,
    VKA_MM_MAX_SIDE = 768u,
    VKA_MM_SLOTS = 2,
    VKA_MM_MAX_CAND = 8
};
#define VKA_MM_BUF_BYTES ((VkDeviceSize)(VKA_MM_MAX_SIDE * VKA_MM_MAX_SIDE * 4u))

uint64_t vka_frame(void);
uint64_t vka_done(void);
uint64_t vka_fence_queued(void);
int vka_origin_ll(void);
int vka_cpu_read(uint64_t addr, void *out, size_t n);

typedef struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *ptr; } vka_mm_slot;
typedef struct { uint32_t w, h; VkFormat fmt; uint64_t serial; } vka_mm_cand;

static struct {
    pthread_mutex_t mu;
    vka_mm_slot slot[VKA_MM_SLOTS];
    int ring_ready, ring_failed;
    VkImage save_img;
    VkDeviceMemory save_mem;
    VkFormat save_fmt;
    int save_ready, save_failed;
    vka_mm_cand cand[VKA_MM_MAX_CAND];
    _Atomic int cand_count, want_active, pending_valid;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, pending_frame;
    int write_slot, pending_slot, ready_slot, wrote_in_frame;
    uint32_t write_w, write_h, pending_w, pending_h, ready_w, ready_h, seq;
    VkFormat write_fmt, pending_fmt, ready_fmt;
    uint64_t pending_fence;
    int in_hud_pass, pass_hit;
    VkImage pass_im, saved_hit_img;
    uint64_t saved_hit_frame;
    uint32_t pass_w, pass_h, pass_lv, pass_l0;
    int32_t save_x, save_y, hit_x, hit_y;
    uint32_t save_w, save_h, hit_side;
    uint32_t learn_w, learn_h;
    int32_t learn_cx, learn_cy, learn_half;
} g_vka_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .ready_slot = -1,
    .pending_slot = -1,
};

static int vka_mm_supported_fmt(VkFormat fmt) {
    return fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB
        || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
}

static int vka_mm_find_cand(uint32_t w, uint32_t h, VkFormat fmt, uint64_t serial) {
    int cnt = atomic_load_explicit(&g_vka_mm.cand_count, memory_order_relaxed);
    for (int i = 0; i < cnt && i < VKA_MM_MAX_CAND; i++) {
        vka_mm_cand *c = &g_vka_mm.cand[i];
        if (c->w == w && c->h == h && c->fmt == fmt) { c->serial = serial; return i; }
    }
    if (cnt >= VKA_MM_MAX_CAND) return -1;
    g_vka_mm.cand[cnt] = (vka_mm_cand){w, h, fmt, serial};
    atomic_store_explicit(&g_vka_mm.cand_count, cnt + 1, memory_order_release);
    VLOG("[companion-rt] HUD minimap stream #%d: %ux%u fmt=%d", cnt, w, h, (int)fmt);
    return cnt;
}

static int vka_mm_alloc_slot(vka_mm_slot *s) {
    VkBufferCreateInfo bi = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = VKA_MM_BUF_BYTES,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    if (vk.vkCreateBuffer(V.dev, &bi, NULL, &s->buf)) return 0;
    VkMemoryRequirements mr;
    vk.vkGetBufferMemoryRequirements(V.dev, s->buf, &mr);
    uint32_t t = vka_mem_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
    if (t == UINT32_MAX) {
        t = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    }
    if (t == UINT32_MAX) { vk.vkDestroyBuffer(V.dev, s->buf, NULL); s->buf = VK_NULL_HANDLE; return 0; }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = t};
    void *p = NULL;
    if (vk.vkAllocateMemory(V.dev, &ai, NULL, &s->mem)
            || vk.vkBindBufferMemory(V.dev, s->buf, s->mem, 0)
            || vk.vkMapMemory(V.dev, s->mem, 0, VK_WHOLE_SIZE, 0, &p)) {
        if (s->buf) vk.vkDestroyBuffer(V.dev, s->buf, NULL);
        if (s->mem) vk.vkFreeMemory(V.dev, s->mem, NULL);
        memset(s, 0, sizeof *s);
        return 0;
    }
    s->ptr = (uint8_t *)p;
    return 1;
}

static int vka_mm_ensure_ring(void) {
    if (g_vka_mm.ring_ready) return 1;
    if (g_vka_mm.ring_failed || !V.dev) return 0;
    for (int i = 0; i < VKA_MM_SLOTS; i++) {
        if (!vka_mm_alloc_slot(&g_vka_mm.slot[i])) {
            for (int j = 0; j < i; j++) {
                vka_mm_slot *s = &g_vka_mm.slot[j];
                if (s->ptr) vk.vkUnmapMemory(V.dev, s->mem);
                if (s->buf) vk.vkDestroyBuffer(V.dev, s->buf, NULL);
                if (s->mem) vk.vkFreeMemory(V.dev, s->mem, NULL);
                memset(s, 0, sizeof *s);
            }
            g_vka_mm.ring_failed = 1;
            return 0;
        }
    }
    g_vka_mm.ring_ready = 1;
    return 1;
}

static int vka_mm_ensure_save(VkCommandBuffer cb, VkFormat fmt) {
    if (g_vka_mm.save_ready) return 1;
    if (g_vka_mm.save_failed || !V.dev || !cb) return 0;
    VkImageCreateInfo ci = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
        .extent = {VKA_MM_MAX_SIDE, VKA_MM_MAX_SIDE, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (vk.vkCreateImage(V.dev, &ci, NULL, &g_vka_mm.save_img)) { g_vka_mm.save_failed = 1; return 0; }
    VkMemoryRequirements mr;
    vk.vkGetImageMemoryRequirements(V.dev, g_vka_mm.save_img, &mr);
    uint32_t t = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (t == UINT32_MAX) t = vka_mem_type(mr.memoryTypeBits, 0, 0);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = t};
    if (t == UINT32_MAX || vk.vkAllocateMemory(V.dev, &ai, NULL, &g_vka_mm.save_mem)
            || vk.vkBindImageMemory(V.dev, g_vka_mm.save_img, g_vka_mm.save_mem, 0)) {
        vk.vkDestroyImage(V.dev, g_vka_mm.save_img, NULL);
        if (g_vka_mm.save_mem) vk.vkFreeMemory(V.dev, g_vka_mm.save_mem, NULL);
        g_vka_mm.save_img = VK_NULL_HANDLE; g_vka_mm.save_mem = VK_NULL_HANDLE;
        g_vka_mm.save_failed = 1;
        return 0;
    }
    VkImageMemoryBarrier ib = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = g_vka_mm.save_img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    g_vka_mm.save_fmt = fmt;
    g_vka_mm.save_ready = 1;
    return 1;
}

static void vka_mm_save_box(uint32_t w, uint32_t h, int32_t *rx, int32_t *ry, uint32_t *rw, uint32_t *rh) {
    int32_t x0, y0, x1, y1;
    if (g_vka_mm.learn_w == w && g_vka_mm.learn_h == h && g_vka_mm.learn_half > 0) {
        int32_t half = g_vka_mm.learn_half + 48;
        if (half > (int32_t)(VKA_MM_MAX_SIDE / 2u)) half = (int32_t)(VKA_MM_MAX_SIDE / 2u);
        x0 = g_vka_mm.learn_cx - half; x1 = g_vka_mm.learn_cx + half;
        y0 = g_vka_mm.learn_cy - half; y1 = g_vka_mm.learn_cy + half;
    } else {
        int32_t sw = (int32_t)(w / 2u < VKA_MM_MAX_SIDE ? w / 2u : VKA_MM_MAX_SIDE);
        int32_t sh = (int32_t)((h * 3u) / 5u < VKA_MM_MAX_SIDE ? (h * 3u) / 5u : VKA_MM_MAX_SIDE);
        x0 = (int32_t)w - sw; x1 = (int32_t)w;
        y0 = vka_origin_ll() ? 0 : (int32_t)h - sh;
        y1 = vka_origin_ll() ? sh : (int32_t)h;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)w) x1 = (int32_t)w;
    if (y1 > (int32_t)h) y1 = (int32_t)h;
    if (x1 - x0 > (int32_t)VKA_MM_MAX_SIDE) x0 = x1 - (int32_t)VKA_MM_MAX_SIDE;
    if (y1 - y0 > (int32_t)VKA_MM_MAX_SIDE) y0 = y1 - (int32_t)VKA_MM_MAX_SIDE;
    *rx = x0; *ry = y0;
    *rw = (uint32_t)(x1 > x0 ? x1 - x0 : 0);
    *rh = (uint32_t)(y1 > y0 ? y1 - y0 : 0);
}

static void vka_minimap_pass_begin(
    VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0, uint32_t nl,
    uint32_t w, uint32_t h, int load_c0, int has_stencil) {
    g_vka_mm.in_hud_pass = 0; g_vka_mm.pass_hit = 0;
    if (!cb || !im || !im->img || !V.dev || vka_hung()) return;
    if (!atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire)) return;
    if (!load_c0 || !has_stencil || nl != 1u || im->samples > 1u || w < 640u || h < 360u) return;
    if (!vka_mm_supported_fmt(im->f.vk)
            || (im->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
                != (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return;
    uint64_t fr = vka_frame();
    if (g_vka_mm.saved_hit_frame == fr && g_vka_mm.saved_hit_img == im->img) return;
    if (!vka_mm_ensure_save(cb, im->f.vk)) return;
    int32_t sx = 0, sy = 0; uint32_t sw = 0, sh = 0;
    vka_mm_save_box(w, h, &sx, &sy, &sw, &sh);
    if (!sw || !sh) return;
    vka_barrier(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {sx, sy, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {sw, sh, 1}
    };
    vk.vkCmdCopyImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, g_vka_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vka_barrier(cb);
    g_vka_mm.in_hud_pass = 1; g_vka_mm.pass_im = im->img;
    g_vka_mm.pass_w = w; g_vka_mm.pass_h = h; g_vka_mm.pass_lv = lv; g_vka_mm.pass_l0 = l0;
    g_vka_mm.save_x = sx; g_vka_mm.save_y = sy; g_vka_mm.save_w = sw; g_vka_mm.save_h = sh;
}

static float vka_mm_f16(uint16_t h) {
    uint32_t sg = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31u, m = h & 1023u;
    if (e == 0u) return m ? ((sg ? -1.f : 1.f) * (float)m / 16777216.f) : (sg ? -0.f : 0.f);
    uint32_t u = (e == 31u) ? (sg | 0x7f800000u | (m << 13)) : (sg | ((e + 112u) << 23) | (m << 13));
    float f = 0.f; memcpy(&f, &u, 4); return f;
}

static int vka_mm_attr_xy_bounds(const vka_dstate *d, const p_draw *dw, const p_draw_idx *di, int32_t cnt, float *mn, float *mx) {
    va_st a = d->attr[0];
    if (a.stream >= 16u) return 0;
    int half = (a.fmt == NVN_FORMAT_RG16F || a.fmt == NVN_FORMAT_RGB16F || a.fmt == NVN_FORMAT_RGBA16F);
    int flt = (a.fmt == NVN_FORMAT_RG32F || a.fmt == NVN_FORMAT_RGB32F || a.fmt == NVN_FORMAT_RGBA32F);
    if (!half && !flt) return 0;
    uint32_t sd = d->stream[a.stream].stride; uint64_t base = d->vbuf[a.stream].address;
    if (!base) return 0;
    int32_t lim = cnt < 16 ? cnt : 16;
    for (int32_t v = 0; v < lim; v++) {
        int64_t vi = dw ? (int64_t)dw->first + v : -1;
        if (di) {
            uint32_t ix = 0; size_t isz = di->itype == 0 ? 1u : di->itype == 1 ? 2u : 4u;
            if (!di->ibuf || !vka_cpu_read(di->ibuf + (uint64_t)v * isz, &ix, isz)) return 0;
            vi = (int64_t)di->base_vertex + (int64_t)ix;
        }
        if (vi < 0) return 0;
        uint8_t b[8] = {0}; float xy[2] = {0.f, 0.f};
        if (!vka_cpu_read(base + (uint64_t)vi * sd + a.offset, b, half ? 4u : 8u)) return 0;
        if (half) { uint16_t h0, h1; memcpy(&h0, b, 2); memcpy(&h1, b + 2, 2); xy[0] = vka_mm_f16(h0); xy[1] = vka_mm_f16(h1); }
        else memcpy(xy, b, 8);
        if (v == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else { if (xy[0] < mn[0]) mn[0] = xy[0]; if (xy[0] > mx[0]) mx[0] = xy[0]; if (xy[1] < mn[1]) mn[1] = xy[1]; if (xy[1] > mx[1]) mx[1] = xy[1]; }
    }
    return 1;
}

static void vka_minimap_note_draw(
    const vka_dstate *d, const p_draw *dw, const p_draw_idx *di, int mesh, uint64_t ind,
    uint32_t stt, uint32_t dt, uint32_t c0_mask) {
    if (!g_vka_mm.in_hud_pass || mesh || ind || stt == 0u || dt != 0u || (c0_mask & 0xfu) != 0u) return;
    int32_t cnt = dw ? dw->count : (di ? di->count : 0);
    if (cnt < 4 || cnt > 12 || d->nattr < 1) return;
    float mn[2] = {0}, mx[2] = {0};
    if (!vka_mm_attr_xy_bounds(d, dw, di, cnt, mn, mx)) return;
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    if (bw < h * 0.14f || bw > h * 0.46f || fabsf(bw - bh) > fmaxf(12.f, bw * 0.10f)) return;
    float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    int ll = vka_origin_ll();
    if (cx < w * 0.50f || cx > w * 0.99f) return;
    if ((!ll && cy < h * 0.35f) || (ll && cy > h * 0.65f)) return;
    uint32_t side = (uint32_t)lrintf(fmaxf(bw, bh));
    if (side < VKA_MM_MIN_SIDE) side = VKA_MM_MIN_SIDE;
    if (side > VKA_MM_MAX_SIDE) side = VKA_MM_MAX_SIDE;
    if (side > g_vka_mm.pass_w) side = g_vka_mm.pass_w;
    if (side > g_vka_mm.pass_h) side = g_vka_mm.pass_h;
    int32_t x0 = (int32_t)lrintf(cx - side * 0.5f), y0 = (int32_t)lrintf(cy - side * 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + (int32_t)side > (int32_t)g_vka_mm.pass_w) x0 = (int32_t)g_vka_mm.pass_w - (int32_t)side;
    if (y0 + (int32_t)side > (int32_t)g_vka_mm.pass_h) y0 = (int32_t)g_vka_mm.pass_h - (int32_t)side;
    g_vka_mm.hit_x = x0; g_vka_mm.hit_y = y0; g_vka_mm.hit_side = side; g_vka_mm.pass_hit = 1;
    g_vka_mm.learn_w = g_vka_mm.pass_w; g_vka_mm.learn_h = g_vka_mm.pass_h;
    g_vka_mm.learn_cx = (int32_t)lrintf(cx); g_vka_mm.learn_cy = (int32_t)lrintf(cy);
    g_vka_mm.learn_half = (int32_t)(side / 2u);
}

static void vka_mm_stage_capture(VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0) {
    uint32_t side = g_vka_mm.hit_side;
    if (g_vka_mm.wrote_in_frame || atomic_load_explicit(&g_vka_mm.pending_valid, memory_order_acquire)) return;
    if ((g_vka_mm.frame_counter & 1u) != 0u || (VkDeviceSize)side * side * 4u > VKA_MM_BUF_BYTES) return;
    if (!vka_mm_ensure_ring()) return;
    vka_mm_slot *dst = &g_vka_mm.slot[g_vka_mm.write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1},
        .imageOffset = {g_vka_mm.hit_x, g_vka_mm.hit_y, 0},
        .imageExtent = {side, side, 1}
    };
    vk.vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1, &rg);
    VkBufferMemoryBarrier bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst->buf, .offset = 0, .size = (VkDeviceSize)side * side * 4u
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
    g_vka_mm.wrote_in_frame = 1;
    g_vka_mm.write_w = side; g_vka_mm.write_h = side; g_vka_mm.write_fmt = im->f.vk;
}

static void vka_mm_restore_display0(VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0) {
    if (!g_vka_mm.save_ready) return;
    int32_t pad = (int32_t)(g_vka_mm.hit_side / 24u);
    if (pad < 12) pad = 12; else if (pad > 32) pad = 32;
    int32_t x0 = g_vka_mm.hit_x - pad, y0 = g_vka_mm.hit_y - pad;
    int32_t x1 = g_vka_mm.hit_x + (int32_t)g_vka_mm.hit_side + pad;
    int32_t y1 = g_vka_mm.hit_y + (int32_t)g_vka_mm.hit_side + pad;
    int32_t sx0 = g_vka_mm.save_x, sy0 = g_vka_mm.save_y;
    int32_t sx1 = sx0 + (int32_t)g_vka_mm.save_w, sy1 = sy0 + (int32_t)g_vka_mm.save_h;
    if (x0 < sx0) x0 = sx0; if (y0 < sy0) y0 = sy0;
    if (x1 > sx1) x1 = sx1; if (y1 > sy1) y1 = sy1;
    if (x1 <= x0 || y1 <= y0) return;
    vka_barrier(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {x0 - sx0, y0 - sy0, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {x0, y0, 0},
        {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0), 1}
    };
    vk.vkCmdCopyImage(cb, g_vka_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vka_barrier(cb);
}

static void vka_minimap_pass_end(VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0) {
    int was_hud = g_vka_mm.in_hud_pass, hit = g_vka_mm.pass_hit;
    g_vka_mm.in_hud_pass = 0; g_vka_mm.pass_hit = 0;
    if (!was_hud || !hit || !cb || !im || im->img != g_vka_mm.pass_im || !V.dev || vka_hung()) return;
    if (!atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire)) return;
    vka_mm_find_cand(g_vka_mm.hit_side, g_vka_mm.hit_side, im->f.vk, im->serial);
    vka_mm_stage_capture(cb, im, lv, l0);
    vka_mm_restore_display0(cb, im, lv, l0);
    g_vka_mm.saved_hit_frame = vka_frame();
    g_vka_mm.saved_hit_img = im->img;
}

static void vka_minimap_pass_abort(void) {
    g_vka_mm.in_hud_pass = 0;
    g_vka_mm.pass_hit = 0;
}

void vka_minimap_present(void) {
    g_vka_mm.frame_counter++;
    if (g_vka_mm.wrote_in_frame) {
        g_vka_mm.pending_slot = g_vka_mm.write_slot;
        g_vka_mm.pending_fence = vka_fence_queued();
        g_vka_mm.pending_frame = g_vka_mm.frame_counter;
        g_vka_mm.pending_w = g_vka_mm.write_w;
        g_vka_mm.pending_h = g_vka_mm.write_h;
        g_vka_mm.pending_fmt = g_vka_mm.write_fmt;
        atomic_store_explicit(&g_vka_mm.pending_valid, 1, memory_order_release);
        g_vka_mm.wrote_in_frame = 0;
    }
    if (!atomic_load_explicit(&g_vka_mm.pending_valid, memory_order_relaxed)) return;
    int done = (g_vka_mm.frame_counter - g_vka_mm.pending_frame) >= 1u
        && (g_vka_mm.pending_fence == 0 || vka_done() >= g_vka_mm.pending_fence);
    if (!done) return;
    pthread_mutex_lock(&g_vka_mm.mu);
    int prev = g_vka_mm.ready_slot;
    g_vka_mm.ready_slot = g_vka_mm.pending_slot;
    g_vka_mm.ready_w = g_vka_mm.pending_w;
    g_vka_mm.ready_h = g_vka_mm.pending_h;
    g_vka_mm.ready_fmt = g_vka_mm.pending_fmt;
    g_vka_mm.seq++;
    g_vka_mm.write_slot = prev >= 0 ? prev : (g_vka_mm.pending_slot ^ 1);
    atomic_store_explicit(&g_vka_mm.pending_valid, 0, memory_order_release);
    pthread_mutex_unlock(&g_vka_mm.mu);
}

static void vka_mm_copy_rgba(uint8_t *dst, const uint8_t *src, uint32_t w, uint32_t h, VkFormat fmt, int flip) {
    int bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = flip ? (h - 1u - y) : y;
        const uint8_t *sr = src + (size_t)sy * w * 4u;
        uint8_t *dr = dst + (size_t)y * w * 4u;
        for (uint32_t x = 0; x < w; x++) {
            size_t o = (size_t)x * 4u;
            dr[o + 0] = sr[o + (bgra ? 2 : 0)];
            dr[o + 1] = sr[o + 1];
            dr[o + 2] = sr[o + (bgra ? 0 : 2)];
            dr[o + 3] = 255u;
        }
    }
}

__attribute__((visibility("default")))
int nxhost_companion_minimap(uint8_t *rgba_out, int max_bytes, int32_t meta[5]) {
    if (!meta) return 0;
    atomic_store_explicit(&g_vka_mm.want_active, 1, memory_order_release);
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vka_mm.mu);
    int cnt = atomic_load_explicit(&g_vka_mm.cand_count, memory_order_acquire);
    int cidx = cnt > 0 ? (int)(atomic_load_explicit(&g_vka_mm.cand_idx, memory_order_acquire) % (uint32_t)cnt) : 0;
    int rslot = g_vka_mm.ready_slot;
    uint32_t w = g_vka_mm.ready_w, h = g_vka_mm.ready_h, seq = g_vka_mm.seq;
    if (rslot >= 0 && rslot < VKA_MM_SLOTS && g_vka_mm.slot[rslot].ptr && seq != client_seq) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && (size_t)max_bytes >= need) {
            vka_mm_copy_rgba(rgba_out, g_vka_mm.slot[rslot].ptr, w, h, g_vka_mm.ready_fmt, vka_origin_ll());
            updated = 1;
        }
    }
    meta[0] = (int32_t)w;
    meta[1] = (int32_t)h;
    meta[2] = (int32_t)seq;
    meta[3] = cnt;
    meta[4] = cidx;
    pthread_mutex_unlock(&g_vka_mm.mu);
    return updated;
}

__attribute__((visibility("default")))
int nxhost_companion_minimap_cycle(void) {
    uint32_t next = atomic_fetch_add_explicit(&g_vka_mm.cand_idx, 1u, memory_order_acq_rel) + 1u;
    int cnt = atomic_load_explicit(&g_vka_mm.cand_count, memory_order_acquire);
    return cnt > 0 ? (int)(next % (uint32_t)cnt) : 0;
}

__attribute__((visibility("default")))
void nxhost_companion_minimap_enable(int on) {
    atomic_store_explicit(&g_vka_mm.want_active, on ? 1 : 0, memory_order_release);
}
