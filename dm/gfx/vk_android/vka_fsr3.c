// FSR 3.1 temporal upscaling in place of an NVN2 game's DLSS evaluation (Fire Emblem Fortune's Weave). The game renders its scene with a sub-pixel camera jitter each
// frame and hands DLSS the jittered colour, depth, motion vectors and the jitter; a spatial scaler (the linear copy, FSR 1) keeps the jitter, so the whole scene shakes by a
// fraction of a pixel every frame. FSR 3.1 takes the same inputs as DLSS and accumulates the frames along the motion vectors, which removes the jitter and the aliasing.
// The passes, internal images and constants follow AMD's ffx_fsr3upscaler.cpp (FidelityFX SDK 1.1.4, FSR 3.1.4); the shaders (fsr3/*.h, dm/third_party/fidelityfx-fsr3, MIT) are
// the SDK's GLSL passes compiled for reversed depth, HDR colour, render-size motion vectors, no half precision, the SPD passes with and without wave operations. Rebuilt and checked by dm/tools/fsr3_shaders.py.
// Here each pass is a compute pipeline with push descriptors (set 0: its bindings in the SDK's order, then the point and linear samplers at 1000 and 1001), recorded into the
// replay's command buffer where the game's DLSS evaluation is. Every image stays in GENERAL. DM_VK_DLSS=4 picks it (the Upscaler row's FSR 3.1) (the caller then uses FSR 1 or the linear copy)
#if defined(__ANDROID__)
#include "vka.h"
#include "vka_fg_fast.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fsr3/fsr3_prepare_inputs.h"
#include "fsr3/fsr3_luma_pyramid_nowave.h"
#include "fsr3/fsr3_luma_pyramid.h"
#include "fsr3/fsr3_shading_change_pyramid.h"
#include "fsr3/fsr3_shading_change_pyramid_nowave.h"
#include "fsr3/fsr3_shading_change.h"
#include "fsr3/fsr3_prepare_reactivity.h"
#include "fsr3/fsr3_luma_instability.h"
#include "fsr3/fsr3_accumulate.h"
#include "fsr3/fsr3_accumulate_sharpen.h"
#include "fsr3/fsr3_rcas.h"

enum { P_PREP, P_LUMA, P_SCP, P_SC, P_REACT, P_INSTAB, P_ACC, P_ACCS, P_RCAS, P_N };
// Each pass's bindings in order: T a sampled image, I a storage image, U a uniform buffer (FSR3UPSCALER_BIND_* in the pass files)
static const char *const g_kinds[P_N] = {"TTTIIIIIU", "TTIIIIIIIIIUU", "TTTTIIIIIIIUU", "TIU", "TTTTTTTTTIIIU", "TTTTTTTIIU", "TTTTTTTTTIIIU", "TTTTTTTTTIIIU", "TTIUU"};
static struct { const uint32_t *code; size_t size; } g_code[P_N] = {
    {FSR3_UPSCALER_PREPARE_INPUTS_COMP_SPV, sizeof FSR3_UPSCALER_PREPARE_INPUTS_COMP_SPV},
    {FSR3_UPSCALER_LUMA_PYRAMID_NOWAVE_COMP_SPV, sizeof FSR3_UPSCALER_LUMA_PYRAMID_NOWAVE_COMP_SPV},
    {FSR3_UPSCALER_SHADING_CHANGE_PYRAMID_NOWAVE_COMP_SPV, sizeof FSR3_UPSCALER_SHADING_CHANGE_PYRAMID_NOWAVE_COMP_SPV},
    {FSR3_UPSCALER_SHADING_CHANGE_COMP_SPV, sizeof FSR3_UPSCALER_SHADING_CHANGE_COMP_SPV},
    {FSR3_UPSCALER_PREPARE_REACTIVITY_COMP_SPV, sizeof FSR3_UPSCALER_PREPARE_REACTIVITY_COMP_SPV},
    {FSR3_UPSCALER_LUMA_INSTABILITY_COMP_SPV, sizeof FSR3_UPSCALER_LUMA_INSTABILITY_COMP_SPV},
    {FSR3_UPSCALER_ACCUMULATE_COMP_SPV, sizeof FSR3_UPSCALER_ACCUMULATE_COMP_SPV},
    {FSR3_UPSCALER_ACCUMULATE_SHARPEN_COMP_SPV, sizeof FSR3_UPSCALER_ACCUMULATE_SHARPEN_COMP_SPV},
    {FSR3_UPSCALER_RCAS_COMP_SPV, sizeof FSR3_UPSCALER_RCAS_COMP_SPV},
};

// cbFSR3Upscaler (std140, 148 bytes), cbSPD and cbRCAS
typedef struct {
    int32_t render_size[2], previous_render_size[2], upscale_size[2], previous_upscale_size[2], max_render_size[2], max_upscale_size[2];
    float device_to_view_depth[4], jitter[2], previous_jitter[2], motion_vector_scale[2], downscale_factor[2], motion_vector_jitter_cancellation[2];
    float tan_half_fov, jitter_phase_count, delta_time, delta_pre_exposure, view_space_to_meters, frame_index, velocity_factor, reactiveness_scale,
          shading_change_scale, accumulation_added_per_frame, min_disocclusion_accumulation;
} f3_consts;
_Static_assert(sizeof(f3_consts) == 148, "cbFSR3Upscaler");
typedef struct { uint32_t mips, num_work_groups, work_group_offset[2], render_size[2]; } f3_spd;

