// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan HUD minimap & objective redirection (2-slot staging rings) + one-shot UI texture harvest.
// Pre-clears learned HUD boxes to opaque black so redirected widgets have zero 3D bleed-through, then restores Display 0.

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    VKA_MM_MIN_SIDE = 96u,
    VKA_MM_MAX_SIDE = 768u,
    VKA_MM_SAVE_W = 896u,
    VKA_MM_SAVE_H = 896u,
    VKA_OBJ_MAX_W = 768u,
    VKA_OBJ_MAX_H = 224u,
    VKA_UI_TEX_SIDE = 512u,
    VKA_MM_SLOTS = 2,
    VKA_MM_MAX_CAND = 8,
    VKA_MM_TTL = 3u
};
#define VKA_MM_BUF_BYTES ((VkDeviceSize)(VKA_MM_MAX_SIDE * VKA_MM_MAX_SIDE * 4u))
#define VKA_OBJ_BUF_BYTES ((VkDeviceSize)(VKA_OBJ_MAX_W * VKA_OBJ_MAX_H * 4u))
#define VKA_UI_BUF_BYTES ((VkDeviceSize)(VKA_UI_TEX_SIDE * VKA_UI_TEX_SIDE * 4u))

uint64_t vka_frame(void);
uint64_t vka_done(void);
uint64_t vka_fence_queued(void);
int vka_origin_ll(void);
int vka_cpu_read(uint64_t addr, void *out, size_t n);

typedef struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *ptr; } vka_mm_slot;
typedef struct { uint32_t w, h; VkFormat fmt; uint64_t serial; } vka_mm_cand;
typedef struct { int32_t x, y; uint32_t w, h; } vka_mm_rect;
typedef struct {
    VkImage img; uint32_t w, h; VkFormat fmt; uint64_t serial; int score;
} vka_ui_cand;

typedef struct {
    vka_mm_slot slot[VKA_MM_SLOTS];
    VkDeviceSize slot_bytes;
    int ring_ready, ring_failed;
    _Atomic int pending_valid;
    int write_slot, pending_slot, ready_slot, wrote_in_frame;
    uint32_t pending_frame, write_w, write_h, pending_w, pending_h, ready_w, ready_h, seq;
    VkFormat write_fmt, pending_fmt, ready_fmt;
    uint64_t pending_fence;
} vka_mm_stream;

