// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan HUD minimap, left-HUD & objective per-draw attachment redirection (2-slot staging rings)
// + one-shot UI texture harvest. Redirects HUD draws at recording time onto an offscreen color attachment
// sharing the pass depth/stencil views; never masks, clears, or overwrites Display 0.

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    VKA_MM_MIN_SIDE = 96u,
    VKA_MM_MAX_SIDE = 768u,
    VKA_MM_SAVE_MAX_W = 2560u,
    VKA_MM_SAVE_MAX_H = 1440u,
    VKA_OBJ_MAX_W = 1024u,
    VKA_OBJ_MAX_H = 288u,
    VKA_LHUD_MAX_W = 640u,
    VKA_LHUD_MAX_H = 1440u,
    VKA_UI_TEX_SIDE = 512u,
    VKA_MM_SLOTS = 2,
    VKA_MM_MAX_CAND = 8,
    VKA_MM_TTL = 2u
};
#define VKA_MM_BUF_BYTES ((VkDeviceSize)(VKA_MM_MAX_SIDE * VKA_MM_MAX_SIDE * 4u))
#define VKA_OBJ_BUF_BYTES ((VkDeviceSize)(VKA_OBJ_MAX_W * VKA_OBJ_MAX_H * 4u))
#define VKA_LHUD_BUF_BYTES ((VkDeviceSize)(VKA_LHUD_MAX_W * VKA_LHUD_MAX_H * 4u))
#define VKA_UI_BUF_BYTES ((VkDeviceSize)(VKA_UI_TEX_SIDE * VKA_UI_TEX_SIDE * 4u))

uint64_t vka_frame(void);
uint64_t vka_done(void);
uint64_t vka_fence_queued(void);
int vka_origin_ll(void);
int vka_cpu_read(uint64_t addr, void *out, size_t n);
int dm_ktgl_tag_get(uint64_t ubo, uint64_t *chunk, uint32_t *off, uint32_t *len);   // hle_nvn.c: the draw's display-list chunk (DM_KTGL_TAG)

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
    vka_mm_stream mm, obj, lhud;
    vka_mm_slot ui_slot;
    VkImage ui_img;
    VkDeviceMemory ui_mem;
    int ui_ready, ui_failed, ui_wrote_frame, ui_pending, ui_has_ready;
    uint32_t ui_pending_frame, ui_seq;
    uint64_t ui_pending_fence, ui_seen[8];
    int ui_seen_n, ui_seen_pos;
    _Atomic int ui_want;
    vka_ui_cand ui_cand;
    VkImage hud_img, hud_cleared_im, hud_state_im, pass_im, saved_hit_img;
    VkImageView hud_view, main_cview, depth_view, stencil_view;
    VkDeviceMemory hud_mem;
    VkFormat hud_fmt;
    VkImageLayout main_clayout, depth_layout, stencil_layout;
    VkRect2D pass_area;
    uint32_t hud_w, hud_h, pass_w, pass_h;
    int hud_ready, hud_failed, has_depth, has_stencil_att, cur_hud, pass_hud_cleared;
    vka_mm_cand cand[VKA_MM_MAX_CAND];
    _Atomic int cand_count, want_active;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, mm_ttl, obj_ttl, lhud_ttl;
    int saw_mm_frame, saw_obj_frame, saw_lhud_frame;
    int in_hud_pass, pass_opaque_fs, pass_modal, pass_dialog;
    int pass_mm_hit, pass_obj_hit, obj_bounds_init, pass_obj_icon, pass_obj_pill, pass_obj_text, cand_obj_text;
    int pass_lhud_any, pass_lhud_top, nfp, fr_modal, mm_open;
    float fp[8][4];   // this frame's foreign panels (vka_mm_note_foreign)
    uint64_t hud_cleared_frame, hud_state_frame, frame_got_mm, frame_got_obj, frame_got_lhud;
    vka_mm_rect hit_mm, hit_obj, prev_hit_obj, hit_lhud;
    uint32_t learn_mm_w, learn_mm_h;
    int32_t learn_mm_cx, learn_mm_cy, learn_mm_half;
    float obj_umin_x, obj_umin_y, obj_umax_x, obj_umax_y;
    float obj_icon_cx, obj_icon_core_w, obj_icon_cy, obj_pill_mn_x, obj_pill_cy;
    float cand_text_mn_x, cand_text_cy, learn_obj_icon_x, learn_obj_text_x, learn_obj_cy;
} g_vka_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .mm = { .slot_bytes = VKA_MM_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
    .obj = { .slot_bytes = VKA_OBJ_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
    .lhud = { .slot_bytes = VKA_LHUD_BUF_BYTES, .pending_slot = -1, .ready_slot = -1 },
};

static struct {
    int on, valid, hb; char cls; int32_t cnt; uint32_t stt, sref, blend, mask, seq, mbase; float mn[2], mx[2];
    uint64_t ua, usz;   // the uniform block the draw's sprite matrices were read from: one per layout (widget) instance
} g_vka_mm_tr;

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

static void vka_mm_destroy_hud(void) {
    if (g_vka_mm.hud_view) vk.vkDestroyImageView(V.dev, g_vka_mm.hud_view, NULL);
    if (g_vka_mm.hud_img) vk.vkDestroyImage(V.dev, g_vka_mm.hud_img, NULL);
    if (g_vka_mm.hud_mem) vk.vkFreeMemory(V.dev, g_vka_mm.hud_mem, NULL);
    g_vka_mm.hud_view = VK_NULL_HANDLE; g_vka_mm.hud_img = VK_NULL_HANDLE;
    g_vka_mm.hud_mem = VK_NULL_HANDLE; g_vka_mm.hud_ready = 0;
    g_vka_mm.hud_cleared_frame = 0u; g_vka_mm.hud_state_frame = 0u;
    g_vka_mm.hud_cleared_im = VK_NULL_HANDLE; g_vka_mm.hud_state_im = VK_NULL_HANDLE;
    g_vka_mm.prev_hit_obj = (vka_mm_rect){0, 0, 0u, 0u};
}

static int vka_mm_ensure_hud(VkCommandBuffer cb, uint32_t w, uint32_t h, VkFormat fmt) {
    if (w > VKA_MM_SAVE_MAX_W || h > VKA_MM_SAVE_MAX_H) return 0;
    if (g_vka_mm.hud_ready) {
        if (g_vka_mm.hud_fmt == fmt && g_vka_mm.hud_w == w && g_vka_mm.hud_h == h) return 1;
        vk.vkDeviceWaitIdle(V.dev);
        vka_mm_destroy_hud();
    }
    if (g_vka_mm.hud_failed || !V.dev || !cb || !w || !h) return 0;
    if (!vka_mm_create_img(cb, w, h, fmt,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &g_vka_mm.hud_img, &g_vka_mm.hud_mem)) {
        g_vka_mm.hud_failed = 1; return 0;
    }
    VkImageViewCreateInfo vi = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = g_vka_mm.hud_img, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = fmt, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u}
    };
    if (vk.vkCreateImageView(V.dev, &vi, NULL, &g_vka_mm.hud_view)) {
        vka_mm_destroy_hud(); g_vka_mm.hud_failed = 1; return 0;
    }
    g_vka_mm.hud_fmt = fmt; g_vka_mm.hud_w = w; g_vka_mm.hud_h = h;
    g_vka_mm.hud_ready = 1;
    return 1;
}