#define F3_SPD_STORED_MIPS 6
#define F3_SLOT 256u                      // one constant block per slot (Adreno's offset alignment is 64)
#define F3_FRAME (3u * F3_SLOT)           // FSR, SPD, RCAS constants of one evaluation
#define F3_RING 16u                       // evaluations in flight at most
#define F3_LUT_OFF (F3_RING * F3_FRAME)   // the Lanczos table's upload, after the ring

typedef struct { VkImage img; VkDeviceMemory mem; VkImageView view, lv[16]; uint32_t levels; } f3_tex;
typedef struct {
    f3_tex acc[2], luma[2], luma_hist[2], up[2];
    f3_tex farthest, shading, locks, spd, farthest_mip1, dmv, ddepth, prev_depth, dreact, info, spd_atomic, lut, def_react, out;
    uint32_t rw, rh, uw, uh; VkFormat out_fmt;
} f3_res;

static struct {
    int state;   // 0 not tried, 1 ready, -1 failed
    VkSampler point, linear;
    VkDescriptorSetLayout dsl[P_N]; VkPipelineLayout lay[P_N]; VkPipeline pipe[P_N];
    VkBuffer ubo; VkDeviceMemory ubo_mem; uint8_t *ubo_map; uint32_t ring;
    f3_res *res;
    int first, parity;
    int32_t prev_rs[2], prev_us[2]; float prev_jitter[2], phase_count, frame_index;
    struct timespec last;
} F;

static vka_fmt_feat_cache g_f3_fmt_cache;
static vka_fsr3_pass_meta g_f3_meta[P_N];
static vka_fsr3_fast_memo g_f3_memo;
static VkFormatFeatureFlags f3_fmt_features(VkFormat fmt) {
    uint32_t cached = 0;
    if (vka_fg_fast_on() && vka_fmt_feat_get(&g_f3_fmt_cache, (uintptr_t)V.pd, (uint32_t)fmt, &cached))
        return (VkFormatFeatureFlags)cached;
    VkFormatProperties fp = {0};
    vk.vkGetPhysicalDeviceFormatProperties(V.pd, fmt, &fp);
    if (vka_fg_fast_on()) vka_fmt_feat_put(&g_f3_fmt_cache, (uintptr_t)V.pd, (uint32_t)fmt, (uint32_t)fp.optimalTilingFeatures);
    return fp.optimalTilingFeatures;
}
static void f3_memo_env_sync(void) {
    uint32_t g = atomic_load_explicit(&vka_env_gen, memory_order_relaxed);
    if (g_f3_memo.env_valid && g_f3_memo.env_gen == g) return;
    g_f3_memo.env_gen = g; g_f3_memo.env_valid = 1;
    g_f3_memo.out_w = VKA_ENV("DM_VK_FSR3_OUT_W", 0); g_f3_memo.out_h = VKA_ENV("DM_VK_FSR3_OUT_H", 0);
    g_f3_memo.fsr3_reset = VKA_ENV("DM_VK_FSR3_RESET", 0);
    g_f3_memo.jx = (float)VKA_ENVF("DM_VK_FSR3_JX", 1); g_f3_memo.jy = (float)VKA_ENVF("DM_VK_FSR3_JY", 1);
    g_f3_memo.mx = (float)VKA_ENVF("DM_VK_FSR3_MX", 1); g_f3_memo.my = (float)VKA_ENVF("DM_VK_FSR3_MY", 1);
    g_f3_memo.sharp = (float)VKA_ENVF("DM_VK_FSR3_SHARP", 0.5);
}
static void tex_free(f3_tex *t) {
    for (uint32_t i = 0; i < t->levels && t->levels > 1; i++) if (t->lv[i]) vk.vkDestroyImageView(V.dev, t->lv[i], NULL);
    if (t->view) vk.vkDestroyImageView(V.dev, t->view, NULL);
    if (t->img) vk.vkDestroyImage(V.dev, t->img, NULL);
    if (t->mem) vk.vkFreeMemory(V.dev, t->mem, NULL);
    memset(t, 0, sizeof *t);
}
static void res_free(void *p) {
    f3_res *r = p;
    f3_tex *all = (f3_tex *)r; size_t n = offsetof(f3_res, rw) / sizeof(f3_tex);
    for (size_t i = 0; i < n; i++) tex_free(&all[i]);
    free(r);
}
static VkImageView mkview(VkImage img, VkFormat fmt, uint32_t base, uint32_t n) {
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, base, n, 0, 1}};
    VkImageView v = VK_NULL_HANDLE;
    return vk.vkCreateImageView(V.dev, &vi, NULL, &v) ? VK_NULL_HANDLE : v;
}
static int tex_make(f3_tex *t, VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, VkImageUsageFlags extra) {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | extra | (fmt == VK_FORMAT_R16_SNORM ? 0 : VK_IMAGE_USAGE_STORAGE_BIT);
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt, .extent = {w ? w : 1, h ? h : 1, 1}, .mipLevels = levels,
                            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage};
    if (vk.vkCreateImage(V.dev, &ci, NULL, &t->img)) return 0;
    VkMemoryRequirements mr; vk.vkGetImageMemoryRequirements(V.dev, t->img, &mr);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0)};
    if (ai.memoryTypeIndex == UINT32_MAX || vk.vkAllocateMemory(V.dev, &ai, NULL, &t->mem) || vk.vkBindImageMemory(V.dev, t->img, t->mem, 0)) return 0;
    t->levels = levels;
    if (!(t->view = mkview(t->img, fmt, 0, levels))) return 0;
    if (levels > 1) for (uint32_t i = 0; i < levels && i < 16; i++) if (!(t->lv[i] = mkview(t->img, fmt, i, 1))) return 0;
    return 1;
}