static struct {
    pthread_mutex_t mu;
    vka_mm_stream mm, obj;
    vka_mm_slot ui_slot;
    VkImage ui_img;
    VkDeviceMemory ui_mem;
    int ui_ready, ui_failed, ui_wrote_frame, ui_pending, ui_has_ready;
    uint32_t ui_pending_frame, ui_seq;
    uint64_t ui_pending_fence, ui_seen[8];
    int ui_seen_n, ui_seen_pos;
    _Atomic int ui_want;
    vka_ui_cand ui_cand;
    VkImage save_img;
    VkDeviceMemory save_mem;
    VkFormat save_fmt;
    int save_ready, save_failed;
    vka_mm_cand cand[VKA_MM_MAX_CAND];
    _Atomic int cand_count, want_active;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, mm_ttl, obj_ttl;
    int saw_mm_frame, saw_obj_frame;
    int in_hud_pass, pass_opaque_fs, pass_mm_hit, pass_obj_hit, pc_mm_done, pc_obj_done;
    VkImage pass_im, saved_hit_img;
    uint64_t frame_got_mm, frame_got_obj;
    uint32_t pass_w, pass_h, pass_lv, pass_l0;
    vka_mm_rect save_box, pc_mm, pc_obj, hit_mm, hit_obj;
    uint32_t learn_mm_w, learn_mm_h;
    int32_t learn_mm_cx, learn_mm_cy, learn_mm_half;
    uint32_t learn_obj_w, learn_obj_h;
    vka_mm_rect learn_obj;
    float obj_umin_x, obj_umin_y, obj_umax_x, obj_umax_y;
} g_vka_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .mm = { .slot_bytes = VKA_MM_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
    .obj = { .slot_bytes = VKA_OBJ_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
};

static int vka_mm_supported_fmt(VkFormat fmt) {
    return fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB
        || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
}

static int vka_mm_is_color4_fmt(VkFormat f) {
    if (f == VK_FORMAT_R8_UNORM || f == VK_FORMAT_R8_SNORM || f == VK_FORMAT_R16_UNORM
            || f == VK_FORMAT_R16_SFLOAT || f == VK_FORMAT_R32_SFLOAT
            || f == VK_FORMAT_R8G8_UNORM || f == VK_FORMAT_R8G8_SNORM || f == VK_FORMAT_R16G16_UNORM
            || f == VK_FORMAT_R16G16_SFLOAT || f == VK_FORMAT_R32G32_SFLOAT
            || f == VK_FORMAT_BC4_UNORM_BLOCK || f == VK_FORMAT_BC4_SNORM_BLOCK
            || f == VK_FORMAT_BC5_UNORM_BLOCK || f == VK_FORMAT_BC5_SNORM_BLOCK) return 0;
    return f != VK_FORMAT_UNDEFINED;
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

static void vka_mm_free_slot(vka_mm_slot *s) {
    if (s->ptr && s->mem) vk.vkUnmapMemory(V.dev, s->mem);
    if (s->buf) vk.vkDestroyBuffer(V.dev, s->buf, NULL);
    if (s->mem) vk.vkFreeMemory(V.dev, s->mem, NULL);
    memset(s, 0, sizeof *s);
}

static int vka_mm_alloc_slot(vka_mm_slot *s, VkDeviceSize sz) {
    VkBufferCreateInfo bi = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sz, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    if (vk.vkCreateBuffer(V.dev, &bi, NULL, &s->buf)) return 0;
    VkMemoryRequirements mr;
    vk.vkGetBufferMemoryRequirements(V.dev, s->buf, &mr);
    uint32_t t = vka_mem_type(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
    if (t == UINT32_MAX) {
        t = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    }
    if (t == UINT32_MAX) { vka_mm_free_slot(s); return 0; }
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = t};
    void *p = NULL;
    if (vk.vkAllocateMemory(V.dev, &ai, NULL, &s->mem)
            || vk.vkBindBufferMemory(V.dev, s->buf, s->mem, 0)
            || vk.vkMapMemory(V.dev, s->mem, 0, VK_WHOLE_SIZE, 0, &p)) {
        vka_mm_free_slot(s);
        return 0;
    }
    s->ptr = (uint8_t *)p;
    return 1;
}

static int vka_mm_ensure_stream_ring(vka_mm_stream *st) {
    if (st->ring_ready) return 1;
    if (st->ring_failed || !V.dev) return 0;
    for (int i = 0; i < VKA_MM_SLOTS; i++) {
        if (!vka_mm_alloc_slot(&st->slot[i], st->slot_bytes)) {
            for (int j = 0; j < i; j++) vka_mm_free_slot(&st->slot[j]);
            st->ring_failed = 1;
            return 0;
        }
    }
    st->ring_ready = 1;
    return 1;
}

static int vka_mm_create_img(
    VkCommandBuffer cb, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
    VkImage *im_out, VkDeviceMemory *mem_out) {
    VkImageCreateInfo ci = {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
        .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (vk.vkCreateImage(V.dev, &ci, NULL, im_out)) return 0;
    VkMemoryRequirements mr;
    vk.vkGetImageMemoryRequirements(V.dev, *im_out, &mr);
    uint32_t t = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (t == UINT32_MAX) t = vka_mem_type(mr.memoryTypeBits, 0, 0);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = t};
    if (t == UINT32_MAX || vk.vkAllocateMemory(V.dev, &ai, NULL, mem_out)
            || vk.vkBindImageMemory(V.dev, *im_out, *mem_out, 0)) {
        vk.vkDestroyImage(V.dev, *im_out, NULL);
        if (*mem_out) vk.vkFreeMemory(V.dev, *mem_out, NULL);
        *im_out = VK_NULL_HANDLE; *mem_out = VK_NULL_HANDLE;
        return 0;
    }
    VkImageMemoryBarrier ib = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *im_out, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    return 1;
}

static int vka_mm_ensure_save(VkCommandBuffer cb, VkFormat fmt) {
    if (g_vka_mm.save_ready) {
        if (g_vka_mm.save_fmt == fmt) return 1;
        vk.vkDestroyImage(V.dev, g_vka_mm.save_img, NULL);
        vk.vkFreeMemory(V.dev, g_vka_mm.save_mem, NULL);
        g_vka_mm.save_img = VK_NULL_HANDLE; g_vka_mm.save_mem = VK_NULL_HANDLE; g_vka_mm.save_ready = 0;
    }
    if (g_vka_mm.save_failed || !V.dev || !cb) return 0;
    if (!vka_mm_create_img(cb, VKA_MM_SAVE_W, VKA_MM_SAVE_H, fmt,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &g_vka_mm.save_img, &g_vka_mm.save_mem)) {
        g_vka_mm.save_failed = 1;
        return 0;
    }
    g_vka_mm.save_fmt = fmt;
    g_vka_mm.save_ready = 1;
    return 1;
}

static vka_mm_rect vka_mm_clamp_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const vka_mm_rect *lim) {
    int32_t lx0 = lim->x, ly0 = lim->y, lx1 = lim->x + (int32_t)lim->w, ly1 = lim->y + (int32_t)lim->h;
    if (x0 < lx0) x0 = lx0;
    if (y0 < ly0) y0 = ly0;
    if (x1 > lx1) x1 = lx1;
    if (y1 > ly1) y1 = ly1;
    return (vka_mm_rect){x0, y0, (uint32_t)(x1 > x0 ? x1 - x0 : 0), (uint32_t)(y1 > y0 ? y1 - y0 : 0)};
}

static vka_mm_rect vka_mm_union_rect(const vka_mm_rect *a, const vka_mm_rect *b) {
    if (!a->w || !a->h) return *b;
    if (!b->w || !b->h) return *a;
    int32_t x0 = a->x < b->x ? a->x : b->x, y0 = a->y < b->y ? a->y : b->y;
    int32_t x1 = (a->x + (int32_t)a->w) > (b->x + (int32_t)b->w) ? (a->x + (int32_t)a->w) : (b->x + (int32_t)b->w);
    int32_t y1 = (a->y + (int32_t)a->h) > (b->y + (int32_t)b->h) ? (a->y + (int32_t)a->h) : (b->y + (int32_t)b->h);
    return (vka_mm_rect){x0, y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

static vka_mm_rect vka_mm_pad_rect(const vka_mm_rect *r, int32_t px, int32_t py, uint32_t pw, uint32_t ph) {
    vka_mm_rect fb = {0, 0, pw, ph};
    return vka_mm_clamp_rect(r->x - px, r->y - py, r->x + (int32_t)r->w + px, r->y + (int32_t)r->h + py, &fb);
}

static vka_mm_rect vka_mm_compute_save_box(uint32_t w, uint32_t h) {
    int32_t sw = (int32_t)(w < VKA_MM_SAVE_W ? w : VKA_MM_SAVE_W);
    int32_t sh = (int32_t)(h < VKA_MM_SAVE_H ? h : VKA_MM_SAVE_H);
    int ll = vka_origin_ll();
    int32_t x1 = (int32_t)w, x0 = x1 - sw;
    int32_t y0 = ll ? 0 : (int32_t)h - sh, y1 = ll ? sh : (int32_t)h;
    if (g_vka_mm.learn_mm_w == w && g_vka_mm.learn_mm_h == h && g_vka_mm.learn_mm_half > 0) {
        int32_t half = g_vka_mm.learn_mm_half + 48;
        if (half > (int32_t)(VKA_MM_SAVE_W / 2u)) half = (int32_t)(VKA_MM_SAVE_W / 2u);
        vka_mm_rect mm = vka_mm_pad_rect(&(vka_mm_rect){g_vka_mm.learn_mm_cx - half,
            g_vka_mm.learn_mm_cy - half, (uint32_t)(half * 2), (uint32_t)(half * 2)}, 0, 0, w, h);
        vka_mm_rect u = mm;
        if (g_vka_mm.learn_obj_w == w && g_vka_mm.learn_obj_h == h && g_vka_mm.learn_obj.w > 0u) {
            vka_mm_rect ob = vka_mm_pad_rect(&g_vka_mm.learn_obj, 24, 24, w, h);
            vka_mm_rect cand = vka_mm_union_rect(&mm, &ob);
            if (cand.w <= VKA_MM_SAVE_W && cand.h <= VKA_MM_SAVE_H) u = cand;
        }
        int32_t ux0 = u.x, uy0 = u.y, ux1 = u.x + (int32_t)u.w, uy1 = u.y + (int32_t)u.h;
        if (ux0 < x0 && ux1 <= x1) x0 = ux1 - sw > 0 ? ux1 - sw : 0;
        if (ux0 >= x0 && ux1 <= x1 && uy0 >= y0 && uy1 <= y1) return (vka_mm_rect){x0, y0, (uint32_t)sw, (uint32_t)sh};
        x0 = ux0; x1 = ux1; y0 = uy0; y1 = uy1;
        if (x1 - x0 < sw) { int32_t nx0 = x1 - sw; x0 = nx0 > 0 ? nx0 : 0; if (x0 + sw <= (int32_t)w) x1 = x0 + sw; }
        if (y1 - y0 < sh) {
            if (!ll) { int32_t ny0 = y1 - sh; y0 = ny0 > 0 ? ny0 : 0; if (y0 + sh <= (int32_t)h) y1 = y0 + sh; }
            else { y1 = y0 + sh <= (int32_t)h ? y0 + sh : (int32_t)h; }
        }
    }
    return (vka_mm_rect){x0, y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

static void vka_minimap_pass_begin(
    VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0, uint32_t nl,
    uint32_t w, uint32_t h, int load_c0, int has_stencil) {
    g_vka_mm.in_hud_pass = 0; g_vka_mm.pass_opaque_fs = 0;
    g_vka_mm.pass_mm_hit = 0; g_vka_mm.pass_obj_hit = 0;
    g_vka_mm.pc_mm_done = 0; g_vka_mm.pc_obj_done = 0;
    g_vka_mm.ui_cand.img = VK_NULL_HANDLE; g_vka_mm.ui_cand.score = -1;
    if (!cb || !im || !im->img || !V.dev || vka_hung()) return;
    if (!atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire)) return;
    if (!load_c0 || !has_stencil || nl != 1u || im->samples > 1u || w < 640u || h < 360u) return;
    if (!vka_mm_supported_fmt(im->f.vk)
            || (im->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
                != (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return;
    uint64_t fr = vka_frame();
    if (g_vka_mm.frame_got_mm == fr && g_vka_mm.frame_got_obj == fr && g_vka_mm.saved_hit_img == im->img) return;
    if (!vka_mm_ensure_save(cb, im->f.vk)) return;
    vka_mm_rect sb = vka_mm_compute_save_box(w, h);
    if (!sb.w || !sb.h) return;
    vka_barrier(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {sb.x, sb.y, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {sb.w, sb.h, 1}
    };
    vk.vkCmdCopyImage(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, g_vka_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vka_barrier(cb);
    g_vka_mm.in_hud_pass = 1; g_vka_mm.pass_im = im->img;
    g_vka_mm.pass_w = w; g_vka_mm.pass_h = h; g_vka_mm.pass_lv = lv; g_vka_mm.pass_l0 = l0;
    g_vka_mm.save_box = sb;
}

static void vka_mm_clear_box(VkCommandBuffer cb, const vka_mm_rect *r) {
    VkClearAttachment ca = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, {.color = {.float32 = {0.f, 0.f, 0.f, 1.f}}}};
    VkClearRect cr = {{{r->x, r->y}, {r->w, r->h}}, 0u, 1u};
    vk.vkCmdClearAttachments(cb, 1u, &ca, 1u, &cr);
}

static void vka_minimap_pass_preclear(VkCommandBuffer cb) {
    if (!g_vka_mm.in_hud_pass || !cb || !V.dev) return;
    uint64_t fr = vka_frame();
    if (g_vka_mm.mm_ttl > 0u && g_vka_mm.frame_got_mm != fr
            && g_vka_mm.learn_mm_w == g_vka_mm.pass_w && g_vka_mm.learn_mm_h == g_vka_mm.pass_h
            && g_vka_mm.learn_mm_half > 0) {
        int32_t half = g_vka_mm.learn_mm_half;
        int32_t pad = half / 12;
        if (pad < 12) pad = 12; else if (pad > 32) pad = 32;
        vka_mm_rect r = vka_mm_clamp_rect(g_vka_mm.learn_mm_cx - half - pad, g_vka_mm.learn_mm_cy - half - pad,
            g_vka_mm.learn_mm_cx + half + pad, g_vka_mm.learn_mm_cy + half + pad, &g_vka_mm.save_box);
        if (r.w > 0u && r.h > 0u) { g_vka_mm.pc_mm = r; g_vka_mm.pc_mm_done = 1; vka_mm_clear_box(cb, &r); }
    }
    if (g_vka_mm.obj_ttl > 0u && g_vka_mm.frame_got_obj != fr
            && g_vka_mm.learn_obj_w == g_vka_mm.pass_w && g_vka_mm.learn_obj_h == g_vka_mm.pass_h
            && g_vka_mm.learn_obj.w > 0u && g_vka_mm.learn_obj.h > 0u) {
        vka_mm_rect ob = g_vka_mm.learn_obj;
        vka_mm_rect r = vka_mm_clamp_rect(ob.x - 16, ob.y - 16,
            ob.x + (int32_t)ob.w + 16, ob.y + (int32_t)ob.h + 16, &g_vka_mm.save_box);
        if (r.w > 0u && r.h > 0u) { g_vka_mm.pc_obj = r; g_vka_mm.pc_obj_done = 1; vka_mm_clear_box(cb, &r); }
    }
}

static float vka_mm_f16(uint16_t h) {
    uint32_t sg = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31u, m = h & 1023u;
    if (e == 0u) return m ? ((sg ? -1.f : 1.f) * (float)m / 16777216.f) : (sg ? -0.f : 0.f);
    uint32_t u = (e == 31u) ? (sg | 0x7f800000u | (m << 13)) : (sg | ((e + 112u) << 23) | (m << 13));
    float f = 0.f; memcpy(&f, &u, 4); return f;
}

static int vka_mm_attr_xy_bounds(
    const vka_dstate *d, const p_draw *dw, const p_draw_idx *di, int32_t cnt, float *mn, float *mx) {
    va_st a = d->attr[0];
    if (a.stream >= 16u || cnt <= 0) return 0;
    int half = (a.fmt == NVN_FORMAT_RG16F || a.fmt == NVN_FORMAT_RGB16F || a.fmt == NVN_FORMAT_RGBA16F);
    int flt = (a.fmt == NVN_FORMAT_RG32F || a.fmt == NVN_FORMAT_RGB32F || a.fmt == NVN_FORMAT_RGBA32F);
    if (!half && !flt) return 0;
    uint32_t sd = d->stream[a.stream].stride; uint64_t base = d->vbuf[a.stream].address;
    if (!base) return 0;
    int32_t lim = cnt < 256 ? cnt : 256;
    for (int32_t i = 0; i < lim; i++) {
        int32_t v = cnt <= 256 ? i : (int32_t)(((int64_t)i * cnt) / lim);
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
        if (i == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else { if (xy[0] < mn[0]) mn[0] = xy[0]; if (xy[0] > mx[0]) mx[0] = xy[0]; if (xy[1] < mn[1]) mn[1] = xy[1]; if (xy[1] > mx[1]) mx[1] = xy[1]; }
    }
    return 1;
}

static void vka_mm_try_minimap(const float *mn, const float *mx) {
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
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x0 + (int32_t)side > (int32_t)g_vka_mm.pass_w) x0 = (int32_t)g_vka_mm.pass_w - (int32_t)side;
    if (y0 + (int32_t)side > (int32_t)g_vka_mm.pass_h) y0 = (int32_t)g_vka_mm.pass_h - (int32_t)side;
    g_vka_mm.hit_mm = (vka_mm_rect){x0, y0, side, side};
    g_vka_mm.pass_mm_hit = 1;
    g_vka_mm.learn_mm_w = g_vka_mm.pass_w; g_vka_mm.learn_mm_h = g_vka_mm.pass_h;
    g_vka_mm.learn_mm_cx = (int32_t)lrintf(cx); g_vka_mm.learn_mm_cy = (int32_t)lrintf(cy);
    g_vka_mm.learn_mm_half = (int32_t)(side / 2u);
}

static void vka_mm_try_objective(const float *mn, const float *mx) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    float cy = (mn[1] + mx[1]) * 0.5f;
    if (mn[0] < w * 0.62f || mx[0] > w * 0.995f || cy < h * 0.28f || cy > h * 0.62f) return;
    if (bw < h * 0.08f || bw > w * 0.36f || bh < h * 0.015f || bh > h * 0.14f || bw < bh * 1.4f) return;
    if (!g_vka_mm.pass_obj_hit) {
        g_vka_mm.obj_umin_x = mn[0]; g_vka_mm.obj_umin_y = mn[1];
        g_vka_mm.obj_umax_x = mx[0]; g_vka_mm.obj_umax_y = mx[1];
    } else {
        if (mn[0] < g_vka_mm.obj_umin_x) g_vka_mm.obj_umin_x = mn[0];
        if (mn[1] < g_vka_mm.obj_umin_y) g_vka_mm.obj_umin_y = mn[1];
        if (mx[0] > g_vka_mm.obj_umax_x) g_vka_mm.obj_umax_x = mx[0];
        if (mx[1] > g_vka_mm.obj_umax_y) g_vka_mm.obj_umax_y = mx[1];
    }
    int32_t x0 = (int32_t)floorf(g_vka_mm.obj_umin_x - h * 0.085f), x1 = (int32_t)g_vka_mm.pass_w;
    int32_t y0 = (int32_t)floorf(g_vka_mm.obj_umin_y - h * 0.026f);
    int32_t y1 = (int32_t)ceilf(g_vka_mm.obj_umax_y + h * 0.026f);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (y1 > (int32_t)g_vka_mm.pass_h) y1 = (int32_t)g_vka_mm.pass_h;
    if (x1 - x0 > (int32_t)VKA_OBJ_MAX_W) x0 = x1 - (int32_t)VKA_OBJ_MAX_W;
    if (y1 - y0 > (int32_t)VKA_OBJ_MAX_H) y1 = y0 + (int32_t)VKA_OBJ_MAX_H;
    int32_t ow = (x1 - x0) & ~3;
    if (ow < 64 || y1 <= y0) return;
    x0 = x1 - ow;
    vka_mm_rect ob = {x0, y0, (uint32_t)ow, (uint32_t)(y1 - y0)};
    if (ob.h > VKA_OBJ_MAX_H) ob.h = VKA_OBJ_MAX_H;
    g_vka_mm.hit_obj = ob; g_vka_mm.pass_obj_hit = 1;
    g_vka_mm.learn_obj = ob; g_vka_mm.learn_obj_w = g_vka_mm.pass_w; g_vka_mm.learn_obj_h = g_vka_mm.pass_h;
}

static void vka_minimap_note_draw(
    const vka_dstate *d, const p_draw *dw, const p_draw_idx *di, int mesh, uint64_t ind,
    uint32_t stt, uint32_t dt, uint32_t c0_mask, uint32_t blend_en) {
    if (!g_vka_mm.in_hud_pass || mesh || ind || dt != 0u || d->nattr < 1) return;
    int32_t cnt = dw ? dw->count : (di ? di->count : 0);
    float mn[2] = {0}, mx[2] = {0};
    if (stt != 0u && (c0_mask & 0xfu) == 0u && cnt >= 4 && cnt <= 12) {
        if (vka_mm_attr_xy_bounds(d, dw, di, cnt, mn, mx)) vka_mm_try_minimap(mn, mx);
        return;
    }
    if (stt == 0u && (c0_mask & 0xfu) != 0u && !blend_en && cnt >= 3 && cnt <= 6) {
        if (vka_mm_attr_xy_bounds(d, dw, di, cnt, mn, mx)) {
            float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
            int px_fs = (bw >= g_vka_mm.pass_w * 0.90f && bh >= g_vka_mm.pass_h * 0.90f);
            int ndc_fs = (mn[0] <= -0.95f && mx[0] >= 0.95f && mn[1] <= -0.95f && mx[1] >= 0.95f);
            if (px_fs || ndc_fs) g_vka_mm.pass_opaque_fs = 1;
        }
        return;
    }
    if (stt == 0u && (c0_mask & 0xfu) != 0u && blend_en && cnt >= 16 && cnt <= 600) {
        if (vka_mm_attr_xy_bounds(d, dw, di, cnt, mn, mx)) vka_mm_try_objective(mn, mx);
    }
}

static int vka_minimap_want_ui_tex(void) {
    return g_vka_mm.in_hud_pass && !g_vka_mm.pass_opaque_fs
        && atomic_load_explicit(&g_vka_mm.ui_want, memory_order_relaxed) == 1;
}

static int vka_mm_ui_seen(uint64_t serial) {
    for (int i = 0; i < g_vka_mm.ui_seen_n; i++) if (g_vka_mm.ui_seen[i] == serial) return 1;
    return 0;
}

static void vka_minimap_note_fs_tex(vka_img *im, uint32_t dt) {
    if (!vka_minimap_want_ui_tex() || dt != 0u || !im || !im->img || im->gpu_owned) return;
    if (im->img == g_vka_mm.pass_im || im->layers != 1u || im->samples != 1u || im->depth > 1u) return;
    if (im->aspect != VK_IMAGE_ASPECT_COLOR_BIT || !(im->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return;
    if (im->w < 128u || im->h < 128u || im->w > 2048u || im->h > 2048u) return;
    if ((im->w == g_vka_mm.pass_w && im->h == g_vka_mm.pass_h)
            || (im->w == 1280u && im->h == 720u) || (im->w == 1920u && im->h == 1080u)) return;
    if (!vka_mm_is_color4_fmt(im->f.vk) || vka_mm_ui_seen(im->serial)) return;
    VkFormatProperties fp;
    vk.vkGetPhysicalDeviceFormatProperties(V.pd, im->f.vk, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) return;
    int score = (im->w == im->h ? 20 : 5) + (im->w >= 512u && im->h >= 512u ? 20 : (im->w >= 256u ? 10 : 0));
    if (score > g_vka_mm.ui_cand.score) {
        g_vka_mm.ui_cand = (vka_ui_cand){im->img, im->w, im->h, im->f.vk, im->serial, score};
    }
}

static void vka_mm_stream_stage(
    vka_mm_stream *st, VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0, const vka_mm_rect *r) {
    if (st->wrote_in_frame || atomic_load_explicit(&st->pending_valid, memory_order_acquire)) return;
    VkDeviceSize need = (VkDeviceSize)r->w * r->h * 4u;
    if ((g_vka_mm.frame_counter & 1u) != 0u || !r->w || !r->h || need > st->slot_bytes) return;
    if (!vka_mm_ensure_stream_ring(st)) return;
    vka_mm_slot *dst = &st->slot[st->write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1},
        .imageOffset = {r->x, r->y, 0}, .imageExtent = {r->w, r->h, 1}
    };
    vk.vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1, &rg);
    VkBufferMemoryBarrier bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst->buf, .offset = 0, .size = need
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
    st->wrote_in_frame = 1; st->write_w = r->w; st->write_h = r->h; st->write_fmt = im->f.vk;
}

static void vka_mm_restore_box(
    VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0, const vka_mm_rect *raw) {
    if (!g_vka_mm.save_ready) return;
    vka_mm_rect r = vka_mm_clamp_rect(raw->x, raw->y, raw->x + (int32_t)raw->w, raw->y + (int32_t)raw->h, &g_vka_mm.save_box);
    if (!r.w || !r.h) return;
    vka_barrier(cb);
    VkImageCopy cp = {
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {r.x - g_vka_mm.save_box.x, r.y - g_vka_mm.save_box.y, 0},
        {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1}, {r.x, r.y, 0}, {r.w, r.h, 1}
    };
    vk.vkCmdCopyImage(cb, g_vka_mm.save_img, VK_IMAGE_LAYOUT_GENERAL, im->img, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);
    vka_barrier(cb);
}

static int vka_mm_ensure_ui(VkCommandBuffer cb) {
    if (g_vka_mm.ui_ready) return 1;
    if (g_vka_mm.ui_failed) return 0;
    VkFormatProperties fp;
    vk.vkGetPhysicalDeviceFormatProperties(V.pd, VK_FORMAT_R8G8B8A8_UNORM, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)
            || !vka_mm_alloc_slot(&g_vka_mm.ui_slot, VKA_UI_BUF_BYTES)
            || !vka_mm_create_img(cb, VKA_UI_TEX_SIDE, VKA_UI_TEX_SIDE, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    &g_vka_mm.ui_img, &g_vka_mm.ui_mem)) {
        vka_mm_free_slot(&g_vka_mm.ui_slot); g_vka_mm.ui_failed = 1; return 0;
    }
    g_vka_mm.ui_ready = 1; return 1;
}

static void vka_mm_stage_ui_tex(VkCommandBuffer cb) {
    vka_ui_cand *c = &g_vka_mm.ui_cand;
    if (!c->img || !vka_mm_ensure_ui(cb)) return;
    vka_barrier(cb);
    VkImageBlit bl = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffsets = {{0, 0, 0}, {(int32_t)c->w, (int32_t)c->h, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstOffsets = {{0, 0, 0}, {(int32_t)VKA_UI_TEX_SIDE, (int32_t)VKA_UI_TEX_SIDE, 1}},
    };
    vk.vkCmdBlitImage(cb, c->img, VK_IMAGE_LAYOUT_GENERAL, g_vka_mm.ui_img, VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR);
    vka_barrier(cb);
    VkBufferImageCopy rg = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {VKA_UI_TEX_SIDE, VKA_UI_TEX_SIDE, 1}
    };
    vk.vkCmdCopyImageToBuffer(cb, g_vka_mm.ui_img, VK_IMAGE_LAYOUT_GENERAL, g_vka_mm.ui_slot.buf, 1, &rg);
    VkBufferMemoryBarrier bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = g_vka_mm.ui_slot.buf, .offset = 0, .size = VKA_UI_BUF_BYTES
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
    g_vka_mm.ui_wrote_frame = 1;
    g_vka_mm.ui_seen[g_vka_mm.ui_seen_pos] = c->serial;
    g_vka_mm.ui_seen_pos = (g_vka_mm.ui_seen_pos + 1) & 7;
    if (g_vka_mm.ui_seen_n < 8) g_vka_mm.ui_seen_n++;
    atomic_store_explicit(&g_vka_mm.ui_want, 2, memory_order_release);
}

static void vka_minimap_pass_end(VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0) {
    int was = g_vka_mm.in_hud_pass, mh = g_vka_mm.pass_mm_hit, oh = g_vka_mm.pass_obj_hit, op = g_vka_mm.pass_opaque_fs;
    g_vka_mm.in_hud_pass = 0; g_vka_mm.pass_mm_hit = 0; g_vka_mm.pass_obj_hit = 0; g_vka_mm.pass_opaque_fs = 0;
    if (!was || !cb || !im || im->img != g_vka_mm.pass_im || !V.dev || vka_hung()) return;
    if (!atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire)) return;
    if (op && !mh && !oh) return;
    uint64_t fr = vka_frame();
    if (mh) {
        vka_mm_find_cand(g_vka_mm.hit_mm.w, g_vka_mm.hit_mm.h, im->f.vk, im->serial);
        vka_mm_stream_stage(&g_vka_mm.mm, cb, im, lv, l0, &g_vka_mm.hit_mm);
        int32_t pad = (int32_t)(g_vka_mm.hit_mm.w / 24u);
        if (pad < 12) pad = 12; else if (pad > 32) pad = 32;
        vka_mm_rect pm = vka_mm_pad_rect(&g_vka_mm.hit_mm, pad, pad, g_vka_mm.pass_w, g_vka_mm.pass_h);
        vka_mm_rect u = g_vka_mm.pc_mm_done ? vka_mm_union_rect(&g_vka_mm.pc_mm, &pm) : pm;
        vka_mm_restore_box(cb, im, lv, l0, &u);
        g_vka_mm.saw_mm_frame = 1; g_vka_mm.frame_got_mm = fr; g_vka_mm.saved_hit_img = im->img;
    } else if (g_vka_mm.pc_mm_done) {
        vka_mm_restore_box(cb, im, lv, l0, &g_vka_mm.pc_mm);
    }
    if (oh) {
        vka_mm_stream_stage(&g_vka_mm.obj, cb, im, lv, l0, &g_vka_mm.hit_obj);
        vka_mm_rect po = vka_mm_pad_rect(&g_vka_mm.hit_obj, 16, 16, g_vka_mm.pass_w, g_vka_mm.pass_h);
        vka_mm_rect u = g_vka_mm.pc_obj_done ? vka_mm_union_rect(&g_vka_mm.pc_obj, &po) : po;
        vka_mm_restore_box(cb, im, lv, l0, &u);
        g_vka_mm.saw_obj_frame = 1; g_vka_mm.frame_got_obj = fr; g_vka_mm.saved_hit_img = im->img;
    } else if (g_vka_mm.pc_obj_done) {
        vka_mm_restore_box(cb, im, lv, l0, &g_vka_mm.pc_obj);
    }
    if ((mh || oh) && atomic_load_explicit(&g_vka_mm.ui_want, memory_order_relaxed) == 1) vka_mm_stage_ui_tex(cb);
}

static void vka_minimap_pass_abort(void) {
    g_vka_mm.in_hud_pass = 0; g_vka_mm.pass_mm_hit = 0; g_vka_mm.pass_obj_hit = 0; g_vka_mm.pass_opaque_fs = 0;
}

static void vka_mm_stream_present(vka_mm_stream *st, uint32_t fr, uint64_t fence, uint64_t done) {
    if (st->wrote_in_frame) {
        st->pending_slot = st->write_slot; st->pending_fence = fence; st->pending_frame = fr;
        st->pending_w = st->write_w; st->pending_h = st->write_h; st->pending_fmt = st->write_fmt;
        atomic_store_explicit(&st->pending_valid, 1, memory_order_release);
        st->wrote_in_frame = 0;
    }
    if (!atomic_load_explicit(&st->pending_valid, memory_order_relaxed)) return;
    if ((fr - st->pending_frame) < 1u || (st->pending_fence != 0 && done < st->pending_fence)) return;
    pthread_mutex_lock(&g_vka_mm.mu);
    int prev = st->ready_slot;
    st->ready_slot = st->pending_slot; st->ready_w = st->pending_w; st->ready_h = st->pending_h; st->ready_fmt = st->pending_fmt;
    st->seq++;
    st->write_slot = prev >= 0 ? prev : (st->pending_slot ^ 1);
    atomic_store_explicit(&st->pending_valid, 0, memory_order_release);
    pthread_mutex_unlock(&g_vka_mm.mu);
}

void vka_minimap_present(void) {
    g_vka_mm.frame_counter++;
    uint32_t fr = g_vka_mm.frame_counter;
    uint64_t fq = vka_fence_queued(), dn = vka_done();
    vka_mm_stream_present(&g_vka_mm.mm, fr, fq, dn);
    vka_mm_stream_present(&g_vka_mm.obj, fr, fq, dn);
    if (g_vka_mm.ui_wrote_frame) {
        g_vka_mm.ui_pending = 1; g_vka_mm.ui_pending_fence = fq; g_vka_mm.ui_pending_frame = fr;
        g_vka_mm.ui_wrote_frame = 0;
    }
    if (g_vka_mm.ui_pending && (fr - g_vka_mm.ui_pending_frame) >= 1u && (!g_vka_mm.ui_pending_fence || dn >= g_vka_mm.ui_pending_fence)) {
        pthread_mutex_lock(&g_vka_mm.mu);
        g_vka_mm.ui_pending = 0; g_vka_mm.ui_has_ready = 1; g_vka_mm.ui_seq++;
        atomic_store_explicit(&g_vka_mm.ui_want, 0, memory_order_release);
        pthread_mutex_unlock(&g_vka_mm.mu);
    }
    if (g_vka_mm.saw_mm_frame) g_vka_mm.mm_ttl = VKA_MM_TTL; else if (g_vka_mm.mm_ttl > 0u) g_vka_mm.mm_ttl--;
    if (g_vka_mm.saw_obj_frame) g_vka_mm.obj_ttl = VKA_MM_TTL; else if (g_vka_mm.obj_ttl > 0u) g_vka_mm.obj_ttl--;
    g_vka_mm.saw_mm_frame = 0; g_vka_mm.saw_obj_frame = 0;
}

static void vka_mm_copy_rgba(uint8_t *dst, const uint8_t *src, uint32_t w, uint32_t h, VkFormat fmt, int flip, int keep_a) {
    int bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *sr = src + (size_t)(flip ? (h - 1u - y) : y) * w * 4u;
        uint8_t *dr = dst + (size_t)y * w * 4u;
        for (uint32_t x = 0; x < w; x++) {
            size_t o = (size_t)x * 4u;
            dr[o + 0] = sr[o + (bgra ? 2 : 0)];
            dr[o + 1] = sr[o + 1];
            dr[o + 2] = sr[o + (bgra ? 0 : 2)];
            dr[o + 3] = keep_a ? sr[o + 3] : 255u;
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
    int rslot = g_vka_mm.mm.ready_slot;
    uint32_t w = g_vka_mm.mm.ready_w, h = g_vka_mm.mm.ready_h, seq = g_vka_mm.mm.seq;
    if (rslot >= 0 && rslot < VKA_MM_SLOTS && g_vka_mm.mm.slot[rslot].ptr && seq != client_seq) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && (size_t)max_bytes >= need) {
            vka_mm_copy_rgba(rgba_out, g_vka_mm.mm.slot[rslot].ptr, w, h, g_vka_mm.mm.ready_fmt, vka_origin_ll(), 0);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w; meta[1] = (int32_t)h; meta[2] = (int32_t)seq; meta[3] = cnt; meta[4] = cidx;
    pthread_mutex_unlock(&g_vka_mm.mu);
    return updated;
}

__attribute__((visibility("default")))
int nxhost_companion_objective(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    if (!meta) return 0;
    atomic_store_explicit(&g_vka_mm.want_active, 1, memory_order_release);
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vka_mm.mu);
    int active = (g_vka_mm.obj_ttl > 0u || g_vka_mm.saw_obj_frame
        || atomic_load_explicit(&g_vka_mm.obj.pending_valid, memory_order_relaxed)) ? 1 : 0;
    int rslot = g_vka_mm.obj.ready_slot;
    uint32_t w = g_vka_mm.obj.ready_w, h = g_vka_mm.obj.ready_h, seq = g_vka_mm.obj.seq;
    if (active && rslot >= 0 && rslot < VKA_MM_SLOTS && g_vka_mm.obj.slot[rslot].ptr && seq != client_seq) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && (size_t)max_bytes >= need) {
            vka_mm_copy_rgba(rgba_out, g_vka_mm.obj.slot[rslot].ptr, w, h, g_vka_mm.obj.ready_fmt, vka_origin_ll(), 0);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w; meta[1] = (int32_t)h; meta[2] = (int32_t)seq; meta[3] = active;
    pthread_mutex_unlock(&g_vka_mm.mu);
    return updated;
}

__attribute__((visibility("default")))
int nxhost_companion_ui_tex(uint8_t *rgba_out, int max_bytes, int32_t meta[4], int request) {
    if (!meta) return 0;
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vka_mm.mu);
    int st = atomic_load_explicit(&g_vka_mm.ui_want, memory_order_relaxed);
    if (request && st == 0 && (!g_vka_mm.ui_has_ready || client_seq == g_vka_mm.ui_seq)) {
        atomic_store_explicit(&g_vka_mm.ui_want, 1, memory_order_release);
        st = 1;
    }
    if (g_vka_mm.ui_has_ready && g_vka_mm.ui_slot.ptr && g_vka_mm.ui_seq != client_seq) {
        if (rgba_out && (size_t)max_bytes >= (size_t)VKA_UI_BUF_BYTES) {
            vka_mm_copy_rgba(rgba_out, g_vka_mm.ui_slot.ptr, VKA_UI_TEX_SIDE, VKA_UI_TEX_SIDE, VK_FORMAT_R8G8B8A8_UNORM, 0, 1);
            updated = 1;
        }
    }
    meta[0] = (int32_t)VKA_UI_TEX_SIDE; meta[1] = (int32_t)VKA_UI_TEX_SIDE;
    meta[2] = (int32_t)g_vka_mm.ui_seq; meta[3] = st;
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
    int v = on ? 1 : 0;
    atomic_store_explicit(&g_vka_mm.want_active, v, memory_order_release);
    if (!v) { g_vka_mm.mm_ttl = 0u; g_vka_mm.obj_ttl = 0u; }
}
