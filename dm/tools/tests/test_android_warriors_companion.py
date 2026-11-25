# SPDX-License-Identifier: MIT
"""Host JVM verification and pure-Java checks for Warriors companion model, display, and KTGL extractor."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
JAVA = REPO / 'android/app/src/main/java/dev/nxnative/android'
PURE_NAMES = (
    'CompanionState.java',
    'CompanionDisplay.java',
    'WarriorsCompanionModel.java',
    'WarriorsCompanionLayout.java',
    'WarriorsKtglArt.java',
    'Nup.java',
    'Zstd.java',
)
CLOS_NAMES = (
    'Nsp.java',
    'Pack.java',
    'DumpLayout.java',
    'BuildCheck.java',
    'RomfsCheck.java',
    'Profile.java',
    'ShaderLibs.java',
    'PickedPath.java',
    'CopyPlan.java',
    'Words.java',
)
SRCS = [JAVA / n for n in PURE_NAMES]
COMPILE_SRCS = [JAVA / n for n in PURE_NAMES + CLOS_NAMES]
TEST = REPO / 'android/app/src/jvmtest/java/dev/nxnative/android/WarriorsCompanionJvmTest.java'
LAYOUT_TEST = REPO / 'android/app/src/jvmtest/java/dev/nxnative/android/WarriorsCompanionLayoutTest.java'

STUB_BUILD_CONFIG = '''package dev.nxnative.android;
public final class BuildConfig {
    public static final String NX_PROFILES = "";
    public static final String NX_DMSH_VK_CHAIN = "";
    public static final String NX_DMSH_VK_ABI = "";
    public static final String NX_DMSH_VK_ABI_COMPAT = "";
    public static final String NX_DMSH_VK_ABI_SAME = "";
}
'''



MM_C_HARNESS = r'''#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

typedef uint64_t VkImage;
typedef uint64_t VkImageView;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkBuffer;
typedef uint64_t VkCommandBuffer;
typedef uint64_t VkDevice;
typedef uint64_t VkPhysicalDevice;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkFormat;
typedef uint32_t VkImageLayout;
typedef uint32_t VkFilter;
typedef uint32_t VkImageUsageFlags;
typedef uint32_t VkResult;

#define VK_NULL_HANDLE 0ull
#define VK_WHOLE_SIZE (~0ull)
#define VK_FORMAT_UNDEFINED 0u
#define VK_FORMAT_R8G8B8A8_UNORM 37u
#define VK_FORMAT_R8G8B8A8_SRGB 43u
#define VK_FORMAT_B8G8R8A8_UNORM 44u
#define VK_FORMAT_B8G8R8A8_SRGB 50u
#define VK_FORMAT_R8_UNORM 9u
#define VK_FORMAT_R8_SNORM 10u
#define VK_FORMAT_R16_UNORM 70u
#define VK_FORMAT_R16_SFLOAT 76u
#define VK_FORMAT_R32_SFLOAT 100u
#define VK_FORMAT_R8G8_UNORM 16u
#define VK_FORMAT_R8G8_SNORM 17u
#define VK_FORMAT_R16G16_UNORM 77u
#define VK_FORMAT_R16G16_SFLOAT 83u
#define VK_FORMAT_R32G32_SFLOAT 103u
#define VK_FORMAT_BC4_UNORM_BLOCK 139u
#define VK_FORMAT_BC4_SNORM_BLOCK 140u
#define VK_FORMAT_BC5_UNORM_BLOCK 141u
#define VK_FORMAT_BC5_SNORM_BLOCK 142u
#define VK_IMAGE_LAYOUT_UNDEFINED 0u
#define VK_IMAGE_LAYOUT_GENERAL 1u
#define VK_IMAGE_ASPECT_COLOR_BIT 1u
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 1u
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 2u
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 16u
#define VK_FORMAT_FEATURE_BLIT_SRC_BIT 0x400u
#define VK_FORMAT_FEATURE_BLIT_DST_BIT 0x800u
#define VK_FILTER_LINEAR 1u
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12u
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5u
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14u
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15u
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 45u
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER 44u
#define VK_STRUCTURE_TYPE_MEMORY_BARRIER 46u
#define VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO 1000044001u
#define VK_STRUCTURE_TYPE_RENDERING_INFO 1000044000u
#define VK_ATTACHMENT_LOAD_OP_LOAD 0u
#define VK_ATTACHMENT_STORE_OP_STORE 0u
#define VK_QUEUE_FAMILY_IGNORED (~0u)
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 2u
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 1u
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 2u
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 4u
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT 8u
#define VK_IMAGE_TYPE_2D 1u
#define VK_SAMPLE_COUNT_1_BIT 1u
#define VK_IMAGE_TILING_OPTIMAL 0u
#define VK_IMAGE_VIEW_TYPE_2D 1u
#define VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 1u
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x1000u
#define VK_PIPELINE_STAGE_HOST_BIT 0x4000u
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 0x400u
#define VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 0x100u
#define VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT 0x200u
#define VK_ACCESS_TRANSFER_READ_BIT 0x800u
#define VK_ACCESS_TRANSFER_WRITE_BIT 0x1000u
#define VK_ACCESS_HOST_READ_BIT 0x2000u
#define VK_ACCESS_COLOR_ATTACHMENT_READ_BIT 0x80u
#define VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT 0x100u
#define VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT 0x200u
#define VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT 0x400u
#define VKA_NSTAGE 8

enum {
    NVN_FORMAT_R16F = 0x5, NVN_FORMAT_R32F = 0xA,
    NVN_FORMAT_RG16F = 0x11, NVN_FORMAT_RG32F = 0x16,
    NVN_FORMAT_RGB16F = 0x1D, NVN_FORMAT_RGB32F = 0x22,
    NVN_FORMAT_RGBA16F = 0x29, NVN_FORMAT_RGBA32F = 0x2E,
};

typedef struct { int32_t x, y; } VkOffset2D;
typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { VkOffset2D offset; VkExtent2D extent; } VkRect2D;
typedef struct { int32_t x, y, z; } VkOffset3D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef union { float float32[4]; } VkClearColorValue;
typedef struct { uint32_t aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct { uint32_t aspectMask, mipLevel, baseArrayLayer, layerCount; } VkImageSubresourceLayers;
typedef struct { uint32_t sType; VkDeviceSize size; uint32_t usage; } VkBufferCreateInfo;
typedef struct { uint64_t size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { uint32_t sType; uint64_t allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { uint32_t sType, imageType; VkFormat format; VkExtent3D extent; uint32_t mipLevels, arrayLayers, samples, tiling, usage, initialLayout; } VkImageCreateInfo;
typedef struct { uint32_t sType; VkImage image; uint32_t viewType; VkFormat format; VkImageSubresourceRange subresourceRange; } VkImageViewCreateInfo;
typedef struct { uint32_t sType, srcAccessMask, dstAccessMask, oldLayout, newLayout, srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange; } VkImageMemoryBarrier;
typedef struct { uint32_t sType, srcAccessMask, dstAccessMask, srcQueueFamilyIndex, dstQueueFamilyIndex; VkBuffer buffer; uint64_t offset, size; } VkBufferMemoryBarrier;
typedef struct { uint32_t sType, srcAccessMask, dstAccessMask; } VkMemoryBarrier;
typedef struct { uint64_t bufferOffset; VkImageSubresourceLayers imageSubresource; VkOffset3D imageOffset; VkExtent3D imageExtent; } VkBufferImageCopy;
typedef struct { VkImageSubresourceLayers srcSubresource; VkOffset3D srcOffsets[2]; VkImageSubresourceLayers dstSubresource; VkOffset3D dstOffsets[2]; } VkImageBlit;
typedef struct { uint32_t optimalTilingFeatures; } VkFormatProperties;
typedef struct { uint32_t sType; VkImageView imageView; uint32_t imageLayout, loadOp, storeOp; } VkRenderingAttachmentInfo;
typedef struct { uint32_t sType; VkRect2D renderArea; uint32_t layerCount, colorAttachmentCount; const VkRenderingAttachmentInfo *pColorAttachments, *pDepthAttachment, *pStencilAttachment; } VkRenderingInfo;

typedef struct { uint64_t addr, size; } vka_range;
typedef struct { vka_range ubo[VKA_NSTAGE][16]; uint32_t base_instance; } vka_bstate;
typedef struct { uint32_t fmt : 8, offset : 19, stream : 5; } va_st;
typedef struct { uint32_t stride, divisor; } vs_st;
typedef struct { uint64_t address, size; } NVNbufferRange;
typedef struct { vka_bstate b; va_st attr[16]; int nattr; vs_st stream[16]; NVNbufferRange vbuf[16]; float vp[16][4]; int has_vp; uint8_t vp_sw[16][4]; uint32_t st_ref[2]; } vka_dstate;
typedef struct { int32_t prim, first, count, instances, base_instance; uint64_t ind; } p_draw;
typedef struct { int32_t prim, itype, count, base_vertex, base_instance, instances; uint64_t ibuf, ind; } p_draw_idx;
typedef struct { VkImage img; struct { VkFormat vk; } f; uint64_t serial; uint32_t w, h, layers, levels, samples, depth, usage, aspect; int gpu_owned; } vka_img;

static struct { VkDevice dev; VkPhysicalDevice pd; } V = { 1ull, 1ull };
static uint64_t g_f = 10;
static uint8_t g_bufs[16][1024 * 1024];
static uint32_t g_nbuf = 0;

static void stub_unmap(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; }
static void stub_dbuf(VkDevice d, VkBuffer b, const void *a) { (void)d; (void)b; (void)a; }
static void stub_fmem(VkDevice d, VkDeviceMemory m, const void *a) { (void)d; (void)m; (void)a; }
static void stub_dimg(VkDevice d, VkImage i, const void *a) { (void)d; (void)i; (void)a; }
static void stub_div(VkDevice d, VkImageView v, const void *a) { (void)d; (void)v; (void)a; }
static void stub_wi(VkDevice d) { (void)d; }
static VkResult stub_cbuf(VkDevice d, const VkBufferCreateInfo *ci, const void *a, VkBuffer *o) { (void)d; (void)ci; (void)a; *o = ++g_nbuf; return 0; }
static void stub_bmr(VkDevice d, VkBuffer b, VkMemoryRequirements *r) { (void)d; (void)b; r->size = 1024 * 1024; r->memoryTypeBits = 1u; }
static VkResult stub_amem(VkDevice d, const VkMemoryAllocateInfo *ai, const void *a, VkDeviceMemory *o) { (void)d; (void)ai; (void)a; *o = g_nbuf; return 0; }
static VkResult stub_bindb(VkDevice d, VkBuffer b, VkDeviceMemory m, uint64_t o) { (void)d; (void)b; (void)m; (void)o; return 0; }
static VkResult stub_map(VkDevice d, VkDeviceMemory m, uint64_t o, uint64_t s, uint32_t fl, void **p) { (void)d; (void)o; (void)s; (void)fl; *p = g_bufs[m & 15u]; return 0; }
static VkResult stub_cimg(VkDevice d, const VkImageCreateInfo *ci, const void *a, VkImage *o) { (void)d; (void)ci; (void)a; *o = 100ull + ++g_nbuf; return 0; }
static void stub_imr(VkDevice d, VkImage i, VkMemoryRequirements *r) { (void)d; (void)i; r->size = 4096; r->memoryTypeBits = 1u; }
static VkResult stub_bindi(VkDevice d, VkImage i, VkDeviceMemory m, uint64_t o) { (void)d; (void)i; (void)m; (void)o; return 0; }
static VkResult stub_civ(VkDevice d, const VkImageViewCreateInfo *ci, const void *a, VkImageView *o) { (void)d; (void)ci; (void)a; *o = 200ull + ++g_nbuf; return 0; }
static void stub_fp(VkPhysicalDevice p, VkFormat f, VkFormatProperties *fp) { (void)p; (void)f; fp->optimalTilingFeatures = 0xfffu; }
static void stub_pb(VkCommandBuffer c, uint32_t s, uint32_t d, uint32_t f, uint32_t m, const VkMemoryBarrier *mb, uint32_t b, const VkBufferMemoryBarrier *bb, uint32_t i, const VkImageMemoryBarrier *ib) { (void)c; (void)s; (void)d; (void)f; (void)m; (void)mb; (void)b; (void)bb; (void)i; (void)ib; }
static void stub_clr(VkCommandBuffer c, VkImage i, uint32_t l, const VkClearColorValue *cv, uint32_t n, const VkImageSubresourceRange *r) { (void)c; (void)i; (void)l; (void)cv; (void)n; (void)r; }
static void stub_br(VkCommandBuffer c, const VkRenderingInfo *ri) { (void)c; (void)ri; }
static void stub_er(VkCommandBuffer c) { (void)c; }
static int g_ncib;   /* image-to-buffer copies: one per staged stream */
static void stub_cib(VkCommandBuffer c, VkImage i, uint32_t l, VkBuffer b, uint32_t n, const VkBufferImageCopy *r) { (void)c; (void)i; (void)l; (void)b; (void)n; (void)r; g_ncib++; }
static void stub_bi(VkCommandBuffer c, VkImage s, uint32_t sl, VkImage d, uint32_t dl, uint32_t n, const VkImageBlit *r, VkFilter f) { (void)c; (void)s; (void)sl; (void)d; (void)dl; (void)n; (void)r; (void)f; }

static struct {
    void (*vkUnmapMemory)(VkDevice, VkDeviceMemory);
    void (*vkDestroyBuffer)(VkDevice, VkBuffer, const void *);
    void (*vkFreeMemory)(VkDevice, VkDeviceMemory, const void *);
    VkResult (*vkCreateBuffer)(VkDevice, const VkBufferCreateInfo *, const void *, VkBuffer *);
    void (*vkGetBufferMemoryRequirements)(VkDevice, VkBuffer, VkMemoryRequirements *);
    VkResult (*vkAllocateMemory)(VkDevice, const VkMemoryAllocateInfo *, const void *, VkDeviceMemory *);
    VkResult (*vkBindBufferMemory)(VkDevice, VkBuffer, VkDeviceMemory, uint64_t);
    VkResult (*vkMapMemory)(VkDevice, VkDeviceMemory, uint64_t, uint64_t, uint32_t, void **);
    VkResult (*vkCreateImage)(VkDevice, const VkImageCreateInfo *, const void *, VkImage *);
    void (*vkGetImageMemoryRequirements)(VkDevice, VkImage, VkMemoryRequirements *);
    VkResult (*vkBindImageMemory)(VkDevice, VkImage, VkDeviceMemory, uint64_t);
    void (*vkDestroyImage)(VkDevice, VkImage, const void *);
    void (*vkDestroyImageView)(VkDevice, VkImageView, const void *);
    void (*vkDeviceWaitIdle)(VkDevice);
    VkResult (*vkCreateImageView)(VkDevice, const VkImageViewCreateInfo *, const void *, VkImageView *);
    void (*vkGetPhysicalDeviceFormatProperties)(VkPhysicalDevice, VkFormat, VkFormatProperties *);
    void (*vkCmdPipelineBarrier)(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, const VkMemoryBarrier *, uint32_t, const VkBufferMemoryBarrier *, uint32_t, const VkImageMemoryBarrier *);
    void (*vkCmdClearColorImage)(VkCommandBuffer, VkImage, uint32_t, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *);
    void (*vkCmdBeginRendering)(VkCommandBuffer, const VkRenderingInfo *);
    void (*vkCmdEndRendering)(VkCommandBuffer);
    void (*vkCmdCopyImageToBuffer)(VkCommandBuffer, VkImage, uint32_t, VkBuffer, uint32_t, const VkBufferImageCopy *);
    void (*vkCmdBlitImage)(VkCommandBuffer, VkImage, uint32_t, VkImage, uint32_t, uint32_t, const VkImageBlit *, VkFilter);
} vk = {
    stub_unmap, stub_dbuf, stub_fmem, stub_cbuf, stub_bmr, stub_amem, stub_bindb, stub_map,
    stub_cimg, stub_imr, stub_bindi, stub_dimg, stub_div, stub_wi, stub_civ, stub_fp,
    stub_pb, stub_clr, stub_br, stub_er, stub_cib, stub_bi
};

static uint32_t vka_mem_type(uint32_t b, uint32_t r, uint32_t p) { (void)r; (void)p; return (b & 1u) ? 0u : UINT32_MAX; }
static int vka_hung(void) { return 0; }
static void vka_oq_pass_end(void) {}
static void vka_barrier(VkCommandBuffer c) { (void)c; }
uint64_t vka_frame(void) { return g_f; }
uint64_t vka_done(void) { return g_f; }
uint64_t vka_fence_queued(void) { return g_f; }
int vka_origin_ll(void) { return 0; }
int vka_cpu_read(uint64_t addr, void *out, size_t n) {
    if (!addr || !out || !n) return 0;
    memcpy(out, (const void *)(uintptr_t)addr, n);
    return 1;
}

#define VLOG(...) ((void)0)
#define VKA_ENV(key, def) (def)
int dm_ktgl_tag_get(uint64_t ubo, uint64_t *chunk, uint32_t *off, uint32_t *len) { (void)ubo; (void)chunk; (void)off; (void)len; return 0; }
#include "vka_minimap.h"

typedef struct { float x, y, z, idx; } vtx_idx_pos;

static void make_quad_vtx(vtx_idx_pos *q, float x0, float y0, float x1, float y1, float z, float idx) {
    float xs[6] = {x0, x1, x0, x1, x1, x0}, ys[6] = {y0, y0, y1, y0, y1, y1};
    for (int i = 0; i < 6; i++) { q[i].x = xs[i]; q[i].y = ys[i]; q[i].z = z; q[i].idx = idx; }
}

static void init_id_m34(float m[12]) {
    memset(m, 0, 48);
    m[0] = 1.f; m[5] = 1.f; m[10] = 1.f;
}

static void make_ortho_1280(float ubo[64]) {
    memset(ubo, 0, 256);
    ubo[8 + 0] = 2.f / 1280.f; ubo[8 + 5] = -2.f / 720.f; ubo[8 + 10] = 1.f;
    ubo[8 + 12] = -1.f; ubo[8 + 13] = 1.f; ubo[8 + 15] = 1.f;
    init_id_m34(&ubo[24]);
}

static void make_persp(float ubo[64]) {
    memset(ubo, 0, 256);
    ubo[0] = 1.f; ubo[5] = 16.f / 9.f; ubo[10] = -1.f; ubo[11] = -1.f; ubo[14] = -1.f;
    init_id_m34(&ubo[16]);
}

static vka_dstate make_dstate(void *vbuf, void *ubo0, uint64_t usz0, void *ubo1, uint64_t usz1) {
    vka_dstate d; memset(&d, 0, sizeof d);
    d.has_vp = 1; d.vp[0][2] = 1920.f; d.vp[0][3] = 1080.f;
    d.nattr = 2;
    d.attr[0] = (va_st){NVN_FORMAT_R32F, 12u, 0u};
    d.attr[1] = (va_st){NVN_FORMAT_RGB32F, 0u, 0u};
    d.stream[0] = (vs_st){16u, 0u};
    d.vbuf[0] = (NVNbufferRange){(uint64_t)(uintptr_t)vbuf, 4096u};
    d.b.ubo[0][0] = (vka_range){(uint64_t)(uintptr_t)ubo0, usz0};
    d.b.ubo[0][1] = (vka_range){(uint64_t)(uintptr_t)ubo1, usz1};
    return d;
}

static void test_proj_decode(void) {
    float ubo_ortho[64], ubo_persp[64], mn[2], mx[2];
    make_ortho_1280(ubo_ortho); make_persp(ubo_persp);
    vtx_idx_pos vq[64];
    memset(vq, 0, sizeof vq);
    make_quad_vtx(vq, 640.f, 360.f, 960.f, 540.f, 0.f, 0.f);
    vka_dstate d_o = make_dstate(vq, ubo_ortho, 256u, NULL, 0u);
    g_vka_mm.pass_w = 1920u; g_vka_mm.pass_h = 1080u;
    assert(vka_mm_ubo_xy_bounds(&d_o, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 6, mn, mx));
    assert(fabsf(mn[0] - 960.f) < 2.f && fabsf(mx[0] - 1440.f) < 2.f);
    assert(fabsf(mn[1] - 540.f) < 2.f && fabsf(mx[1] - 810.f) < 2.f);
    make_quad_vtx(vq, 0.f, 0.f, 0.25f, 0.1f, -2.f, 0.f);
    vka_dstate d_p = make_dstate(vq, NULL, 0u, ubo_persp, 0u);
    assert(vka_mm_ubo_xy_bounds(&d_p, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 6, mn, mx));
    assert(mn[0] >= 958.f && mx[0] <= 1210.f);
}

static void run_obj_frame(
    const float fb_ortho[64], const vtx_idx_pos q_bar[18], const vtx_idx_pos q_txt[180],
    const vtx_idx_pos q_ic[12], int icon_draws, const vtx_idx_pos q_sub[18], const vtx_idx_pos q_subt[180]) {
    vka_img im = { .img = 55ull, .serial = 4ull, .w = 1920u, .h = 1080u, .layers = 1u, .samples = 1u };
    vka_minimap_trace_draw(1ull, 2ull, &im, 1);   /* off by default: logs nothing, changes nothing */
    VkRenderingAttachmentInfo sp = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 30ull, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u };
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);

    vka_dstate ds_bar = make_dstate((void *)q_bar, (void *)fb_ortho, 256u, NULL, 0u);
    vka_minimap_select_target(1ull, &ds_bar, 3u, &(p_draw){0, 0, 18, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1 && g_vka_mm.pass_obj_pill == 1);

    vka_dstate ds_txt = make_dstate((void *)q_txt, (void *)fb_ortho, 256u, NULL, 0u);
    vka_minimap_select_target(1ull, &ds_txt, 3u, &(p_draw){0, 0, 174, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1 && g_vka_mm.pass_obj_text == 1);

    vka_dstate ds_ic = make_dstate((void *)q_ic, (void *)fb_ortho, 256u, NULL, 0u);
    for (int i = 0; i < icon_draws; i++) {
        vka_minimap_select_target(1ull, &ds_ic, 3u, &(p_draw){0, i * 6, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
        assert(g_vka_mm.cur_hud == 1);
    }

    vka_dstate ds_sub = make_dstate((void *)q_sub, (void *)fb_ortho, 256u, NULL, 0u);
    vka_minimap_select_target(1ull, &ds_sub, 3u, &(p_draw){0, 0, 18, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0 && g_vka_mm.pass_dialog == 1);

    vka_dstate ds_subt = make_dstate((void *)q_subt, (void *)fb_ortho, 256u, NULL, 0u);
    vka_minimap_select_target(1ull, &ds_subt, 3u, &(p_draw){0, 0, 180, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0);

    vka_minimap_pass_end(1ull, &im);
    assert(g_vka_mm.saw_obj_frame == 1);
}

static void test_pass_462636(void) {
    float fb_ortho[64]; memset(fb_ortho, 0, sizeof fb_ortho);
    fb_ortho[8 + 0] = 2.f / 1920.f; fb_ortho[8 + 5] = -2.f / 1080.f; fb_ortho[8 + 10] = 1.f;
    fb_ortho[8 + 12] = -1.f; fb_ortho[8 + 13] = 1.f; fb_ortho[8 + 15] = 1.f;
    init_id_m34(&fb_ortho[24]);

    vtx_idx_pos q_bar[18], q_txt[180], q_ic[12], q_sub[18], q_subt[180];
    make_quad_vtx(&q_bar[0], 1420.f, 468.f, 1920.f, 524.f, 0.f, 0.f);
    make_quad_vtx(&q_bar[6], 1440.f, 472.f, 1920.f, 520.f, 0.f, 0.f);
    make_quad_vtx(&q_bar[12], 1460.f, 476.f, 1920.f, 516.f, 0.f, 0.f);
    for (int q = 0; q < 180; q += 6) {
        make_quad_vtx(&q_txt[q], 1493.f, 478.f, 1781.f, 514.f, 0.f, 0.f);
        make_quad_vtx(&q_subt[q], 759.f, 846.f, 1161.f, 894.f, 0.f, 0.f);
    }
    make_quad_vtx(&q_ic[0], 1443.f, 476.f, 1483.f, 516.f, 0.f, 0.f);
    make_quad_vtx(&q_ic[6], 1383.f, 416.f, 1543.f, 576.f, 0.f, 0.f);
    for (int q = 0; q < 18; q += 6) make_quad_vtx(&q_sub[q], 620.f, 840.f, 1300.f, 900.f, 0.f, 0.f);

    nxhost_companion_minimap_enable(1);
    assert(!vka_minimap_in_hud_pass());
    (void)&vka_minimap_bind_main;
    (void)&vka_minimap_note_fs_tex;
    (void)&vka_minimap_pass_abort;

    g_f = 10; run_obj_frame(fb_ortho, q_bar, q_txt, q_ic, 1, q_sub, q_subt);
    vka_mm_rect r1 = g_vka_mm.hit_obj;
    assert(r1.x <= 1380 && r1.x + (int32_t)r1.w >= 1916);
    assert(r1.y <= 410 && r1.y + (int32_t)r1.h >= 580);
    vka_minimap_present();

    g_f = 12; run_obj_frame(fb_ortho, q_bar, q_txt, q_ic, 2, q_sub, q_subt);
    vka_mm_rect r2 = g_vka_mm.hit_obj;
    assert(r1.x == r2.x && r1.y == r2.y && r1.w == r2.w && r1.h == r2.h);
    vka_minimap_present();

    g_f = 14; run_obj_frame(fb_ortho, q_bar, q_txt, q_ic, 0, q_sub, q_subt);
    vka_mm_rect r3 = g_vka_mm.hit_obj;
    assert(r1.x == r3.x && r1.y == r3.y && r1.w == r3.w && r1.h == r3.h);

    /* A frame's HUD is recorded over several render passes: each pass end must stage the stream again,
       so the copy that is read back is the one taken after the frame's last HUD draw. */
    g_f++; vka_minimap_present();
    for (int i = 0; i < 4 && (atomic_load(&g_vka_mm.obj.pending_valid) || (g_vka_mm.frame_counter & 1u)); i++) { g_f++; vka_minimap_present(); }
    assert(!atomic_load(&g_vka_mm.obj.pending_valid) && !(g_vka_mm.frame_counter & 1u));
    g_f += 2;
    int c0 = g_ncib;
    run_obj_frame(fb_ortho, q_bar, q_txt, q_ic, 1, q_sub, q_subt);
    int c1 = g_ncib;
    run_obj_frame(fb_ortho, q_bar, q_txt, q_ic, 1, q_sub, q_subt);   /* a second HUD pass of the same frame */
    int c2 = g_ncib;
    assert(c1 - c0 >= 1);
    assert(c2 - c1 == c1 - c0);
}

static void test_speech_popup_stays(void) {
    float fb_ortho[64]; memset(fb_ortho, 0, sizeof fb_ortho);
    fb_ortho[8 + 0] = 2.f / 1920.f; fb_ortho[8 + 5] = -2.f / 1080.f; fb_ortho[8 + 10] = 1.f;
    fb_ortho[8 + 12] = -1.f; fb_ortho[8 + 13] = 1.f; fb_ortho[8 + 15] = 1.f;
    init_id_m34(&fb_ortho[24]);
    vtx_idx_pos q_panel[6], q_face[6], q_heart[6];
    make_quad_vtx(q_panel, 0.f, 902.f, 980.f, 1062.f, 0.f, 0.f);    /* the pop-up's backing, measured on AoI */
    make_quad_vtx(q_face, 6.f, 842.f, 206.f, 1062.f, 0.f, 0.f);     /* its portrait */
    make_quad_vtx(q_heart, 50.f, 25.f, 94.f, 69.f, 0.f, 0.f);       /* a heart: left HUD */
    vka_img im = { .img = 55ull, .serial = 4ull, .w = 1920u, .h = 1080u, .layers = 1u, .samples = 1u };
    VkRenderingAttachmentInfo sp = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 30ull, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u };
    vka_dstate ds_panel = make_dstate((void *)q_panel, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_face = make_dstate((void *)q_face, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_heart = make_dstate((void *)q_heart, (void *)fb_ortho, 256u, NULL, 0u);
    for (int with_panel = 0; with_panel < 2; with_panel++) {
        g_f += 2;
        vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
            (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
        vka_minimap_select_target(1ull, &ds_heart, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
        assert(g_vka_mm.cur_hud == 1);
        if (with_panel) {
            vka_minimap_select_target(1ull, &ds_panel, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
            assert(g_vka_mm.cur_hud == 0);
        }
        vka_minimap_select_target(1ull, &ds_face, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
        assert(g_vka_mm.cur_hud == (with_panel ? 0 : 1));
        vka_minimap_select_target(1ull, &ds_heart, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
        assert(g_vka_mm.cur_hud == 1);
        vka_minimap_pass_end(1ull, &im);
        vka_minimap_present();
    }
}

static void test_menu_dim_stops_capture(void) {
    float fb_ortho[64]; memset(fb_ortho, 0, sizeof fb_ortho);
    fb_ortho[8 + 0] = 2.f / 1920.f; fb_ortho[8 + 5] = -2.f / 1080.f; fb_ortho[8 + 10] = 1.f;
    fb_ortho[8 + 12] = -1.f; fb_ortho[8 + 13] = 1.f; fb_ortho[8 + 15] = 1.f;
    init_id_m34(&fb_ortho[24]);
    vtx_idx_pos q_dim[6], q_heart[6];
    make_quad_vtx(q_dim, 0.f, 0.f, 1920.f, 1080.f, 0.f, 0.f);
    make_quad_vtx(q_heart, 50.f, 25.f, 94.f, 69.f, 0.f, 0.f);
    vka_img im = { .img = 55ull, .serial = 4ull, .w = 1920u, .h = 1080u, .layers = 1u, .samples = 1u };
    VkRenderingAttachmentInfo sp = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 30ull, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u };
    vka_dstate ds_dim = make_dstate((void *)q_dim, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_heart = make_dstate((void *)q_heart, (void *)fb_ortho, 256u, NULL, 0u);
    VkRect2D area = {{0, 0}, {1920u, 1080u}};
    g_f += 2;
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM, area, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
    vka_minimap_select_target(1ull, &ds_heart, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1);                      /* the HUD frame before the menu opens */
    vka_minimap_pass_end(1ull, &im);
    vka_minimap_present();
    g_f += 2;
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM, area, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
    vka_minimap_select_target(1ull, &ds_dim, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    vka_minimap_select_target(1ull, &ds_heart, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0);                      /* after the dim layer: stays on the main target */
    vka_minimap_pass_end(1ull, &im);
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM, area, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
    vka_minimap_select_target(1ull, &ds_heart, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0);                      /* and in a later pass of the same frame */
    vka_minimap_pass_end(1ull, &im);
    vka_minimap_present();
    g_f += 2;
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM, area, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
    vka_minimap_select_target(1ull, &ds_heart, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1);                      /* menu closed: captured again */
    vka_minimap_pass_end(1ull, &im);
    vka_minimap_present();
}

static void test_minimap_group_closes(void) {
    float fb_ortho[64]; memset(fb_ortho, 0, sizeof fb_ortho);
    fb_ortho[8 + 0] = 2.f / 1920.f; fb_ortho[8 + 5] = -2.f / 1080.f; fb_ortho[8 + 10] = 1.f;
    fb_ortho[8 + 12] = -1.f; fb_ortho[8 + 13] = 1.f; fb_ortho[8 + 15] = 1.f;
    init_id_m34(&fb_ortho[24]);
    vtx_idx_pos q_mask[6], q_icon[6], q_else[6], q_bubble[6], q_layer[6];
    make_quad_vtx(q_layer, 686.f, -156.f, 2734.f, 1892.f, 0.f, 0.f);   /* a map layer wider than the screen, centred on the minimap (measured) */
    make_quad_vtx(q_mask, 1540.f, 698.f, 1880.f, 1038.f, 0.f, 0.f);    /* the minimap's stencil mask, measured on AoI */
    make_quad_vtx(q_icon, 1707.f, 741.f, 1779.f, 813.f, 0.f, 0.f);     /* its player marker */
    make_quad_vtx(q_else, 900.f, 500.f, 1000.f, 560.f, 0.f, 0.f);      /* some other widget, mid screen */
    make_quad_vtx(q_bubble, 1600.f, 760.f, 1680.f, 820.f, 0.f, 0.f);   /* a bubble over a character standing in that corner */
    vka_img im = { .img = 55ull, .serial = 4ull, .w = 1920u, .h = 1080u, .layers = 1u, .samples = 1u };
    VkRenderingAttachmentInfo sp = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 30ull, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u };
    vka_dstate ds_mask = make_dstate((void *)q_mask, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_icon = make_dstate((void *)q_icon, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_else = make_dstate((void *)q_else, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_bubble = make_dstate((void *)q_bubble, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_layer = make_dstate((void *)q_layer, (void *)fb_ortho, 256u, NULL, 0u);
    g_f += 2;
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
    vka_minimap_select_target(1ull, &ds_mask, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 1u, 0u, 0x0u, 1u);
    assert(g_vka_mm.pass_mm_hit == 1);
    vka_minimap_select_target(1ull, &ds_icon, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1);                      /* inside the box while the group is open */
    vka_minimap_select_target(1ull, &ds_layer, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1);                      /* the wide map layer is the minimap's and does not close its group */
    vka_minimap_select_target(1ull, &ds_icon, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 1);                      /* so the markers drawn after it are still captured */
    vka_minimap_select_target(1ull, &ds_else, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0);
    vka_minimap_select_target(1ull, &ds_bubble, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0);                      /* inside the box after the group closed: stays on Display 0 */
    vka_minimap_pass_end(1ull, &im);
    vka_minimap_present();
}

static void test_world_marker_stays(void) {   /* runs right after test_pass_462636: the bar's icon column is learned */
    float fb_ortho[64]; memset(fb_ortho, 0, sizeof fb_ortho);
    fb_ortho[8 + 0] = 2.f / 1920.f; fb_ortho[8 + 5] = -2.f / 1080.f; fb_ortho[8 + 10] = 1.f;
    fb_ortho[8 + 12] = -1.f; fb_ortho[8 + 13] = 1.f; fb_ortho[8 + 15] = 1.f;
    init_id_m34(&fb_ortho[24]);
    vtx_idx_pos q_marker[6], q_ring[12];
    make_quad_vtx(q_marker, 951.f, 478.f, 1020.f, 547.f, 0.f, 0.f);    /* the marker over the 3D world, on the bar's row (measured) */
    make_quad_vtx(&q_ring[0], 930.f, 457.f, 1041.f, 568.f, 0.f, 0.f);  /* its pulsing ring */
    make_quad_vtx(&q_ring[6], 930.f, 457.f, 1041.f, 568.f, 0.f, 0.f);
    vka_img im = { .img = 55ull, .serial = 4ull, .w = 1920u, .h = 1080u, .layers = 1u, .samples = 1u };
    VkRenderingAttachmentInfo sp = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, 30ull, VK_IMAGE_LAYOUT_GENERAL, 0u, 0u };
    vka_dstate ds_marker = make_dstate((void *)q_marker, (void *)fb_ortho, 256u, NULL, 0u);
    vka_dstate ds_ring = make_dstate((void *)q_ring, (void *)fb_ortho, 256u, NULL, 0u);
    vka_minimap_present();
    assert(g_vka_mm.obj_ttl > 0u);
    g_f += 2;
    vka_minimap_pass_begin(1ull, &im, 1u, 1920u, 1080u, VK_FORMAT_R8G8B8A8_UNORM,
        (VkRect2D){{0, 0}, {1920u, 1080u}}, 10ull, VK_IMAGE_LAYOUT_GENERAL, NULL, &sp, 1, 0);
    for (int i = 0; i < 3; i++) {
        vka_minimap_select_target(1ull, &ds_marker, 3u, &(p_draw){0, 0, 6, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
        assert(g_vka_mm.cur_hud == 0 && g_vka_mm.pass_obj_hit == 0);
    }
    vka_minimap_select_target(1ull, &ds_ring, 3u, &(p_draw){0, 0, 12, 1, 0, 0}, NULL, 0, 0u, 0u, 0u, 0xfu, 1u);
    assert(g_vka_mm.cur_hud == 0 && g_vka_mm.pass_obj_hit == 0);
    vka_minimap_pass_end(1ull, &im);
    vka_minimap_present();
}

int main(void) {
    test_proj_decode();
    test_pass_462636();
    test_world_marker_stays();
    test_speech_popup_stays();
    test_menu_dim_stops_capture();
    test_minimap_group_closes();
    puts("vka_minimap_c_regression: PASS");
    return 0;
}
'''


class WarriorsCompanionTests(unittest.TestCase):
    """Compile and execute pure-JDK Warriors companion tests without the Android SDK."""

    def test_pure_jvm_sources_have_no_android_imports(self):
        self.assertGreater(len(SRCS), 0)
        for p in SRCS:
            self.assertNotIn('import android.', p.read_text(encoding='utf-8'), p.name)

    def test_draw_source_has_no_status_word_cutscene_or_boxes(self):
        draw_src = (JAVA / 'WarriorsCompanionDraw.java').read_text(encoding='utf-8')
        mm_src = (REPO / 'dm/gfx/vk_android/vka_minimap.h').read_text(encoding='utf-8')
        self.assertNotIn('"STATUS"', draw_src)
        self.assertNotIn('CUTSCENE', draw_src)
        self.assertNotIn('drawRoundRect', draw_src)
        self.assertNotIn('0xF205080A', draw_src)
        self.assertNotIn('drawWallFrieze', draw_src)   # no wall text, side border or scanline overlay on the left and right
        self.assertNotIn('drawFilter', draw_src)
        self.assertNotIn('sidePaint', draw_src)
        self.assertIn('vka_mm_ubo_xy_bounds', mm_src)
        draw_c = (REPO / 'dm/gfx/vk_android/vka_draw.c').read_text(encoding='utf-8')
        self.assertIn('vka_mm_find_proj', mm_src)
        for forbidden in ('save_img', 'pass_preclear', 'restore_box', 'mm_skip', 'skip_out'):
            self.assertNotIn(forbidden, mm_src)
            self.assertNotIn(forbidden, draw_c)

    @unittest.skipUnless(shutil.which('cc'), 'no C compiler')
    def test_vka_minimap_c_regression(self):
        inc = str(REPO / 'dm/gfx/vk_android')
        with tempfile.TemporaryDirectory(prefix='mm_c_') as tmp:
            src = Path(tmp) / 'mm_test.c'
            exe = Path(tmp) / 'mm_test'
            src.write_text(MM_C_HARNESS, encoding='utf-8')
            c = subprocess.run(
                ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                 '-I' + inc, str(src), '-lm', '-lpthread', '-o', str(exe)],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run([str(exe)], capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn('vka_minimap_c_regression: PASS', r.stdout)

    @unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'no JDK')
    def test_warriors_companion_jvm_suite(self):
        with tempfile.TemporaryDirectory(prefix='wc_jvm_') as tmp:
            root = Path(tmp)
            classes = root / 'classes'
            classes.mkdir()
            bc = root / 'BuildConfig.java'
            bc.write_text(STUB_BUILD_CONFIG, encoding='utf-8')
            c = subprocess.run(
                ['javac', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', str(classes),
                 *map(str, COMPILE_SRCS), str(bc), str(TEST)],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run(
                ['java', '-Dstdout.encoding=UTF-8', '-cp', str(classes), 'dev.nxnative.android.WarriorsCompanionJvmTest'],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn('WarriorsCompanionJvmTest: PASS', r.stdout)

    @unittest.skipUnless(shutil.which('javac') and shutil.which('java'), 'no JDK')
    def test_warriors_companion_layout_and_motion(self):
        # Shared verbatim with the Age of Calamity port: rects in every state apart and inside the frame,
        # frame-time easing, retargeting, size hold and HUD/minimap sequencing.
        with tempfile.TemporaryDirectory(prefix='wc_layout_') as tmp:
            c = subprocess.run(
                ['javac', '-Xlint:all', '-Werror', '-encoding', 'UTF-8', '-d', tmp,
                 str(JAVA / 'WarriorsCompanionLayout.java'), str(LAYOUT_TEST)],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(c.returncode, 0, c.stderr)
            r = subprocess.run(
                ['java', '-cp', tmp, 'dev.nxnative.android.WarriorsCompanionLayoutTest'],
                capture_output=True, text=True, encoding='utf-8',
            )
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertIn('WarriorsCompanionLayoutTest: PASS', r.stdout)

    def test_draw_and_view_use_the_shared_layout(self):
        draw_src = (JAVA / 'WarriorsCompanionDraw.java').read_text(encoding='utf-8')
        view_src = (JAVA / 'WarriorsCompanionView.java').read_text(encoding='utf-8')
        self.assertIn('scene.step(', draw_src)
        self.assertNotIn('816f', draw_src)
        self.assertIn('if (moving) postInvalidateOnAnimation();', view_src)
        self.assertIn('WarriorsCompanionLayout.statusRowAt(', view_src)
        self.assertIn('WarriorsCompanionLayout.headerTabAt(', view_src)
        self.assertIn('scanLeftHud(', draw_src)
        self.assertIn('c.drawBitmap(b, srcRect, dstF, imgPaint);', draw_src)


if __name__ == '__main__':
    unittest.main()