// What the passes need from the device, checked before anything is made (the caller then uses FSR 1): push descriptors (every pass binds that way), the storage image features
// the SPIR-V declares (StorageImageExtendedFormats, StorageImageWriteWithoutFormat; vka_dev.c enables every feature the device has, so supported means enabled), and each
// internal format as a sampled + storage image (R32_UINT also with atomics: the pyramids' counter and the previous depth)
static const VkFormat g_storage_fmts[] = {VK_FORMAT_R8_UNORM, VK_FORMAT_R16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16_SFLOAT,
                                          VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32_UINT, VK_FORMAT_R32G32B32A32_SFLOAT};
static const char *f3_missing(void) {
    if (!V.push_desc || !vk.vkCmdPushDescriptorSetKHR) return "push descriptors (VK_KHR_push_descriptor)";
    if (V.max_push < 15) return "15 push descriptors per set";   // the largest pass: 13 bindings and the 2 samplers
    VkPhysicalDeviceFeatures pf; vk.vkGetPhysicalDeviceFeatures(V.pd, &pf);
    if (!pf.shaderStorageImageExtendedFormats) return "shaderStorageImageExtendedFormats";
    if (!pf.shaderStorageImageWriteWithoutFormat) return "shaderStorageImageWriteWithoutFormat";
    for (size_t i = 0; i < sizeof g_storage_fmts / sizeof *g_storage_fmts; i++) {
        VkFormatProperties fp; vk.vkGetPhysicalDeviceFormatProperties(V.pd, g_storage_fmts[i], &fp);
        VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | (g_storage_fmts[i] == VK_FORMAT_R32_UINT ? VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT : 0);
        if ((fp.optimalTilingFeatures & need) != need) { static char b[64]; snprintf(b, sizeof b, "storage format %d", g_storage_fmts[i]); return b; }
    }
    VkFormatProperties fp; vk.vkGetPhysicalDeviceFormatProperties(V.pd, VK_FORMAT_R16_SNORM, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) return "sampled R16_SNORM";
    return NULL;
}
static void f3_teardown(void) {   // a setup that failed part way: what was made goes, F.state stays -1
    for (int p = 0; p < P_N; p++) {
        if (F.pipe[p]) vk.vkDestroyPipeline(V.dev, F.pipe[p], NULL);
        if (F.lay[p]) vk.vkDestroyPipelineLayout(V.dev, F.lay[p], NULL);
        if (F.dsl[p]) vk.vkDestroyDescriptorSetLayout(V.dev, F.dsl[p], NULL);
        F.pipe[p] = VK_NULL_HANDLE; F.lay[p] = VK_NULL_HANDLE; F.dsl[p] = VK_NULL_HANDLE;
    }
    if (F.ubo_map) vk.vkUnmapMemory(V.dev, F.ubo_mem);
    if (F.ubo) vk.vkDestroyBuffer(V.dev, F.ubo, NULL);
    if (F.ubo_mem) vk.vkFreeMemory(V.dev, F.ubo_mem, NULL);
    if (F.point) vk.vkDestroySampler(V.dev, F.point, NULL);
    if (F.linear) vk.vkDestroySampler(V.dev, F.linear, NULL);
    F.ubo_map = NULL; F.ubo = VK_NULL_HANDLE; F.ubo_mem = VK_NULL_HANDLE; F.point = F.linear = VK_NULL_HANDLE;
}