static vka_mm_rect vka_mm_lhud_box(uint32_t w, uint32_t h) {
    uint32_t lw = ((uint32_t)lrintf((float)w * 0.26f)) & ~3u;
    if (lw > VKA_LHUD_MAX_W) lw = VKA_LHUD_MAX_W;
    if (lw > (w & ~3u)) lw = w & ~3u;
    uint32_t lh = h < VKA_LHUD_MAX_H ? h : VKA_LHUD_MAX_H;
    return (vka_mm_rect){0, 0, lw, lh};
}

static int vka_minimap_in_hud_pass(void) {
    return g_vka_mm.in_hud_pass;
}

static void vka_minimap_pass_begin(
    VkCommandBuffer cb, vka_img *im, uint32_t nl, uint32_t w, uint32_t h,
    VkFormat att_fmt, VkRect2D area, VkImageView main_cview, VkImageLayout main_clayout,
    const VkRenderingAttachmentInfo *dp, const VkRenderingAttachmentInfo *sp, int load_c0, int classic) {
    g_vka_mm.in_hud_pass = 0; g_vka_mm.cur_hud = 0; g_vka_mm.pass_hud_cleared = 0;
    g_vka_mm.pass_opaque_fs = 0; g_vka_mm.pass_modal = 0; g_vka_mm.pass_dialog = 0;
    g_vka_mm.ui_cand.img = VK_NULL_HANDLE; g_vka_mm.ui_cand.score = -1;
    if (classic || !cb || !im || !im->img || !main_cview || !sp || !sp->imageView || !V.dev || vka_hung()) return;
    if (!atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire)) return;
    if (!load_c0 || nl != 1u || im->samples > 1u || w < 640u || h < 360u) return;
    if (w > VKA_MM_SAVE_MAX_W || h > VKA_MM_SAVE_MAX_H) return;
    if (area.offset.x != 0 || area.offset.y != 0 || area.extent.width != w || area.extent.height != h) return;
    if (!vka_mm_supported_fmt(att_fmt) || !vka_mm_ensure_hud(cb, w, h, att_fmt)) return;
    uint64_t fr = vka_frame();
    if (g_vka_mm.hud_cleared_frame == fr && g_vka_mm.hud_cleared_im == im->img) g_vka_mm.pass_hud_cleared = 1;
    if (g_vka_mm.hud_state_frame != fr || g_vka_mm.hud_state_im != im->img) {
        g_vka_mm.pass_mm_hit = 0; g_vka_mm.pass_obj_hit = 0; g_vka_mm.obj_bounds_init = 0;
        g_vka_mm.pass_obj_icon = 0; g_vka_mm.pass_obj_pill = 0; g_vka_mm.pass_obj_text = 0; g_vka_mm.cand_obj_text = 0;
        g_vka_mm.obj_icon_cx = 0.f; g_vka_mm.obj_icon_core_w = 0.f;
        g_vka_mm.pass_lhud_any = 0; g_vka_mm.pass_lhud_top = 0; g_vka_mm.nfp = 0; g_vka_mm.fr_modal = 0; g_vka_mm.mm_open = 0;
        g_vka_mm.hud_state_frame = fr; g_vka_mm.hud_state_im = im->img;
    }
    g_vka_mm.pass_area = area; g_vka_mm.main_cview = main_cview; g_vka_mm.main_clayout = main_clayout;
    g_vka_mm.has_depth = (dp && dp->imageView) ? 1 : 0;
    if (g_vka_mm.has_depth) { g_vka_mm.depth_view = dp->imageView; g_vka_mm.depth_layout = dp->imageLayout; }
    g_vka_mm.has_stencil_att = 1; g_vka_mm.stencil_view = sp->imageView; g_vka_mm.stencil_layout = sp->imageLayout;
    g_vka_mm.in_hud_pass = 1; g_vka_mm.pass_im = im->img; g_vka_mm.pass_w = w; g_vka_mm.pass_h = h;
    if (g_vka_mm.fr_modal) g_vka_mm.pass_modal = 1;
    { int tn = VKA_ENV("DM_VK_MM_TRACE", 0); g_vka_mm_tr.on = tn > 0 && (fr % (uint64_t)tn) == 0u; g_vka_mm_tr.valid = 0; }
}

static void vka_mm_clear_hud_img(VkCommandBuffer cb) {
    VkImageMemoryBarrier pb = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = g_vka_mm.hud_img, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u}
    };
    VkClearColorValue clr = {{0.f, 0.f, 0.f, 0.f}};
    VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u, &pb);
    vk.vkCmdClearColorImage(cb, g_vka_mm.hud_img, VK_IMAGE_LAYOUT_GENERAL, &clr, 1u, &rr);
    g_vka_mm.pass_hud_cleared = 1;
    g_vka_mm.hud_cleared_frame = vka_frame();
    g_vka_mm.hud_cleared_im = g_vka_mm.pass_im;
}

static int vka_mm_switch_target(VkCommandBuffer cb, int want_hud) {
    if (!g_vka_mm.in_hud_pass || !g_vka_mm.hud_ready || g_vka_mm.cur_hud == want_hud) return 0;
    vka_oq_pass_end();
    vk.vkCmdEndRendering(cb);
    if (want_hud && !g_vka_mm.pass_hud_cleared) vka_mm_clear_hud_img(cb);
    VkMemoryBarrier mb = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };
    vk.vkCmdPipelineBarrier(cb,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0u, 1u, &mb, 0u, NULL, 0u, NULL);
    VkRenderingAttachmentInfo ca = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = want_hud ? g_vka_mm.hud_view : g_vka_mm.main_cview,
        .imageLayout = want_hud ? VK_IMAGE_LAYOUT_GENERAL : g_vka_mm.main_clayout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingAttachmentInfo da = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = g_vka_mm.depth_view, .imageLayout = g_vka_mm.depth_layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingAttachmentInfo sa = {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = g_vka_mm.stencil_view, .imageLayout = g_vka_mm.stencil_layout,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };
    VkRenderingInfo ri = {
        VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = g_vka_mm.pass_area, .layerCount = 1u,
        .colorAttachmentCount = 1u, .pColorAttachments = &ca,
        .pDepthAttachment = g_vka_mm.has_depth ? &da : NULL,
        .pStencilAttachment = g_vka_mm.has_stencil_att ? &sa : NULL
    };
    vk.vkCmdBeginRendering(cb, &ri);
    g_vka_mm.cur_hud = want_hud;
    return 1;
}

static int vka_minimap_bind_main(VkCommandBuffer cb) {
    return vka_mm_switch_target(cb, 0);
}

static float vka_mm_f16(uint16_t h) {
    uint32_t sg = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31u, m = h & 1023u;
    if (e == 0u) return m ? ((sg ? -1.f : 1.f) * (float)m / 16777216.f) : (sg ? -0.f : 0.f);
    uint32_t u = (e == 31u) ? (sg | 0x7f800000u | (m << 13)) : (sg | ((e + 112u) << 23) | (m << 13));
    float f = 0.f; memcpy(&f, &u, 4); return f;
}

static int vka_mm_fmt_info(uint32_t fmt, int *is_half) {
    *is_half = 0;
    if (fmt == NVN_FORMAT_R16F) { *is_half = 1; return 1; }
    if (fmt == NVN_FORMAT_RG16F) { *is_half = 1; return 2; }
    if (fmt == NVN_FORMAT_RGB16F) { *is_half = 1; return 3; }
    if (fmt == NVN_FORMAT_RGBA16F) { *is_half = 1; return 4; }
    if (fmt == NVN_FORMAT_R32F) return 1;
    if (fmt == NVN_FORMAT_RG32F) return 2;
    if (fmt == NVN_FORMAT_RGB32F) return 3;
    if (fmt == NVN_FORMAT_RGBA32F) return 4;
    return 0;
}

