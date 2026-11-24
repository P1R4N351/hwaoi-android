// SPDX-License-Identifier: MIT
#pragma once
// Native Vulkan offscreen minimap render-target capture (2-slot staging ring).
// Included only by vka_draw.c; exports nxhost_companion_minimap* for nxlaunch.c.

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    VKA_MM_MIN_SIDE = 128u,
    VKA_MM_MAX_SIDE = 512u,
    VKA_MM_SLOTS = 2,
    VKA_MM_MAX_CAND = 8
};
#define VKA_MM_BUF_BYTES ((VkDeviceSize)(VKA_MM_MAX_SIDE * VKA_MM_MAX_SIDE * 4u))

uint64_t vka_done(void);
uint64_t vka_fence_queued(void);
int vka_origin_ll(void);

typedef struct { VkBuffer buf; VkDeviceMemory mem; uint8_t *ptr; } vka_mm_slot;
typedef struct { uint32_t w, h; VkFormat fmt; uint64_t serial; } vka_mm_cand;

static struct {
    pthread_mutex_t mu;
    vka_mm_slot slot[VKA_MM_SLOTS];
    int ring_ready, ring_failed;
    vka_mm_cand cand[VKA_MM_MAX_CAND];
    _Atomic int cand_count, want_active, pending_valid;
    _Atomic uint32_t cand_idx;
    uint32_t frame_counter, pending_frame;
    int write_slot, pending_slot, ready_slot, wrote_in_frame;
    uint32_t write_w, write_h, pending_w, pending_h, ready_w, ready_h, seq;
    VkFormat write_fmt, pending_fmt, ready_fmt;
    uint64_t pending_fence;
    int env_init;
    uint32_t env_w, env_h;
} g_vka_mm = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .ready_slot = -1,
    .pending_slot = -1,
};

static int vka_mm_supported_fmt(VkFormat fmt) {
    return fmt == VK_FORMAT_R8G8B8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_SRGB
        || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
}

static void vka_mm_init_env(void) {
    if (g_vka_mm.env_init) return;
    g_vka_mm.env_init = 1;
    const char *e = getenv("DM_MINIMAP_RT");
    unsigned w = 0, h = 0;
    if (e && sscanf(e, "%ux%u", &w, &h) == 2
            && w >= 64u && w <= VKA_MM_MAX_SIDE && h >= 64u && h <= VKA_MM_MAX_SIDE) {
        g_vka_mm.env_w = w;
        g_vka_mm.env_h = h;
        VLOG("[companion-rt] DM_MINIMAP_RT=%ux%u", w, h);
    }
}

static int vka_mm_match_size(uint32_t w, uint32_t h) {
    vka_mm_init_env();
    if (g_vka_mm.env_w) return w == g_vka_mm.env_w && h == g_vka_mm.env_h;
    return w == h && w >= VKA_MM_MIN_SIDE && w <= VKA_MM_MAX_SIDE;
}

static int vka_mm_find_cand(uint32_t w, uint32_t h, VkFormat fmt, uint64_t serial) {
    int cnt = atomic_load_explicit(&g_vka_mm.cand_count, memory_order_relaxed);
    for (int i = 0; i < cnt && i < VKA_MM_MAX_CAND; i++) {
        vka_mm_cand *c = &g_vka_mm.cand[i];
        if (c->w == w && c->h == h && c->fmt == fmt && c->serial == serial) return i;
    }
    if (cnt >= VKA_MM_MAX_CAND) return -1;
    g_vka_mm.cand[cnt] = (vka_mm_cand){w, h, fmt, serial};
    atomic_store_explicit(&g_vka_mm.cand_count, cnt + 1, memory_order_release);
    VLOG("[companion-rt] candidate #%d: %ux%u fmt=%d serial=%llu",
         cnt, w, h, (int)fmt, (unsigned long long)serial);
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

static void vka_minimap_pass_end(
    VkCommandBuffer cb, vka_img *im, uint32_t lv, uint32_t l0, VkFormat fmt, uint32_t w, uint32_t h) {
    if (!cb || !im || !im->img || !V.dev || vka_hung()) return;
    if (im->samples > 1 || im->d.target == NVN_TEXTURE_TARGET_3D) return;
    if (!vka_mm_supported_fmt(fmt) || !vka_mm_match_size(w, h)) return;
    int ci = vka_mm_find_cand(w, h, fmt, im->serial);
    if (ci < 0 || !atomic_load_explicit(&g_vka_mm.want_active, memory_order_acquire)) return;
    if (g_vka_mm.wrote_in_frame || atomic_load_explicit(&g_vka_mm.pending_valid, memory_order_acquire)) return;
    if ((g_vka_mm.frame_counter & 3u) != 0u) return;
    int cnt = atomic_load_explicit(&g_vka_mm.cand_count, memory_order_relaxed);
    if (cnt <= 0 || ci != (int)(atomic_load_explicit(&g_vka_mm.cand_idx, memory_order_acquire) % (uint32_t)cnt)) return;
    if ((VkDeviceSize)w * h * 4u > VKA_MM_BUF_BYTES || !vka_mm_ensure_ring()) return;

    vka_mm_slot *dst = &g_vka_mm.slot[g_vka_mm.write_slot];
    VkBufferImageCopy rg = {
        .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lv, l0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {w, h, 1}
    };
    vk.vkCmdCopyImageToBuffer(cb, im->img, VK_IMAGE_LAYOUT_GENERAL, dst->buf, 1, &rg);
    VkBufferMemoryBarrier bb = {
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = dst->buf,
        .offset = 0,
        .size = (VkDeviceSize)w * h * 4u
    };
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
    g_vka_mm.wrote_in_frame = 1;
    g_vka_mm.write_w = w;
    g_vka_mm.write_h = h;
    g_vka_mm.write_fmt = fmt;
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
    int done = (g_vka_mm.frame_counter - g_vka_mm.pending_frame) >= 2u
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