static int f3_ready(void) {
    if (F.state) return F.state > 0;
    F.state = -1;
    const char *miss = f3_missing();
    if (miss) { VRED("FSR 3.1: the device lacks %s; the scene is scaled with FSR 1", miss); return 0; }
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxAnisotropy = 1, .maxLod = VK_LOD_CLAMP_NONE};
    VkResult r = vk.vkCreateSampler(V.dev, &si, NULL, &F.point);
    si.magFilter = si.minFilter = VK_FILTER_LINEAR; si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (!r) r = vk.vkCreateSampler(V.dev, &si, NULL, &F.linear);
    VkSampler samplers[2] = {F.point, F.linear};
    // The SPD pyramids reduce with quad shuffles on Turnip, where they were tested, when compute reports them; the wave-free build everywhere else (Qualcomm's driver among them),
    // as the SDK does without wave support. DM_VK_FSR3_WAVE=1: quad shuffles on any driver that reports them; 0: wave-free on Turnip too
    VkPhysicalDeviceSubgroupProperties sg = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 pp = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &sg};
    if (vk.vkGetPhysicalDeviceProperties2) vk.vkGetPhysicalDeviceProperties2(V.pd, &pp);
    int quads = (sg.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT) && (sg.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                VKA_ENV("DM_VK_FSR3_WAVE", V.driver_id == VK_DRIVER_ID_MESA_TURNIP);
    if (quads) {
        g_code[P_LUMA].code = FSR3_UPSCALER_LUMA_PYRAMID_COMP_SPV; g_code[P_LUMA].size = sizeof FSR3_UPSCALER_LUMA_PYRAMID_COMP_SPV;
        g_code[P_SCP].code = FSR3_UPSCALER_SHADING_CHANGE_PYRAMID_COMP_SPV; g_code[P_SCP].size = sizeof FSR3_UPSCALER_SHADING_CHANGE_PYRAMID_COMP_SPV;
    }
    for (int p = 0; p < P_N && !r; p++) {
        VkDescriptorSetLayoutBinding b[16]; uint32_t n = 0;
        for (const char *k = g_kinds[p]; *k; k++, n++)
            b[n] = (VkDescriptorSetLayoutBinding){n, *k == 'T' ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : *k == 'I' ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                  1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
        for (int s = 0; s < 2; s++, n++) b[n] = (VkDescriptorSetLayoutBinding){1000 + s, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &samplers[s]};
        VkDescriptorSetLayoutCreateInfo dci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR, .bindingCount = n, .pBindings = b};
        r = vk.vkCreateDescriptorSetLayout(V.dev, &dci, NULL, &F.dsl[p]);
        VkPipelineLayoutCreateInfo li = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &F.dsl[p]};
        if (!r) r = vk.vkCreatePipelineLayout(V.dev, &li, NULL, &F.lay[p]);
        VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = g_code[p].size, .pCode = g_code[p].code};
        VkShaderModule m = VK_NULL_HANDLE;
        if (!r) r = vk.vkCreateShaderModule(V.dev, &mi, NULL, &m);
        VkComputePipelineCreateInfo ci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = m, .pName = "main"}, .layout = F.lay[p]};
        if (!r) r = vk.vkCreateComputePipelines(V.dev, VK_NULL_HANDLE, 1, &ci, NULL, &F.pipe[p]);
        if (m) vk.vkDestroyShaderModule(V.dev, m, NULL);
        if (r) VRED("FSR 3.1: pass %d: %d", p, r);
    }
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = F3_LUT_OFF + 256, .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    if (!r) r = vk.vkCreateBuffer(V.dev, &bi, NULL, &F.ubo);
    VkMemoryRequirements mr = {0}; if (!r) vk.vkGetBufferMemoryRequirements(V.dev, F.ubo, &mr);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
        .memoryTypeIndex = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0)};
    if (!r && ai.memoryTypeIndex == UINT32_MAX) r = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (!r) r = vk.vkAllocateMemory(V.dev, &ai, NULL, &F.ubo_mem);
    if (!r) r = vk.vkBindBufferMemory(V.dev, F.ubo, F.ubo_mem, 0);
    if (!r) r = vk.vkMapMemory(V.dev, F.ubo_mem, 0, VK_WHOLE_SIZE, 0, (void **)&F.ubo_map);
    if (r) { VRED("FSR 3.1: setup %d; the scene is scaled with FSR 1", r); f3_teardown(); return 0; }
    for (int i = 0; i < P_N; i++) vka_fsr3_compile_kinds(&g_f3_meta[i], i == P_RCAS, g_kinds[i]);
    F.state = 1;
    VLOG("FSR 3.1 upscaler ready (DLSS replacement; pyramids %s; DM_VK_DLSS=4, DM_VK_FSR3_SHARP sharpness 0-1)", quads ? "with quad shuffles" : "wave-free");
    return 1;
}