static int64_t vka_mm_vertex_idx(const p_draw *dw, const p_draw_idx *di, int32_t v) {
    if (di) {
        uint32_t ix = 0u;
        size_t isz = di->itype == 0 ? 1u : (di->itype == 1 ? 2u : 4u);
        if (!di->ibuf || !vka_cpu_read(di->ibuf + (uint64_t)v * isz, &ix, isz)) return -1;
        return (int64_t)di->base_vertex + (int64_t)ix;
    }
    return dw ? (int64_t)dw->first + v : -1;
}

static int vka_mm_read_fvec(
    const vka_dstate *d, int loc, int64_t vi, int ncomp, int half, float out[4]) {
    out[0] = 0.f; out[1] = 0.f; out[2] = 0.f; out[3] = 1.f;
    if (loc < 0 || loc >= d->nattr || loc >= 16 || ncomp < 1 || ncomp > 4) return 0;
    va_st a = d->attr[loc];
    if (a.stream >= 16u) return 0;
    uint64_t base = d->vbuf[a.stream].address;
    if (!base) return 0;
    int64_t eff = d->stream[a.stream].divisor ? (int64_t)d->b.base_instance : vi;
    if (eff < 0) return 0;
    uint32_t sd = d->stream[a.stream].stride;
    size_t elem = (size_t)ncomp * (half ? 2u : 4u);
    uint64_t vo = (uint64_t)eff * sd + a.offset;
    uint64_t bsz = d->vbuf[a.stream].size;
    if (bsz && (vo > bsz || bsz - vo < elem)) return 0;
    uint8_t raw[16] = {0};
    if (!vka_cpu_read(base + vo, raw, elem)) return 0;
    if (half) {
        uint16_t hv[4] = {0};
        memcpy(hv, raw, elem);
        for (int k = 0; k < ncomp; k++) out[k] = vka_mm_f16(hv[k]);
    } else {
        memcpy(out, raw, elem);
    }
    for (int k = 0; k < ncomp; k++) if (!isfinite(out[k])) return 0;
    return 1;
}

static int vka_mm_find_sprite_locs(
    const vka_dstate *d, uint32_t vin_mask, int *pos_loc, int *pos_n,
    int *pos_half, int *idx_loc, int *idx_half) {
    *pos_loc = -1; *pos_n = 0; *pos_half = 0; *idx_loc = -1; *idx_half = 0;
    int n = d->nattr < 16 ? d->nattr : 16;
    if (n <= 0) return 0;
    uint32_t mask = vin_mask ? vin_mask : (n == 16 ? 0xffffu : ((1u << n) - 1u));
    for (int loc = 0; loc < n; loc++) {
        if (!(mask & (1u << loc))) continue;
        int h = 0, nc = vka_mm_fmt_info(d->attr[loc].fmt, &h);
        if (nc >= 2) { *pos_loc = loc; *pos_n = nc; *pos_half = h; }
    }
    if (*pos_loc < 0) return 0;
    for (int loc = 0; loc < *pos_loc; loc++) {
        if (!(mask & (1u << loc))) continue;
        int h = 0, nc = vka_mm_fmt_info(d->attr[loc].fmt, &h);
        if (nc == 1) { *idx_loc = loc; *idx_half = h; break; }
    }
    return 1;
}

static void vka_mm_viewport(const vka_dstate *d, float vp[4], float *gvw, float *gvh) {
    float vx = d->has_vp ? d->vp[0][0] : 0.f;
    float vy = d->has_vp ? d->vp[0][1] : 0.f;
    float vw = d->has_vp ? d->vp[0][2] : (float)g_vka_mm.pass_w;
    float vh = d->has_vp ? d->vp[0][3] : (float)g_vka_mm.pass_h;
    *gvw = fabsf(vw); *gvh = fabsf(vh);
    if (vka_origin_ll()) { vy += vh; vh = -vh; }
    uint32_t sw = 0u;
    memcpy(&sw, d->vp_sw[0], 4u);
    if (sw == 0x100u) { vy += vh; vh = -vh; }
    if (!vw) vw = 1.f;
    if (!vh) vh = 1.f;
    vp[0] = vx; vp[1] = vy + vh; vp[2] = vw; vp[3] = -vh;
}

static int vka_mm_is_ortho_mat(const float p[16]) {
    for (int i = 0; i < 16; i++) if (!isfinite(p[i])) return 0;
    if (fabsf(p[15] - 1.f) >= 1e-2f) return 0;
    if (fabsf(p[1]) >= 1e-3f || fabsf(p[2]) >= 1e-3f || fabsf(p[3]) >= 1e-3f) return 0;
    if (fabsf(p[4]) >= 1e-3f || fabsf(p[6]) >= 1e-3f || fabsf(p[7]) >= 1e-3f) return 0;
    if (fabsf(p[8]) >= 1e-3f || fabsf(p[9]) >= 1e-3f || fabsf(p[11]) >= 1e-3f) return 0;
    float ax = fabsf(p[0]), ay = fabsf(p[5]);
    if (ax < (2.f / 8192.f) || ax > (2.f / 160.f)) return 0;
    if (ay < (2.f / 8192.f) || ay > (2.f / 160.f)) return 0;
    return fabsf(p[12]) <= 4.f && fabsf(p[13]) <= 4.f;
}

static int vka_mm_is_proj_mat(const float p[16]) {
    for (int i = 0; i < 16; i++) if (!isfinite(p[i])) return 0;
    float nx = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    float ny = p[4] * p[4] + p[5] * p[5] + p[6] * p[6];
    if (nx < 1e-10f || nx > 16.f || ny < 1e-10f || ny > 16.f) return 0;
    float dot = p[0] * p[4] + p[1] * p[5] + p[2] * p[6];
    if (dot * dot >= 0.5f * nx * ny) return 0;
    float wnorm = fabsf(p[3]) + fabsf(p[7]) + fabsf(p[11]) + fabsf(p[15]);
    return wnorm >= 1e-4f;
}

static int vka_mm_proj_point(
    const float pos[4], const float m[12], const float p[16], const float vp[4], float xy[2]) {
    float wx = pos[0] * m[0] + pos[1] * m[1] + pos[2] * m[2] + pos[3] * m[3];
    float wy = pos[0] * m[4] + pos[1] * m[5] + pos[2] * m[6] + pos[3] * m[7];
    float wz = pos[0] * m[8] + pos[1] * m[9] + pos[2] * m[10] + pos[3] * m[11];
    float cx = p[0] * wx + p[4] * wy + p[8] * wz + p[12] * pos[3];
    float cy = p[1] * wx + p[5] * wy + p[9] * wz + p[13] * pos[3];
    float cw = p[3] * wx + p[7] * wy + p[11] * wz + p[15] * pos[3];
    if (!isfinite(cw) || fabsf(cw) < 1e-5f) return 0;
    xy[0] = vp[0] + ((cx / cw) * 0.5f + 0.5f) * vp[2];
    xy[1] = vp[1] + ((cy / cw) * 0.5f + 0.5f) * vp[3];
    return isfinite(xy[0]) && isfinite(xy[1]);
}