static f3_res *res_make(uint32_t rw, uint32_t rh, uint32_t uw, uint32_t uh, VkFormat out_fmt, int need_out) {
    f3_res *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    int ok = 1;
    for (int i = 0; i < 2 && ok; i++)
        ok = tex_make(&r->acc[i], VK_FORMAT_R8_UNORM, rw, rh, 1, 0) && tex_make(&r->luma[i], VK_FORMAT_R16_SFLOAT, rw, rh, 1, 0) &&
             tex_make(&r->luma_hist[i], VK_FORMAT_R8G8B8A8_UNORM, rw, rh, 1, 0) && tex_make(&r->up[i], VK_FORMAT_R16G16B16A16_SFLOAT, uw, uh, 1, 0);
    uint32_t hw = rw / 2, hh = rh / 2, m = hw > hh ? hw : hh, levels = 1;
    while ((1u << levels) <= m) levels++;   // floor(log2(max)) + 1
    if (levels < F3_SPD_STORED_MIPS) levels = F3_SPD_STORED_MIPS;
    ok = ok && tex_make(&r->farthest, VK_FORMAT_R16_SFLOAT, rw, rh, 1, 0) && tex_make(&r->shading, VK_FORMAT_R8_UNORM, hw, hh, 1, 0) &&
         tex_make(&r->locks, VK_FORMAT_R8_UNORM, uw, uh, 1, 0) && tex_make(&r->spd, VK_FORMAT_R16G16_SFLOAT, hw, hh, levels, 0) &&
         tex_make(&r->farthest_mip1, VK_FORMAT_R16_SFLOAT, hw, hh, 1, 0) && tex_make(&r->dmv, VK_FORMAT_R16G16_SFLOAT, rw, rh, 1, 0) &&
         tex_make(&r->ddepth, VK_FORMAT_R32_SFLOAT, rw, rh, 1, 0) && tex_make(&r->prev_depth, VK_FORMAT_R32_UINT, rw, rh, 1, 0) &&
         tex_make(&r->dreact, VK_FORMAT_R8G8B8A8_UNORM, rw, rh, 1, 0) && tex_make(&r->info, VK_FORMAT_R32G32B32A32_SFLOAT, 1, 1, 1, 0) &&
         tex_make(&r->spd_atomic, VK_FORMAT_R32_UINT, 1, 1, 1, 0) && tex_make(&r->lut, VK_FORMAT_R16_SNORM, 128, 1, 1, 0) &&
         tex_make(&r->def_react, VK_FORMAT_R8_UNORM, 1, 1, 1, 0) &&
         (!need_out || tex_make(&r->out, out_fmt, uw, uh, 1, VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    if (!ok) { VRED("FSR 3.1: internal images %ux%u -> %ux%u could not be made", rw, rh, uw, uh); res_free(r); return NULL; }
    r->rw = rw; r->rh = rh; r->uw = uw; r->uh = uh; r->out_fmt = out_fmt;
    VLOG("FSR 3.1 upscaler: %ux%u -> %ux%u (%s output)", rw, rh, uw, uh, need_out ? "copied" : "direct");
    return r;
}

static void pass(VkCommandBuffer cb, int p, const VkImageView *views, uint32_t gx, uint32_t gy, uint32_t ubo_off) {
    VkWriteDescriptorSet w[16]; VkDescriptorImageInfo ii[16]; VkDescriptorBufferInfo bi[4]; uint32_t n = 0;
    if (vka_fg_fast_on()) {
        const vka_fsr3_pass_meta *m = &g_f3_meta[p]; n = m->n_total;
        for (uint32_t i = 0; i < m->n_ubo; i++) {
            uint32_t s = m->ubo_slot[i];
            bi[i] = (VkDescriptorBufferInfo){F.ubo, ubo_off + (uint32_t)m->ubo_which[i] * F3_SLOT, F3_SLOT};
            w[s] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = s, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bi[i]};
        }
        for (uint32_t i = 0; i < m->n_img; i++) {
            uint32_t s = m->img_slot[i], is_t = (m->img_is_tex_mask >> i) & 1u;
            ii[i] = (VkDescriptorImageInfo){VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_GENERAL};
            w[s] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = s, .descriptorCount = 1, .descriptorType = is_t ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &ii[i]};
        }
    } else {
        uint32_t nb = 0, ni = 0;
        for (const char *k = g_kinds[p]; *k; k++, n++) {
            w[n] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstBinding = n, .descriptorCount = 1};
            if (*k == 'U') {
                uint32_t which = nb == 0 ? 0 : (p == P_RCAS ? 2 : 1);
                bi[nb] = (VkDescriptorBufferInfo){F.ubo, ubo_off + which * F3_SLOT, F3_SLOT};
                w[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[n].pBufferInfo = &bi[nb++];
            } else {
                ii[ni] = (VkDescriptorImageInfo){VK_NULL_HANDLE, views[ni], VK_IMAGE_LAYOUT_GENERAL};
                w[n].descriptorType = *k == 'T' ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[n].pImageInfo = &ii[ni++];
            }
        }
    }
    vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, F.pipe[p]);
    vk.vkCmdPushDescriptorSetKHR(cb, VK_PIPELINE_BIND_POINT_COMPUTE, F.lay[p], 0, n, w);
    vk.vkCmdDispatch(cb, gx, gy, 1);
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
    if (vka_gp_active) vka_gp_mark(cb, "fsr3-pass", 1000 + p);
}
static uint32_t dc(uint32_t v, uint32_t t) { return (v + t - 1) / t; }
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint16_t f16(float f) { _Float16 h = (_Float16)f; uint16_t u; memcpy(&u, &h, 2); return u; }
static void clear(VkCommandBuffer cb, VkImage img, float v0, float v1) {
    VkClearColorValue c = {.float32 = {v0, v1, 0, 0}};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    vk.vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_GENERAL, &c, 1, &all);
}