static int vka_mm_scan_ubo(
    const vka_dstate *d, int ortho_only, float p_out[16],
    uint64_t *ua_out, uint64_t *usz_out, uint32_t *m_base_out) {
    static const uint32_t k_poff[2] = { 32u, 0u };
    static const uint32_t k_mbase[2] = { 96u, 64u };
    for (int u = 0; u < 4; u++) {
        uint64_t ua = d->b.ubo[0][u].addr, usz = d->b.ubo[0][u].size;
        if (!ua || (usz && usz < 112u)) continue;
        for (int k = 0; k < 2; k++) {
            if (usz && k_mbase[k] + 48u > usz) continue;
            float p[16] = {0};
            if (!vka_cpu_read(ua + k_poff[k], p, sizeof p)) continue;
            int ok = ortho_only ? vka_mm_is_ortho_mat(p) : vka_mm_is_proj_mat(p);
            if (!ok) continue;
            memcpy(p_out, p, sizeof p);
            *ua_out = ua; *usz_out = usz; *m_base_out = k_mbase[k];
            return 1;
        }
    }
    return 0;
}

static int vka_mm_find_proj(
    const vka_dstate *d, int allow_proj, float p_out[16], float vp_out[4],
    uint64_t *ua_out, uint64_t *usz_out, uint32_t *m_base_out) {
    float gvw = 0.f, gvh = 0.f;
    vka_mm_viewport(d, vp_out, &gvw, &gvh);
    if (gvw < 64.f || gvh < 64.f) return 0;
    if (vka_mm_scan_ubo(d, 1, p_out, ua_out, usz_out, m_base_out)) return 1;
    if (allow_proj && vka_mm_scan_ubo(d, 0, p_out, ua_out, usz_out, m_base_out)) return 1;
    return 0;
}

static int vka_mm_ubo_xy_bounds(
    const vka_dstate *d, uint32_t vin_mask, const p_draw *dw, const p_draw_idx *di,
    int32_t cnt, float *mn, float *mx) {
    int pos_loc = -1, pos_n = 0, pos_half = 0, idx_loc = -1, idx_half = 0, h0 = 0;
    if (!vka_mm_find_sprite_locs(d, vin_mask, &pos_loc, &pos_n, &pos_half, &idx_loc, &idx_half)) return 0;
    int nc0 = vka_mm_fmt_info(d->attr[0].fmt, &h0);
    (void)h0;
    float p[16] = {0}, vp[4] = {0}, m[12] = {0};
    uint64_t ua = 0u, usz = 0u;
    uint32_t m_base = 0u;
    if (!vka_mm_find_proj(d, (idx_loc >= 0 || nc0 < 2), p, vp, &ua, &usz, &m_base)) return 0;
    g_vka_mm_tr.ua = ua; g_vka_mm_tr.usz = usz; g_vka_mm_tr.mbase = m_base;
    int32_t lim = cnt < 256 ? cnt : 256, last_idx = -1;
    for (int32_t i = 0; i < lim; i++) {
        int32_t v = cnt <= 256 ? i : (int32_t)(((int64_t)i * cnt) / lim);
        int64_t vi = vka_mm_vertex_idx(dw, di, v);
        float pos[4] = {0}, iv[4] = {0}, xy[2] = {0};
        if (vi < 0 || !vka_mm_read_fvec(d, pos_loc, vi, pos_n, pos_half, pos)) return 0;
        int32_t idx = 0;
        if (idx_loc >= 0) {
            if (!vka_mm_read_fvec(d, idx_loc, vi, 1, idx_half, iv)) return 0;
            float tf = truncf(iv[0]);
            if (tf < 0.f || tf >= 512.f) return 0;
            idx = (int32_t)tf;
        }
        if (idx != last_idx) {
            uint64_t mo = (uint64_t)m_base + (uint64_t)idx * 48u;
            if ((usz && mo + 48u > usz) || !vka_cpu_read(ua + mo, m, sizeof m)) return 0;
            for (int k = 0; k < 12; k++) if (!isfinite(m[k])) return 0;
            last_idx = idx;
        }
        if (!vka_mm_proj_point(pos, m, p, vp, xy)) return 0;
        if (i == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else {
            if (xy[0] < mn[0]) { mn[0] = xy[0]; } if (xy[0] > mx[0]) { mx[0] = xy[0]; }
            if (xy[1] < mn[1]) { mn[1] = xy[1]; } if (xy[1] > mx[1]) { mx[1] = xy[1]; }
        }
    }
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    if (!isfinite(mn[0]) || !isfinite(mx[0]) || !isfinite(mn[1]) || !isfinite(mx[1])) return 0;
    if (mn[0] < -w || mx[0] > 2.f * w || mn[1] < -h || mx[1] > 2.f * h) return 0;
    return mx[0] >= mn[0] && mx[1] >= mn[1];
}

static int vka_mm_attr_xy_bounds(
    const vka_dstate *d, uint32_t vin_mask, const p_draw *dw, const p_draw_idx *di,
    int32_t cnt, float *mn, float *mx) {
    if (cnt <= 0) return 0;
    if (vka_mm_ubo_xy_bounds(d, vin_mask, dw, di, cnt, mn, mx)) return 1;
    int half = 0, nc = vka_mm_fmt_info(d->attr[0].fmt, &half);
    if (nc < 2 || d->nattr < 1) return 0;
    int32_t lim = cnt < 256 ? cnt : 256;
    for (int32_t i = 0; i < lim; i++) {
        int32_t v = cnt <= 256 ? i : (int32_t)(((int64_t)i * cnt) / lim);
        int64_t vi = vka_mm_vertex_idx(dw, di, v);
        float xy[4] = {0};
        if (vi < 0 || !vka_mm_read_fvec(d, 0, vi, 2, half, xy)) return 0;
        if (i == 0) { mn[0] = mx[0] = xy[0]; mn[1] = mx[1] = xy[1]; }
        else {
            if (xy[0] < mn[0]) { mn[0] = xy[0]; } if (xy[0] > mx[0]) { mx[0] = xy[0]; }
            if (xy[1] < mn[1]) { mn[1] = xy[1]; } if (xy[1] > mx[1]) { mx[1] = xy[1]; }
        }
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
    if (x0 < 0) { x0 = 0; } if (y0 < 0) { y0 = 0; }
    if (x0 + (int32_t)side > (int32_t)g_vka_mm.pass_w) x0 = (int32_t)g_vka_mm.pass_w - (int32_t)side;
    if (y0 + (int32_t)side > (int32_t)g_vka_mm.pass_h) y0 = (int32_t)g_vka_mm.pass_h - (int32_t)side;
    g_vka_mm.hit_mm = (vka_mm_rect){x0, y0, side, side};
    g_vka_mm.pass_mm_hit = 1; g_vka_mm.mm_open = 1;
    g_vka_mm.learn_mm_w = g_vka_mm.pass_w; g_vka_mm.learn_mm_h = g_vka_mm.pass_h;
    g_vka_mm.learn_mm_cx = (int32_t)lrintf(cx); g_vka_mm.learn_mm_cy = (int32_t)lrintf(cy);
    g_vka_mm.learn_mm_half = (int32_t)(side / 2u);
}

static void vka_mm_rebuild_obj_rect(void) {
    float h = (float)g_vka_mm.pass_h;
    float cy = g_vka_mm.pass_obj_text ? g_vka_mm.cand_text_cy
        : (g_vka_mm.pass_obj_pill ? g_vka_mm.obj_pill_cy : g_vka_mm.obj_icon_cy);
    int32_t oh = ((int32_t)lrintf(h * 0.19f)) & ~1;
    if (oh > (int32_t)VKA_OBJ_MAX_H) { oh = (int32_t)(VKA_OBJ_MAX_H & ~1u); }
    if (oh > (int32_t)g_vka_mm.pass_h) { oh = (int32_t)(g_vka_mm.pass_h & ~1u); }
    int32_t y0 = (int32_t)lrintf(cy) - oh / 2, y1 = y0 + oh;
    if (y0 < 0) { y0 = 0; y1 = oh; }
    if (y1 > (int32_t)g_vka_mm.pass_h) {
        y1 = (int32_t)g_vka_mm.pass_h;
        y0 = y1 > oh ? y1 - oh : 0;
    }
    float xmn = 1e9f, xmx = -1e9f;
    if (g_vka_mm.obj_bounds_init) {
        xmn = g_vka_mm.obj_umin_x - h * 0.018f;
        xmx = g_vka_mm.obj_umax_x + h * 0.018f;
    }
    int has_ic = g_vka_mm.pass_obj_icon;
    float icx = g_vka_mm.obj_icon_cx;
    if (!has_ic && g_vka_mm.obj_ttl > 0u && fabsf(cy - g_vka_mm.learn_obj_cy) <= h * 0.055f) {
        has_ic = 1; icx = g_vka_mm.learn_obj_icon_x;
    }
    if (has_ic) {
        float l = icx - h * 0.095f, r = icx + h * 0.095f;
        if (l < xmn) { xmn = l; } if (r > xmx) { xmx = r; }
    } else if (g_vka_mm.obj_bounds_init && !g_vka_mm.pass_obj_pill) {
        xmn = g_vka_mm.obj_umin_x - h * 0.065f;
    }
    if (xmn > xmx) return;
    int32_t x0 = (int32_t)floorf(xmn), x1 = (int32_t)ceilf(xmx);
    if (x0 < 0) { x0 = 0; } if (x1 > (int32_t)g_vka_mm.pass_w) { x1 = (int32_t)g_vka_mm.pass_w; }
    if (x1 - x0 > (int32_t)VKA_OBJ_MAX_W) { x1 = x0 + (int32_t)VKA_OBJ_MAX_W; }
    int32_t ow = (x1 - x0) & ~3;
    if (ow < 64 || y1 <= y0) return;
    vka_mm_rect nr = {x0, y0, (uint32_t)ow, (uint32_t)(y1 - y0)};
    vka_mm_rect pr = g_vka_mm.prev_hit_obj;
    if (g_vka_mm.obj_ttl > 0u && pr.w >= 64u && abs(x0 - pr.x) <= 6 && abs(y0 - pr.y) <= 6
            && abs(ow - (int32_t)pr.w) <= 8 && abs((y1 - y0) - (int32_t)pr.h) <= 6) {
        nr = pr;
    }
    g_vka_mm.hit_obj = nr;
}

static void vka_mm_add_bar_bounds(const float *mn, const float *mx) {
    if (!g_vka_mm.obj_bounds_init) {
        g_vka_mm.obj_umin_x = mn[0]; g_vka_mm.obj_umin_y = mn[1];
        g_vka_mm.obj_umax_x = mx[0]; g_vka_mm.obj_umax_y = mx[1];
        g_vka_mm.obj_bounds_init = 1;
    } else {
        if (mn[0] < g_vka_mm.obj_umin_x) { g_vka_mm.obj_umin_x = mn[0]; }
        if (mn[1] < g_vka_mm.obj_umin_y) { g_vka_mm.obj_umin_y = mn[1]; }
        if (mx[0] > g_vka_mm.obj_umax_x) { g_vka_mm.obj_umax_x = mx[0]; }
        if (mx[1] > g_vka_mm.obj_umax_y) { g_vka_mm.obj_umax_y = mx[1]; }
    }
    vka_mm_rebuild_obj_rect();
}

static int vka_mm_is_mm_draw(const float *mn, const float *mx, uint32_t stt, int32_t cnt) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    if (g_vka_mm.pass_mm_hit) {
        if (stt != 0u && mn[0] >= w * 0.45f) return 1;
        vka_mm_rect r = g_vka_mm.hit_mm;
        // The map's own layers (terrain, fog): unstencilled quads that cover the whole minimap and share its centre.
        float rx1 = (float)(r.x + (int32_t)r.w), ry1 = (float)(r.y + (int32_t)r.h);
        if (stt == 0u && cnt <= 6 && mn[0] <= (float)r.x && mx[0] >= rx1 && mn[1] <= (float)r.y && mx[1] >= ry1
                && fabsf((mn[0] + mx[0]) * 0.5f - ((float)r.x + rx1) * 0.5f) <= 4.f
                && fabsf((mn[1] + mx[1]) * 0.5f - ((float)r.y + ry1) * 0.5f) <= 4.f) return 1;
        // The minimap's draws are contiguous. An unstencilled sprite inside its box belongs to it only while that group is
        // open: the first draw outside the box closes it, so a world-anchored bubble that later drifts over the box stays put.
        int inside = mn[0] >= (float)(r.x - 28) && mx[0] <= (float)(r.x + (int32_t)r.w + 28)
            && mn[1] >= (float)(r.y - 28) && mx[1] <= (float)(r.y + (int32_t)r.h + 28);
        if (!inside) g_vka_mm.mm_open = 0;
        return inside && g_vka_mm.mm_open;
    }
    if (g_vka_mm.mm_ttl > 0u && !g_vka_mm.pass_dialog && stt == 0u && cnt >= 4 && cnt <= 24) {
        float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
        float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
        if (bw >= h * 0.14f && bw <= h * 0.46f && fabsf(bw - bh) <= fmaxf(12.f, bw * 0.10f)
                && fabsf(cx - (float)g_vka_mm.learn_mm_cx) <= 28.f
                && fabsf(cy - (float)g_vka_mm.learn_mm_cy) <= 28.f) {
            vka_mm_try_minimap(mn, mx);
            return g_vka_mm.pass_mm_hit;
        }
    }
    return 0;
}

// A widget's draws are contiguous: backing panel first, then its children. A panel that starts in the left strip but reaches
// well past it (a speech pop-up, a menu row) is not left-HUD, and neither are the portrait and text drawn on it afterwards.
static void vka_mm_note_foreign(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    if (cnt < 4 || g_vka_mm.nfp >= 8) return;
    if (mn[0] > w * 0.26f || mx[0] < w * 0.30f || bw < w * 0.20f || bw > w * 0.90f || bh < h * 0.04f || bh > h * 0.50f) return;
    float *r = g_vka_mm.fp[g_vka_mm.nfp++];
    r[0] = mn[0]; r[1] = mn[1]; r[2] = mx[0]; r[3] = mx[1];
}

static int vka_mm_on_foreign(const float *mn, const float *mx) {
    float pad = (float)g_vka_mm.pass_h * 0.05f;
    float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    for (int i = 0; i < g_vka_mm.nfp; i++) {
        const float *r = g_vka_mm.fp[i];
        if (cx >= r[0] && cx <= r[2] && cy >= r[1] - pad && cy <= r[3] + pad) return 1;
    }
    return 0;
}

static int vka_mm_is_lhud_draw(const float *mn, const float *mx, int32_t cnt) {
    if (cnt < 4 || (!g_vka_mm.pass_mm_hit && g_vka_mm.pass_dialog)) return 0;
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1];
    int ll = vka_origin_ll();
    int top_band = !ll ? (mn[1] >= -8.f && mx[1] <= h * 0.18f) : (mn[1] >= h * 0.82f && mx[1] <= h + 8.f);
    if (top_band && mn[0] >= -8.f && mn[0] <= w * 0.095f && mx[0] <= w * 0.22f
            && bw >= 10.f && bh >= 5.f && bh <= h * 0.09f) {
        g_vka_mm.pass_lhud_top = 1;
    }
    if (!(g_vka_mm.pass_lhud_top || g_vka_mm.pass_mm_hit || g_vka_mm.lhud_ttl > 0u)) return 0;
    if (mn[0] < -16.f || mn[0] > w * 0.20f || mx[0] > w * 0.26f || mn[1] < -16.f || mx[1] > h + 16.f) return 0;
    if (bw < 6.f || bw > w * 0.26f || bh < 6.f || bh > h * 0.42f) return 0;
    if (mn[0] > w * 0.18f && bh > bw * 6.f) return 0;
    if (vka_mm_on_foreign(mn, mx)) return 0;
    g_vka_mm.pass_lhud_any = 1;
    g_vka_mm.hit_lhud = vka_mm_lhud_box(g_vka_mm.pass_w, g_vka_mm.pass_h);
    return 1;
}