static int fsr3_fill_consts(uint32_t rw, uint32_t rh, uint32_t uw, uint32_t uh, float dt_ms, int first, int reset, const p_dlss_t2t *o, uint32_t *off_out, int *sharpen_out) {
    f3_consts k = {0}; k.render_size[0] = (int32_t)rw; k.render_size[1] = (int32_t)rh; k.upscale_size[0] = (int32_t)uw; k.upscale_size[1] = (int32_t)uh;
    memcpy(k.previous_render_size, first ? k.render_size : F.prev_rs, 8); memcpy(k.previous_upscale_size, first ? k.upscale_size : F.prev_us, 8);
    memcpy(k.max_render_size, k.render_size, 8); memcpy(k.max_upscale_size, k.upscale_size, 8);
    int fast = vka_fg_fast_on();
    if (fast) { vka_fsr3_geom_sync(&g_f3_memo, rw, rh); memcpy(k.device_to_view_depth, g_f3_memo.dev_to_view, 16); k.tan_half_fov = g_f3_memo.tan_half_fov; }
    else {
        const float near_plane = 0.1f, fov = 60.f * 3.14159265f / 180.f, aspect = (float)rw / rh, cot = cosf(0.5f * fov) / sinf(0.5f * fov);
        k.device_to_view_depth[0] = -1.f * (0.f + FLT_EPSILON); k.device_to_view_depth[1] = near_plane;
        k.device_to_view_depth[2] = 1.f / (cot / aspect); k.device_to_view_depth[3] = 1.f / cot; k.tan_half_fov = tanf(atanf(tanf(fov * 0.5f) * aspect));
    }
    k.jitter[0] = o->jitter[0] * (fast ? g_f3_memo.jx : VKA_ENVF("DM_VK_FSR3_JX", 1));
    k.jitter[1] = o->jitter[1] * (fast ? g_f3_memo.jy : VKA_ENVF("DM_VK_FSR3_JY", 1));
    memcpy(k.previous_jitter, first ? k.jitter : F.prev_jitter, 8);
    k.motion_vector_scale[0] = o->mv[0] * (fast ? g_f3_memo.mx : VKA_ENVF("DM_VK_FSR3_MX", 1)) / rw;
    k.motion_vector_scale[1] = o->mv[1] * (fast ? g_f3_memo.my : VKA_ENVF("DM_VK_FSR3_MY", 1)) / rh;
    k.downscale_factor[0] = (float)rw / uw; k.downscale_factor[1] = (float)rh / uh;
    float ratio = (float)uw / rw, phases = (float)(int)(8.f * ratio * ratio);
    if (reset || F.phase_count == 0) F.phase_count = phases; else if (phases > F.phase_count) F.phase_count += 1; else if (phases < F.phase_count) F.phase_count -= 1;
    k.jitter_phase_count = F.phase_count; k.delta_time = dt_ms / 1000.f > 1 ? 1 : dt_ms < 0 ? 0 : dt_ms / 1000.f;
    k.delta_pre_exposure = 1; k.view_space_to_meters = 1; F.frame_index = reset ? 0 : F.frame_index + 1; k.frame_index = F.frame_index;
    k.velocity_factor = 1; k.reactiveness_scale = 1; k.shading_change_scale = 1; k.accumulation_added_per_frame = 1.f / 3; k.min_disocclusion_accumulation = -1.f / 3;
    memcpy(F.prev_rs, k.render_size, 8); memcpy(F.prev_us, k.upscale_size, 8); memcpy(F.prev_jitter, k.jitter, 8);
    uint32_t sgx = fast ? g_f3_memo.sgx : ((rw - 1) / 64 + 1), sgy = fast ? g_f3_memo.sgy : ((rh - 1) / 64 + 1), mips = 0;
    if (fast) mips = g_f3_memo.mips; else { uint32_t mx = rw > rh ? rw : rh; while ((2u << mips) <= mx) mips++; if (mips > 12) mips = 12; }
    f3_spd spd = {mips, sgx * sgy, {0, 0}, {rw, rh}};
    uint32_t rcas[4] = {0, 0, 0, 0};
    if (fast) { vka_fsr3_rcas_sync(&g_f3_memo, g_f3_memo.sharp); rcas[0] = g_f3_memo.rcas0; rcas[1] = g_f3_memo.rcas1; *sharpen_out = g_f3_memo.sharpen; }
    else {
        float sharp = VKA_ENVF("DM_VK_FSR3_SHARP", 0.5), stops = 2.f - 2.f * (sharp < 0 ? 0 : sharp > 1 ? 1 : sharp), sx = exp2f(-stops);
        *sharpen_out = sharp > 0; rcas[0] = fbits(sx); rcas[1] = (uint32_t)f16(sx) | (uint32_t)f16(sx) << 16;
    }
    uint32_t off = (F.ring++ % F3_RING) * F3_FRAME;
    memcpy(F.ubo_map + off, &k, sizeof k); memcpy(F.ubo_map + off + F3_SLOT, &spd, sizeof spd); memcpy(F.ubo_map + off + 2 * F3_SLOT, rcas, sizeof rcas);
    *off_out = off; return 1;
}
static void fsr3_prepare_images(VkCommandBuffer cb, f3_res *R, int first, int reset, int p) {
    vka_barrier(cb);
    if (first) {
        f3_tex *all = (f3_tex *)R; size_t n = offsetof(f3_res, rw) / sizeof(f3_tex);
        for (size_t i = 0; i < n; i++) if (all[i].img) vka_img_barrier(cb, all[i].img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        int16_t lut[128];
        for (int i = 0; i < 128; i++) {
            float x = 2.f * i / 127, px = 3.14159265f * x, y = fabsf(x) < FLT_EPSILON ? 1.f : (sinf(px) / px) * (sinf(0.5f * px) / (0.5f * px));
            lut[i] = (int16_t)lroundf(y * 32767.f);
        }
        memcpy(F.ubo_map + F3_LUT_OFF, lut, sizeof lut);
        VkBufferImageCopy bc = {.bufferOffset = F3_LUT_OFF, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {128, 1, 1}};
        vk.vkCmdCopyBufferToImage(cb, F.ubo, R->lut.img, VK_IMAGE_LAYOUT_GENERAL, 1, &bc);
        for (size_t i = 0; i < n; i++) if (all[i].img && &all[i] != &R->lut && &all[i] != &R->info) clear(cb, all[i].img, 0, 0);
    }
    clear(cb, R->prev_depth.img, 0, 0); clear(cb, R->spd_atomic.img, 0, 0); clear(cb, R->spd.img, 0, 0);
    if (reset) { clear(cb, R->acc[p].img, 0, 0); clear(cb, R->info.img, -1, 1); }
    vka_barrier(cb);
}
static void fsr3_dispatch_passes(VkCommandBuffer cb, f3_res *R, VkImageView io[4], VkImageView vout, uint32_t sz[4], int p, uint32_t off, int sharpen) {
    VkImageView exposure = R->info.view, cur_luma = R->luma[p].view; int q = p ^ 1;
    uint32_t rw = sz[0], rh = sz[1], uw = sz[2], uh = sz[3], sgx = (rw - 1) / 64 + 1, sgy = (rh - 1) / 64 + 1;
#define MIP(l) R->spd.lv[(l) < R->spd.levels ? (l) : R->spd.levels - 1]
    { VkImageView v[] = {io[2], io[1], io[0], R->dmv.view, R->ddepth.view, R->prev_depth.view, R->farthest.view, cur_luma}; pass(cb, P_PREP, v, dc(rw, 8), dc(rh, 8), off); }
    { VkImageView v[] = {cur_luma, R->farthest.view, R->spd_atomic.view, R->info.view, MIP(0), MIP(1), MIP(2), MIP(3), MIP(4), MIP(5), R->farthest_mip1.view}; pass(cb, P_LUMA, v, sgx, sgy, off); }
    { VkImageView v[] = {cur_luma, R->luma[q].view, R->dmv.view, exposure, R->spd_atomic.view, MIP(0), MIP(1), MIP(2), MIP(3), MIP(4), MIP(5)}; pass(cb, P_SCP, v, sgx, sgy, off); }
    { VkImageView v[] = {R->spd.view, R->shading.view}; pass(cb, P_SC, v, dc(rw / 2, 8), dc(rh / 2, 8), off); }
    { VkImageView v[] = {R->prev_depth.view, R->dmv.view, R->ddepth.view, R->def_react.view, R->def_react.view, R->acc[p].view, R->shading.view, cur_luma, exposure, R->dreact.view, R->locks.view, R->acc[q].view}; pass(cb, P_REACT, v, dc(rw, 8), dc(rh, 8), off); }
    { VkImageView v[] = {exposure, R->dreact.view, R->dmv.view, R->info.view, R->luma_hist[p].view, R->farthest_mip1.view, cur_luma, R->luma_hist[q].view, R->farthest.view}; pass(cb, P_INSTAB, v, dc(rw, 8), dc(rh, 8), off); }
    { VkImageView v[] = {exposure, R->dreact.view, R->dmv.view, R->up[p].view, R->lut.view, R->farthest_mip1.view, cur_luma, R->farthest.view, io[0], R->up[q].view, vout, R->locks.view}; pass(cb, sharpen ? P_ACCS : P_ACC, v, dc(uw, 8), dc(uh, 8), off); }
    if (sharpen) { VkImageView v[] = {exposure, R->up[q].view, vout}; pass(cb, P_RCAS, v, dc(uw, 16), dc(uh, 16), off); }
#undef MIP
}
static int fsr3_finish_output(VkCommandBuffer cb, f3_res *R, vka_img *dst, const NVNcopyRegion *d, uint32_t uw, uint32_t uh, uint32_t ow, uint32_t oh, int enc, int direct, int scaled) {
    if (enc) {
        vka_barrier(cb);
        if (!vka_dlss_scale(cb, R->out.view, 0, 0, (int)uw, (int)uh, uw, uh, dst, 0, d->xoffset, d->yoffset, (int)ow, (int)oh, 1)) { F.parity ^= 1; return 0; }
    } else if (!direct) {
        VkImageSubresourceLayers l = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; vka_barrier(cb);
        if (scaled) {
            VkImageBlit b = {l, {{0, 0, 0}, {(int32_t)uw, (int32_t)uh, 1}}, l, {{d->xoffset, d->yoffset, 0}, {d->xoffset + (int32_t)ow, d->yoffset + (int32_t)oh, 1}}};
            vk.vkCmdBlitImage(cb, R->out.img, VK_IMAGE_LAYOUT_GENERAL, dst->img, VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_LINEAR);
        } else {
            VkImageCopy region = {l, {0, 0, 0}, l, {d->xoffset, d->yoffset, 0}, {uw, uh, 1}};
            vk.vkCmdCopyImage(cb, R->out.img, VK_IMAGE_LAYOUT_GENERAL, dst->img, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        }
    }
    vka_barrier(cb); F.parity ^= 1; return 1;
}
int vka_dlss_fsr3(VkCommandBuffer cb, vka_img *src, VkFormat sfmt, vka_img *dst, int enc, vka_img *depth, vka_img *motion, const p_dlss_t2t *o) {
    const NVNcopyRegion *s = &o->c.sr, *d = &o->c.dr;
    if (!src || !dst || !depth || !motion || src == dst) { VRED_ONCE("FSR 3.1: the evaluation lacks %s", !depth ? "depth" : !motion ? "motion vectors" : "colour"); return 0; }
    uint32_t rw = (uint32_t)s->width, rh = (uint32_t)s->height, uw = (uint32_t)d->width, uh = (uint32_t)d->height, ow = uw, oh = uh;
    if (s->xoffset || s->yoffset || rw != src->w || rh != src->h || depth->w != rw || depth->h != rh || motion->w != rw || motion->h != rh ||
        src->aspect != VK_IMAGE_ASPECT_COLOR_BIT || dst->aspect != VK_IMAGE_ASPECT_COLOR_BIT || !(depth->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ||
        src->samples != 1 || dst->samples != 1 || depth->samples != 1 || src->base || dst->base || d->xoffset < 0 || d->yoffset < 0 ||
        uw < rw || uh < rh || (uint32_t)d->xoffset + uw > dst->w || (uint32_t)d->yoffset + uh > dst->h) return 0;
    if (!f3_ready()) return 0;
    if (vka_fg_fast_on()) f3_memo_env_sync();
    VkFormatFeatureFlags feats = f3_fmt_features(dst->f.vk);
    if (enc ? !vka_dlss_ready() : !(feats & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) { VRED_ONCE("FSR 3.1: output format %d is not a storage format (or no encoding copy for sRGB)", dst->f.vk); return 0; }
    const VkFormat out_fmt = enc ? VK_FORMAT_R16G16B16A16_SFLOAT : dst->f.vk;
    int fw = vka_fg_fast_on() ? g_f3_memo.out_w : VKA_ENV("DM_VK_FSR3_OUT_W", 0), fh = vka_fg_fast_on() ? g_f3_memo.out_h : VKA_ENV("DM_VK_FSR3_OUT_H", 0);
    const VkFormatFeatureFlags blit = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (fw >= (int)rw && fh >= (int)rh && (uint32_t)fw <= ow && (uint32_t)fh <= oh && (enc || (feats & blit) == blit)) { uw = (uint32_t)fw; uh = (uint32_t)fh; }
    const int scaled = uw != ow || uh != oh, direct = !enc && !scaled && (dst->usage & VK_IMAGE_USAGE_STORAGE_BIT) && !d->xoffset && !d->yoffset && uw == dst->w && uh == dst->h;
    if (!direct && !(dst->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return 0;
    VkImageView io[4] = {vka_view(src, sfmt ? sfmt : src->f.view ? src->f.view : src->f.vk, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, NULL),
        vka_view(depth, depth->f.vk, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1, NULL),
        vka_view(motion, motion->f.vk, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, NULL),
        direct ? vka_view(dst, dst->f.vk, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, NULL) : VK_NULL_HANDLE};
    if (!io[0] || !io[1] || !io[2] || (direct && !io[3])) return 0;
    f3_res *R = F.res;
    if (!R || R->rw != rw || R->rh != rh || R->uw != uw || R->uh != uh || (!direct && (!R->out.img || R->out_fmt != out_fmt))) {
        f3_res *n = res_make(rw, rh, uw, uh, out_fmt, !direct); if (!n) return 0;
        if (R) vka_after(vka_fence_last(), res_free, R);
        F.res = R = n; F.first = 1; F.parity = 0;
    }
    VkImageView vout = direct ? io[3] : R->out.view;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    float dt_ms = F.last.tv_sec ? (float)((now.tv_sec - F.last.tv_sec) * 1e3 + (now.tv_nsec - F.last.tv_nsec) / 1e6) : 1000.f / 30; F.last = now;
    int first = F.first, reset = first || o->reset || dt_ms > 250.f || (vka_fg_fast_on() ? g_f3_memo.fsr3_reset : VKA_ENV("DM_VK_FSR3_RESET", 0)); F.first = 0;
    uint32_t off = 0, sz[4] = {rw, rh, uw, uh}; int sharpen = 0;
    fsr3_fill_consts(rw, rh, uw, uh, dt_ms, first, reset, o, &off, &sharpen);
    fsr3_prepare_images(cb, R, first, reset, F.parity);
    fsr3_dispatch_passes(cb, R, io, vout, sz, F.parity, off, sharpen);
    if (!fsr3_finish_output(cb, R, dst, d, uw, uh, ow, oh, enc, direct, scaled)) return 0;
    static int noted; if (!noted++) VLOG("DLSS replaced by FSR 3.1: %ux%u -> %ux%u", rw, rh, uw, uh);
    return 1;
}
#endif