static int vka_mm_obj_pill(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    if (cnt > 36 || mn[0] < w * 0.48f || mn[0] > w * 0.88f || mx[0] < w * 0.72f) return 0;
    if (bh < h * 0.018f || bh > h * 0.14f || bw < bh * 2.0f) return 0;
    int known = (g_vka_mm.pass_obj_icon && fabsf(cy - g_vka_mm.obj_icon_cy) <= h * 0.06f)
        || (g_vka_mm.pass_obj_text && fabsf(cy - g_vka_mm.cand_text_cy) <= h * 0.06f && mn[0] <= g_vka_mm.cand_text_mn_x)
        || (g_vka_mm.obj_ttl > 0u && fabsf(cy - g_vka_mm.learn_obj_cy) <= h * 0.06f && mn[0] >= g_vka_mm.learn_obj_icon_x - 64.f);
    if (!known && !(mx[0] >= w * 0.88f && bw >= w * 0.16f)) return 0;
    g_vka_mm.pass_obj_pill = 1; g_vka_mm.obj_pill_mn_x = mn[0]; g_vka_mm.obj_pill_cy = cy;
    g_vka_mm.pass_obj_hit = 1;
    vka_mm_add_bar_bounds(mn, mx);
    return 1;
}

static int vka_mm_obj_text(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    if (cnt < 12 || cnt > 1200 || mn[0] < w * 0.52f || mx[0] > w + 24.f) return 0;
    if (bw < h * 0.045f || bw > w * 0.44f || bh < h * 0.014f || bh > h * 0.14f || bw < bh * 1.3f) return 0;
    float icx = g_vka_mm.obj_icon_cx;
    int near_pill = g_vka_mm.pass_obj_pill && fabsf(cy - g_vka_mm.obj_pill_cy) <= h * 0.055f
        && mn[0] >= g_vka_mm.obj_pill_mn_x - 24.f;
    int near_icon = g_vka_mm.pass_obj_icon && fabsf(cy - g_vka_mm.obj_icon_cy) <= h * 0.055f
        && mn[0] >= icx - 12.f && mn[0] <= icx + h * 0.10f;
    int near_learn = g_vka_mm.obj_ttl > 0u && fabsf(cy - g_vka_mm.learn_obj_cy) <= h * 0.055f
        && fabsf(mn[0] - g_vka_mm.learn_obj_text_x) <= 64.f;
    if (!g_vka_mm.cand_obj_text || near_pill || near_icon || near_learn) {
        g_vka_mm.cand_obj_text = 1; g_vka_mm.cand_text_mn_x = mn[0]; g_vka_mm.cand_text_cy = cy;
    }
    if (!near_pill && !near_icon && !near_learn) return 0;
    g_vka_mm.pass_obj_text = 1; g_vka_mm.pass_obj_hit = 1;
    vka_mm_add_bar_bounds(mn, mx);
    return 1;
}

static int vka_mm_obj_icon(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cy = (mn[1] + mx[1]) * 0.5f;
    float icx = (mn[0] + mx[0]) * 0.5f;
    if (cnt > 96 || mx[0] > w * 0.92f || bw < h * 0.004f || bw > h * 0.18f || bh < h * 0.004f || bh > h * 0.18f) return 0;
    if (fabsf(bw - bh) > fmaxf(8.f, bw * 0.35f)) return 0;
    int near_text = (g_vka_mm.pass_obj_text || g_vka_mm.cand_obj_text)
        && fabsf(cy - g_vka_mm.cand_text_cy) <= h * 0.06f
        && icx <= g_vka_mm.cand_text_mn_x + 36.f && icx >= g_vka_mm.cand_text_mn_x - h * 0.12f;
    int near_pill = g_vka_mm.pass_obj_pill && fabsf(cy - g_vka_mm.obj_pill_cy) <= h * 0.06f
        && icx >= g_vka_mm.obj_pill_mn_x - h * 0.06f && icx <= g_vka_mm.obj_pill_mn_x + h * 0.14f;
    int near_learn = g_vka_mm.obj_ttl > 0u && fabsf(cy - g_vka_mm.learn_obj_cy) <= h * 0.06f
        && fabsf(icx - g_vka_mm.learn_obj_icon_x) <= h * 0.08f;
    int near_icon = g_vka_mm.pass_obj_icon && fabsf(cy - g_vka_mm.obj_icon_cy) <= h * 0.06f
        && fabsf(icx - g_vka_mm.obj_icon_cx) <= h * 0.05f;
    // Once the bar's icon column is known, an icon-shaped sprite elsewhere on its row is not the bar's: the game draws the
    // same marker over the 3D world, and it crosses that row as the camera turns.
    if (g_vka_mm.obj_ttl > 0u && !near_learn && !near_text && !near_pill) return 0;
    if (bw >= h * 0.016f || near_text || near_pill) {
        if (!g_vka_mm.pass_obj_icon || bw < g_vka_mm.obj_icon_core_w) {
            g_vka_mm.obj_icon_core_w = bw; g_vka_mm.obj_icon_cx = icx; g_vka_mm.obj_icon_cy = cy;
        }
        g_vka_mm.pass_obj_icon = 1;
    }
    if (!near_text && !near_pill && !near_learn
            && !(near_icon && (g_vka_mm.pass_obj_pill || g_vka_mm.cand_obj_text || g_vka_mm.obj_ttl > 0u))) return 0;
    g_vka_mm.pass_obj_hit = 1;
    vka_mm_rebuild_obj_rect();
    return 1;
}

static int vka_mm_is_obj_draw(const float *mn, const float *mx, int32_t cnt) {
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float cy = (mn[1] + mx[1]) * 0.5f;
    if (mn[0] < w * 0.45f || mx[0] > w + 48.f || cy < h * 0.14f || cy > h * 0.68f) return 0;
    if (vka_mm_obj_pill(mn, mx, cnt)) return 1;
    if (vka_mm_obj_text(mn, mx, cnt)) return 1;
    return vka_mm_obj_icon(mn, mx, cnt);
}

// DM_VK_MM_TRACE=<n> (diagnostic, live): every n-th frame, one log line per HUD-pass draw with its identity
// (shader keys, first sampled texture, stencil state), its resolved bounds and the capture decision.
// cls: x pre-filter, b no bounds, F opaque fullscreen, S stencil mask, M modal/masked, m minimap, l left HUD, o objective, - main.
static void vka_minimap_trace_draw(uint64_t vsk, uint64_t fsk, const vka_img *t0, int ntex) {
    (void)vsk; (void)fsk; (void)t0; (void)ntex;   // read only by the log line
    uint64_t kch = 0u; uint32_t koff = 0u, klen = 0u;
    (void)dm_ktgl_tag_get(g_vka_mm_tr.ua, &kch, &koff, &klen);
    if (!g_vka_mm_tr.on || !g_vka_mm_tr.valid) return;
    g_vka_mm_tr.valid = 0;
    VLOG("mm trace f%llu d%u: cls %c vs %016llx fs %016llx cnt %d st %u ref %u bl %u m %x b %d %.0f,%.0f %.0f,%.0f tex %ux%u l%u s%llu n%d ubo %llx+%llu m%u kt %llx+%u/%u",
         (unsigned long long)vka_frame(), g_vka_mm_tr.seq++, g_vka_mm_tr.cls, (unsigned long long)vsk, (unsigned long long)fsk,
         g_vka_mm_tr.cnt, g_vka_mm_tr.stt, g_vka_mm_tr.sref, g_vka_mm_tr.blend, g_vka_mm_tr.mask, g_vka_mm_tr.hb,
         g_vka_mm_tr.mn[0], g_vka_mm_tr.mn[1], g_vka_mm_tr.mx[0], g_vka_mm_tr.mx[1],
         t0 ? t0->w : 0u, t0 ? t0->h : 0u, t0 ? t0->levels : 0u, (unsigned long long)(t0 ? t0->serial : 0u), ntex,
         (unsigned long long)g_vka_mm_tr.ua, (unsigned long long)g_vka_mm_tr.usz, g_vka_mm_tr.mbase, (unsigned long long)kch, koff, klen);
}

static int vka_minimap_select_target(
    VkCommandBuffer cb, const vka_dstate *d, uint32_t vin_mask, const p_draw *dw, const p_draw_idx *di,
    int mesh, uint64_t ind, uint32_t stt, uint32_t dt, uint32_t c0_mask, uint32_t blend_en) {
    if (!g_vka_mm.in_hud_pass) return 0;
    int32_t cnt = dw ? dw->count : (di ? di->count : 0);
    g_vka_mm_tr.valid = 1; g_vka_mm_tr.hb = 0; g_vka_mm_tr.cls = 'x'; g_vka_mm_tr.ua = 0u; g_vka_mm_tr.usz = 0u; g_vka_mm_tr.mbase = 0u; g_vka_mm_tr.cnt = cnt; g_vka_mm_tr.stt = stt;
    g_vka_mm_tr.sref = d->st_ref[0]; g_vka_mm_tr.blend = blend_en; g_vka_mm_tr.mask = c0_mask;
    if (mesh || ind || dt != 0u || d->nattr < 1) return vka_mm_switch_target(cb, 0);
    float mn[2] = {0}, mx[2] = {0};
    g_vka_mm_tr.cls = 'b';
    if (cnt < 3 || cnt > 4096 || !vka_mm_attr_xy_bounds(d, vin_mask, dw, di, cnt, mn, mx)) return vka_mm_switch_target(cb, 0);
    g_vka_mm_tr.hb = 1; g_vka_mm_tr.cls = '-';
    g_vka_mm_tr.mn[0] = mn[0]; g_vka_mm_tr.mn[1] = mn[1]; g_vka_mm_tr.mx[0] = mx[0]; g_vka_mm_tr.mx[1] = mx[1];
    float w = (float)g_vka_mm.pass_w, h = (float)g_vka_mm.pass_h;
    float bw = mx[0] - mn[0], bh = mx[1] - mn[1], cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    if (stt == 0u && !blend_en && cnt <= 6) {
        int px_fs = (bw >= w * 0.90f && bh >= h * 0.90f);
        int ndc_fs = (mn[0] <= -0.95f && mx[0] >= 0.95f && mn[1] <= -0.95f && mx[1] >= 0.95f);
        if (px_fs || ndc_fs) { g_vka_mm.pass_opaque_fs = 1; g_vka_mm_tr.cls = 'F'; return vka_mm_switch_target(cb, 0); }
    }
    // A blended quad over exactly the whole pass is a menu's frozen scene or dim layer (or a fade): the HUD is not the
    // active layer, so nothing more is captured this frame, in this pass or a later one.
    if (stt == 0u && blend_en && cnt <= 6 && (c0_mask & 0xfu) != 0u
            && fabsf(mn[0]) <= 2.f && fabsf(mn[1]) <= 2.f && fabsf(mx[0] - w) <= 2.f && fabsf(mx[1] - h) <= 2.f) {
        g_vka_mm.fr_modal = 1; g_vka_mm.pass_modal = 1;
    }
    if ((c0_mask & 0xfu) != 0u && cnt >= 4) {
        if (bw >= w * 0.28f && bw <= w * 0.88f && bh >= h * 0.12f && bh <= h * 0.88f
                && fabsf(cx - w * 0.5f) < w * 0.16f && mn[1] < h * 0.66f && mx[1] > h * 0.20f)
            g_vka_mm.pass_modal = 1;
        if (bw >= w * 0.34f && bw <= w * 0.88f && bh <= h * 0.45f && cy > h * 0.62f && fabsf(cx - w * 0.5f) < w * 0.18f)
            g_vka_mm.pass_dialog = 1;
    }
    if (stt != 0u && (c0_mask & 0xfu) == 0u && cnt >= 4 && cnt <= 12) { g_vka_mm_tr.cls = 'S'; vka_mm_try_minimap(mn, mx); return 0; }
    if (g_vka_mm.pass_opaque_fs || g_vka_mm.pass_modal || (c0_mask & 0xfu) == 0u) { g_vka_mm_tr.cls = 'M'; return vka_mm_switch_target(cb, 0); }
    int want = vka_mm_is_mm_draw(mn, mx, stt, cnt);
    if (want) g_vka_mm_tr.cls = 'm';
    if (!want) { want = vka_mm_is_lhud_draw(mn, mx, cnt); if (want) g_vka_mm_tr.cls = 'l'; }
    if (!want && blend_en) { want = vka_mm_is_obj_draw(mn, mx, cnt); if (want) g_vka_mm_tr.cls = 'o'; }
    if (!want) vka_mm_note_foreign(mn, mx, cnt);
    return vka_mm_switch_target(cb, want);
}

static int vka_minimap_want_ui_tex(void) {
    return g_vka_mm.in_hud_pass && !g_vka_mm.pass_opaque_fs && !g_vka_mm.pass_modal
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

static void vka_mm_stream_stage(vka_mm_stream *st, VkCommandBuffer cb, const vka_mm_rect *r) {
    // The HUD of one frame is recorded over several render passes: every pass end stages again into the same slot,
    // so the copy that is read back is the last one, taken after the frame's final HUD draw (the first held only a prefix).
    if (atomic_load_explicit(&st->pending_valid, memory_order_acquire)) return;
    VkDeviceSize need = (VkDeviceSize)r->w * r->h * 4u;
    if ((g_vka_mm.frame_counter & 1u) != 0u || !r->w || !r->h || need > st->slot_bytes) return;
    if (!g_vka_mm.hud_ready || !vka_mm_ensure_stream_ring(st)) return;
    vka_mm_slot *dst = &st->slot[st->write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u},
        .imageOffset = {r->x, r->y, 0}, .imageExtent = {r->w, r->h, 1u}
    };
    vk.vkCmdCopyImageToBuffer(cb, g_vka_mm.hud_img, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1u, &rg);
    VkBufferMemoryBarrier bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst->buf, .offset = 0, .size = need
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0u, 0u, NULL, 1u, &bb, 0u, NULL);
    st->wrote_in_frame = 1; st->write_w = r->w; st->write_h = r->h; st->write_fmt = g_vka_mm.hud_fmt;
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

static void vka_minimap_pass_end(VkCommandBuffer cb, vka_img *im) {
    int was = g_vka_mm.in_hud_pass, cleared = g_vka_mm.pass_hud_cleared;
    int mh = g_vka_mm.pass_mm_hit, oh = g_vka_mm.pass_obj_hit, op = g_vka_mm.pass_opaque_fs, md = g_vka_mm.pass_modal;
    int lh = (!op && !md && g_vka_mm.pass_lhud_any && (mh || (g_vka_mm.pass_lhud_top && !g_vka_mm.pass_dialog))
        && g_vka_mm.hit_lhud.w >= 64u && g_vka_mm.hit_lhud.h >= 64u);
    float oicx = g_vka_mm.obj_icon_cx;
    int obj_pair = (!op && !md && g_vka_mm.cand_obj_text && g_vka_mm.pass_obj_icon
        && fabsf(g_vka_mm.cand_text_cy - g_vka_mm.obj_icon_cy) <= (float)g_vka_mm.pass_h * 0.055f
        && g_vka_mm.cand_text_mn_x >= oicx - 12.f
        && g_vka_mm.cand_text_mn_x <= oicx + (float)g_vka_mm.pass_h * 0.10f);
    g_vka_mm.in_hud_pass = 0; g_vka_mm.cur_hud = 0;
    if (!was || !cb || !im || im->img != g_vka_mm.pass_im || !V.dev || vka_hung()) return;
    if (!atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire) || op || md) return;
    uint64_t fr = vka_frame();
    if (obj_pair) {
        g_vka_mm.learn_obj_icon_x = oicx; g_vka_mm.learn_obj_text_x = g_vka_mm.cand_text_mn_x;
        g_vka_mm.learn_obj_cy = g_vka_mm.obj_icon_cy; g_vka_mm.saw_obj_frame = 1;
    } else if (oh && cleared) {
        g_vka_mm.saw_obj_frame = 1;
    }
    if (!cleared) return;
    if (mh) {
        vka_mm_find_cand(g_vka_mm.hit_mm.w, g_vka_mm.hit_mm.h, g_vka_mm.hud_fmt, im->serial);
        vka_mm_stream_stage(&g_vka_mm.mm, cb, &g_vka_mm.hit_mm);
        g_vka_mm.saw_mm_frame = 1; g_vka_mm.frame_got_mm = fr; g_vka_mm.saved_hit_img = im->img;
    }
    if (oh) {
        g_vka_mm.prev_hit_obj = g_vka_mm.hit_obj;
        vka_mm_stream_stage(&g_vka_mm.obj, cb, &g_vka_mm.hit_obj);
        g_vka_mm.frame_got_obj = fr; g_vka_mm.saved_hit_img = im->img;
    }
    if (lh) {
        vka_mm_stream_stage(&g_vka_mm.lhud, cb, &g_vka_mm.hit_lhud);
        g_vka_mm.saw_lhud_frame = 1; g_vka_mm.frame_got_lhud = fr; g_vka_mm.saved_hit_img = im->img;
    }
    if ((mh || oh || lh) && atomic_load_explicit(&g_vka_mm.ui_want, memory_order_relaxed) == 1) vka_mm_stage_ui_tex(cb);
}

static void vka_minimap_pass_abort(void) {
    g_vka_mm.in_hud_pass = 0; g_vka_mm.cur_hud = 0; g_vka_mm.pass_hud_cleared = 0;
    g_vka_mm.pass_opaque_fs = 0; g_vka_mm.pass_modal = 0; g_vka_mm.pass_dialog = 0;
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
    vka_mm_stream_present(&g_vka_mm.lhud, fr, fq, dn);
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
    if (g_vka_mm.saw_lhud_frame) g_vka_mm.lhud_ttl = VKA_MM_TTL; else if (g_vka_mm.lhud_ttl > 0u) g_vka_mm.lhud_ttl--;
    g_vka_mm.saw_mm_frame = 0; g_vka_mm.saw_obj_frame = 0; g_vka_mm.saw_lhud_frame = 0;
}

static void vka_mm_copy_rgba(uint8_t *dst, const uint8_t *src, uint32_t w, uint32_t h, VkFormat fmt, int flip, int alpha_mode) {
    int bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *sr = src + (size_t)(flip ? (h - 1u - y) : y) * w * 4u;
        uint8_t *dr = dst + (size_t)y * w * 4u;
        for (uint32_t x = 0; x < w; x++) {
            size_t o = (size_t)x * 4u;
            uint8_t r = sr[o + (bgra ? 2 : 0)], g = sr[o + 1], b = sr[o + (bgra ? 0 : 2)], a = sr[o + 3];
            if (alpha_mode == 0) a = 255u;
            else if (alpha_mode == 2) {
                uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
                if (a < mx) a = mx;
            }
            dr[o + 0] = r; dr[o + 1] = g; dr[o + 2] = b; dr[o + 3] = a;
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

static int vka_mm_read_overlay_stream(
    vka_mm_stream *st, uint32_t ttl, int saw_frame, uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    if (!meta) return 0;
    atomic_store_explicit(&g_vka_mm.want_active, 1, memory_order_release);
    uint32_t client_seq = (uint32_t)meta[2];
    int updated = 0;
    pthread_mutex_lock(&g_vka_mm.mu);
    int active = (ttl > 0u || saw_frame || atomic_load_explicit(&st->pending_valid, memory_order_relaxed)) ? 1 : 0;
    int rslot = st->ready_slot;
    uint32_t w = st->ready_w, h = st->ready_h, seq = st->seq;
    if (active && rslot >= 0 && rslot < VKA_MM_SLOTS && st->slot[rslot].ptr && seq != client_seq) {
        size_t need = (size_t)w * h * 4u;
        if (rgba_out && need > 0 && max_bytes > 0 && (size_t)max_bytes >= need) {
            vka_mm_copy_rgba(rgba_out, st->slot[rslot].ptr, w, h, st->ready_fmt, vka_origin_ll(), 2);
            updated = 1;
        }
    }
    meta[0] = (int32_t)w; meta[1] = (int32_t)h; meta[2] = (int32_t)seq; meta[3] = active;
    pthread_mutex_unlock(&g_vka_mm.mu);
    return updated;
}

__attribute__((visibility("default")))
int nxhost_companion_objective(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    return vka_mm_read_overlay_stream(&g_vka_mm.obj, g_vka_mm.obj_ttl, g_vka_mm.saw_obj_frame, rgba_out, max_bytes, meta);
}

__attribute__((visibility("default")))
int nxhost_companion_lhud(uint8_t *rgba_out, int max_bytes, int32_t meta[4]) {
    return vka_mm_read_overlay_stream(&g_vka_mm.lhud, g_vka_mm.lhud_ttl, g_vka_mm.saw_lhud_frame, rgba_out, max_bytes, meta);
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
    if (!v) { g_vka_mm.mm_ttl = 0u; g_vka_mm.obj_ttl = 0u; g_vka_mm.lhud_ttl = 0u; g_vka_mm.prev_hit_obj = (vka_mm_rect){0, 0, 0u, 0u}; }
}
