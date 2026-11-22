// Buffer textures (NVN_TEXTURE_TARGET_BUFFER) on the Vulkan replay. On NVN a buffer texture is a typed view of a pool's memory, read through a texture handle; the library's
// binding records say how a shader takes one:
//   - a texel buffer descriptor (UNIFORM_TEXEL_BUFFER, or STORAGE_TEXEL_BUFFER for an image slot): a VkBufferView over the placed guest memory, the segment's own buffer
//     (vka_mem.c), so it reads and writes the memory itself, in GPU order. Views are kept per (buffer, offset, range, format) and made again once a segment's buffer is
//     destroyed (vka_seg_gen); a view that goes is destroyed when the GPU is past the last fence reserved then (vka_after)
//   - a sampled image: the translator reads a 1D fetch it cannot tell from a buffer read as 2D at (x & 0x3FFF, x >> 14) (dm/third_party/ryujinx/README.md, the Mac host's
//     tbuf_as_2d), or as a 1D image. Vulkan cannot sample a buffer through an image descriptor, so the buffer is copied into an image of 16384-texel rows (or a 1D image) on
//     the GPU, from the same guest memory, outside the render pass and between full barriers: at its first use in a frame and again after a GPU write into its memory was
//     recorded since its last copy (vka_tbuf_wrote, from the texture code's write tracking, vka_tex.c gw_add), so it reads what the work before it wrote
// Unreal Engine 5 reads its per-object data in vertex shaders this way (Orbitals: the title screen's one plane read zeros and was never drawn, the background stayed black).
// Not done: a buffer texture bound as a storage image (its writes would have to go back to the memory); it reads and writes nothing (logged)
#include "vka.h"
#include "vka_tbuf_fast.h"
#include "vka_tbuf_vl_fast.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

uint64_t vka_frame(void);
void vka_mem_describe(uint64_t addr, char *out, size_t n);   // (vka_mem.c, diagnostics)
enum { ROW = 16384, NVIEW = 256, NIMG = 256 };

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
uint64_t vka_mem_gen(void);
uint64_t nvn_pool_fin(void);
static _Atomic uintptr_t g_tbuf_owner;
static _Atomic uint64_t g_v_last[NVIEW], g_i_last[NIMG];
static _Atomic uint32_t g_v_evict, g_i_evict;
static _Atomic uint64_t g_timg_wgen = 1;
static vka_tbuf_view_memo4 g_vmemo;
static vka_tbuf_img_memo4 g_imemo;
static vka_tbuf_fmt_memo g_fmemo;
static vka_tbuf_vmap_t g_tb_vmap;
static vka_tbuf_imap_t g_tb_imap;
static vka_timg_wfilter_t g_timg_wf;
static inline int tbuf_is_owner(void) {
    uintptr_t me = (uintptr_t)pthread_self(), none = 0u;
    uintptr_t cur = atomic_load_explicit(&g_tbuf_owner, memory_order_relaxed);
    if (!cur) { atomic_compare_exchange_strong_explicit(&g_tbuf_owner, &none, me, memory_order_acq_rel, memory_order_acquire); cur = atomic_load_explicit(&g_tbuf_owner, memory_order_relaxed); }
    return cur == me;
}
static inline uint64_t tbuf_epoch(uint32_t evict_gen) {
    return vka_tbuf_epoch_mix(vka_mem_gen(), atomic_load_explicit(&vka_seg_gen, memory_order_acquire),
                              nvn_pool_fin(), atomic_load_explicit(&vka_env_gen, memory_order_relaxed), evict_gen);
}
static inline void tbuf_fmt_props(VkFormat fmt, int fast_own, VkFormatProperties *fp) {
    uint32_t feat = 0u;
    if (fast_own && vka_tbuf_fmt_memo_get(&g_fmemo, (uint64_t)(uintptr_t)V.pd, (uint32_t)fmt, &feat)) { fp->bufferFeatures = feat; return; }
    vk.vkGetPhysicalDeviceFormatProperties(V.pd, fmt, fp);
    if (fast_own) vka_tbuf_fmt_memo_put(&g_fmemo, (uint64_t)(uintptr_t)V.pd, (uint32_t)fmt, (uint32_t)fp->bufferFeatures);
}

typedef struct { int id, format; uint64_t addr, bytes; uint32_t elems; vka_fmt f; } tb_t;
static int tb_of(int id, tb_t *t) {
    if (!vka_tex_id_buffer(id, &t->addr, &t->elems, &t->format) || !t->elems) return 0;
    if (!vka_fmt_of(t->format, &t->f)) return 0;   // (logged by vka_fmt_of)
    if (t->f.bw != 1 || t->f.bh != 1 || t->f.kind >= 3 || !t->f.bpb) { VRED_ONCE("buffer texture of NVN format %#x: not a plain colour format, not bound", t->format); return 0; }
    t->id = id; t->bytes = (uint64_t)t->elems * t->f.bpb;
    return 1;
}
static int not_placed(const tb_t *t, const char *as) {
    static _Atomic int n; if (atomic_fetch_add(&n, 1) < 8) { char seg[400]; vka_mem_describe(t->addr, seg, sizeof seg);
        VRED("buffer texture ID %d (%u elements, format %#x) at %#llx: its %llu bytes are not in placed guest memory, it is not bound as a %s (%s)", t->id, t->elems, t->format,
             (unsigned long long)t->addr, (unsigned long long)t->bytes, as, seg); }
    return 0;
}

// ---------- Texel buffer views ----------
typedef struct { VkBuffer buf; VkDeviceSize off, range; VkFormat fmt; VkBufferView v; uint64_t last; } tview;
static tview g_v[NVIEW]; static int g_nv; static uint32_t g_vgen;
static void destroy_view(void *a) { vk.vkDestroyBufferView(V.dev, (VkBufferView)a, NULL); }
static void retire_view(VkBufferView v) { if (v) vka_after(vka_fence_last(), destroy_view, (void *)v); }
// DM_VK_TBUF (live; A/B): 1 (default) binds buffer textures as above, 2 leaves the storage texel buffers unbound (shaders read buffer textures but write none), 0 binds none (as before)
static int tbuf_mode(void) { return VKA_ENV("DM_VK_TBUF", 1); }
static void tb_vmap_rebuild(void) {
    vka_tbuf_vmap_clear(&g_tb_vmap);
    for (int i = 0; i < g_nv; i++)
        vka_tbuf_vmap_put(&g_tb_vmap, (uint64_t)(uintptr_t)g_v[i].buf, (uint64_t)g_v[i].off, (uint64_t)g_v[i].range, (int32_t)g_v[i].fmt, (uint16_t)i);
}
static VkBufferView tbuf_view_hit_or_make(int id, int writes, uint32_t tver0, uint64_t ep0,
                                           int fast_own, const tb_t *t, VkBuffer buf, VkDeviceSize off,
                                           VkDeviceSize range, VkFormat fmt) {
    uint64_t now = vka_frame();
    pthread_mutex_lock(&g_mu);
    uint32_t gen = atomic_load(&vka_seg_gen);
    if (gen != g_vgen) { for (int i = 0; i < g_nv; i++) retire_view(g_v[i].v); g_nv = 0; g_vgen = gen; vka_tbuf_vmap_clear(&g_tb_vmap); atomic_fetch_add_explicit(&g_v_evict, 1u, memory_order_release); }
    int slot = -1, fast_map = vka_tbuf_vl_fast_on(); VkBufferView v = VK_NULL_HANDLE; uint16_t mslot = 0u;
    if (fast_map && vka_tbuf_vmap_find(&g_tb_vmap, (uint64_t)(uintptr_t)buf, (uint64_t)off, (uint64_t)range, (int32_t)fmt, &mslot) && (int)mslot < g_nv) {
        slot = (int)mslot; g_v[slot].last = now; atomic_store_explicit(&g_v_last[slot], now, memory_order_relaxed); v = g_v[slot].v;
    } else if (!fast_map) {
        for (int i = 0; i < g_nv; i++) if (g_v[i].buf == buf && g_v[i].off == off && g_v[i].range == range && g_v[i].fmt == fmt) { slot = i; g_v[i].last = now; atomic_store_explicit(&g_v_last[i], now, memory_order_relaxed); v = g_v[i].v; break; }
    }
    if (!v) {
        VkBufferViewCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, .buffer = buf, .format = fmt, .offset = off, .range = range};
        VkResult r = vk.vkCreateBufferView(V.dev, &ci, NULL, &v);
        if (r) { pthread_mutex_unlock(&g_mu); static _Atomic int k; if (atomic_fetch_add(&k, 1) < 8) VRED("buffer texture ID %d: vkCreateBufferView (VkFormat %d, %llu bytes) failed (%d): not bound", id, fmt, (unsigned long long)range, r); return VK_NULL_HANDLE; }
        int ev = g_nv == NVIEW; slot = ev ? 0 : g_nv++;
        if (ev) { uint64_t bl = atomic_load_explicit(&g_v_last[0], memory_order_relaxed); for (int i = 1; i < NVIEW; i++) { uint64_t cl = atomic_load_explicit(&g_v_last[i], memory_order_relaxed); if (cl < bl) { bl = cl; slot = i; } } retire_view(g_v[slot].v); atomic_fetch_add_explicit(&g_v_evict, 1u, memory_order_release); }
        g_v[slot] = (tview){buf, off, range, fmt, v, now}; atomic_store_explicit(&g_v_last[slot], now, memory_order_relaxed);
        if (fast_map) { if (ev) tb_vmap_rebuild(); else vka_tbuf_vmap_put(&g_tb_vmap, (uint64_t)(uintptr_t)buf, (uint64_t)off, (uint64_t)range, (int32_t)fmt, (uint16_t)slot); }
        static int noted; if (!noted++) VLOG("buffer textures: bound as texel buffers over guest memory (first: texture ID %d, %u elements, VkFormat %d)", id, t->elems, fmt);
    }
    pthread_mutex_unlock(&g_mu);
    if (fast_own && v && tver0 && slot >= 0 && vka_tex_id_tver(id) == tver0 &&
        tbuf_epoch(atomic_load_explicit(&g_v_evict, memory_order_acquire)) == ep0) {
        vka_tbuf_view_memo4_put(&g_vmemo, (uint32_t)id, writes, tver0, ep0, (uint64_t)(uintptr_t)v, t->addr, (uint64_t)range, (uint16_t)slot);
    }
    return v;
}
VkBufferView vka_tbuf_view(int id, int writes) {
    int mode = tbuf_mode(); if (!mode || (writes && mode == 2)) return VK_NULL_HANDLE;
    int fast_own = vka_tbuf_fast_on() && id > 0 && id < (1 << 20) && tbuf_is_owner();
    uint32_t tver0 = fast_own ? vka_tex_id_tver(id) : 0u;
    uint64_t ep0 = fast_own ? tbuf_epoch(atomic_load_explicit(&g_v_evict, memory_order_acquire)) : 0ull;
    if (fast_own && tver0) {
        uint64_t bv_u = 0, ha = 0, hr = 0; uint16_t hslot = 0;
        if (vka_tbuf_view_memo4_find(&g_vmemo, (uint32_t)id, writes, tver0, ep0, &bv_u, &ha, &hr, &hslot)) {
            if (writes) vka_tex_gw_opaque(ha, hr);
            uint64_t now = vka_frame();
            if (hslot < NVIEW && atomic_load_explicit(&g_v_last[hslot], memory_order_relaxed) != now)
                atomic_store_explicit(&g_v_last[hslot], now, memory_order_relaxed);
            return (VkBufferView)(uintptr_t)bv_u;
        }
    }
    tb_t t; if (!tb_of(id, &t)) return VK_NULL_HANDLE;
    uint64_t max = V.props.limits.maxTexelBufferElements, n = t.elems < max ? t.elems : max;
    if (n < t.elems) { static _Atomic int k; if (atomic_fetch_add(&k, 1) < 4) VRED("buffer texture ID %d: %u elements, the device's texel buffers hold %llu: the rest reads as zero", id, t.elems, (unsigned long long)max); }
    VkDeviceSize range = (VkDeviceSize)n * t.f.bpb, off = 0; VkBuffer buf = VK_NULL_HANDLE;
    if (vka_addr_run(t.addr, range, &buf, &off) < range) { not_placed(&t, "texel buffer"); return VK_NULL_HANDLE; }
    VkDeviceSize al = V.props.limits.minTexelBufferOffsetAlignment;
    if (al > 1 && off % al) { static _Atomic int k; if (atomic_fetch_add(&k, 1) < 8) VRED("buffer texture ID %d at %#llx: offset %#llx in its segment is not a multiple of the device's texel buffer alignment %llu: not bound", id, (unsigned long long)t.addr, (unsigned long long)off, (unsigned long long)al); return VK_NULL_HANDLE; }
    VkFormat fmt = t.f.vk;
    VkFormatProperties fp; tbuf_fmt_props(fmt, fast_own, &fp);
    VkFormatFeatureFlags need = writes ? VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT : VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
    if (!(fp.bufferFeatures & need)) { static _Atomic int k; if (atomic_fetch_add(&k, 1) < 8) VRED("buffer texture ID %d: VkFormat %d is not a %s texel buffer format on this device: not bound", id, fmt, writes ? "storage" : "uniform"); return VK_NULL_HANDLE; }
    if (writes) vka_tex_gw_opaque(t.addr, range);
    return tbuf_view_hit_or_make(id, writes, tver0, ep0, fast_own, &t, buf, off, range, fmt);
}

// ---------- Images of buffer textures ----------
typedef struct { uint64_t addr, bytes, last, copied_frame; uint32_t elems, w, h; int format, is2d, laid, dirty; VkImage img; VkDeviceMemory mem; VkImageView v; } timg;
static timg g_i[NIMG]; static _Atomic int g_ni;
void vka_tbuf_wrote(uint64_t addr, uint64_t n) {
    if (!atomic_load_explicit(&g_ni, memory_order_relaxed)) return;
    if (vka_tbuf_vl_fast_on() && !vka_timg_wf_may_overlap(&g_timg_wf, addr, n)) return;
    pthread_mutex_lock(&g_mu);
    int dirtied = 0;
    for (int i = 0; i < g_ni; i++) if (addr < g_i[i].addr + g_i[i].bytes && g_i[i].addr < addr + n) { g_i[i].dirty = 1; dirtied = 1; }
    if (dirtied) atomic_fetch_add_explicit(&g_timg_wgen, 1u, memory_order_release);
    pthread_mutex_unlock(&g_mu);
}
static void destroy_img(void *a) {
    timg *x = a;
    if (x->v) vk.vkDestroyImageView(V.dev, x->v, NULL);
    if (x->img) vk.vkDestroyImage(V.dev, x->img, NULL);
    if (x->mem) vk.vkFreeMemory(V.dev, x->mem, NULL);
    free(x);
}
static void retire_img(const timg *x) { timg *c = malloc(sizeof *c); if (!c) return; *c = *x; vka_after(vka_fence_last(), destroy_img, c); }
// The image shapes a buffer texture can be read as: 2D (rows of ROW texels) or 1D, no array, multisample or depth compare
static int shape(uint8_t dim, int *is2d) {
    if (dim & (DMSH_VK_DIM_ARRAY | DMSH_VK_DIM_MS | DMSH_VK_DIM_DEPTH)) return 0;
    int d = dim & DMSH_VK_DIM_MASK;
    if (d == 0 || d == 1) { *is2d = d == 1; return 1; }
    return 0;
}
static void timg_maps_rebuild(void) {
    vka_tbuf_imap_clear(&g_tb_imap); vka_timg_wf_clear(&g_timg_wf);
    for (int i = 0; i < g_ni; i++) {
        vka_tbuf_imap_put(&g_tb_imap, g_i[i].addr, g_i[i].elems, (int32_t)g_i[i].format, (uint8_t)(g_i[i].is2d != 0), (uint16_t)i);
        vka_timg_wf_add(&g_timg_wf, g_i[i].addr, g_i[i].bytes);
    }
}
static timg *timg_store_slot(timg x) {
    int ev = g_ni == NIMG, slot = ev ? 0 : g_ni++;
    if (ev) { uint64_t bl = atomic_load_explicit(&g_i_last[0], memory_order_relaxed); for (int i = 1; i < NIMG; i++) { uint64_t cl = atomic_load_explicit(&g_i_last[i], memory_order_relaxed); if (cl < bl) { bl = cl; slot = i; } } retire_img(&g_i[slot]); atomic_fetch_add_explicit(&g_i_evict, 1u, memory_order_release); }
    g_i[slot] = x; atomic_store_explicit(&g_i_last[slot], 0u, memory_order_relaxed);
    if (vka_tbuf_vl_fast_on()) {
        if (ev) timg_maps_rebuild();
        else { vka_tbuf_imap_put(&g_tb_imap, x.addr, x.elems, (int32_t)x.format, (uint8_t)(x.is2d != 0), (uint16_t)slot); vka_timg_wf_add(&g_timg_wf, x.addr, x.bytes); }
    }
    return &g_i[slot];
}
static timg *img_find(const tb_t *t, int is2d) {   // (g_mu held)
    if (vka_tbuf_vl_fast_on()) {
        uint16_t slot = 0u;
        if (vka_tbuf_imap_find(&g_tb_imap, t->addr, t->elems, (int32_t)t->format, (uint8_t)(is2d != 0), &slot) && (int)slot < g_ni) return &g_i[slot];
        return NULL;
    }
    for (int i = 0; i < g_ni; i++) if (g_i[i].addr == t->addr && g_i[i].elems == t->elems && g_i[i].format == t->format && g_i[i].is2d == is2d) return &g_i[i];
    return NULL;
}
static timg *img_make(const tb_t *t, int is2d) {   // (g_mu held)
    uint32_t w, h;
    if (is2d) {
        w = t->elems < ROW ? t->elems : ROW; h = (t->elems + ROW - 1) / ROW;
        if (w > V.props.limits.maxImageDimension2D || h > V.props.limits.maxImageDimension2D) { VRED_ONCE("buffer texture of %u elements read as 2D rows of %d: larger than the device's 2D images, not bound", t->elems, ROW); return NULL; }
    } else {
        w = t->elems; h = 1;
        if (w > V.props.limits.maxImageDimension1D) { w = V.props.limits.maxImageDimension1D; VRED_ONCE("buffer texture of %u elements read as a 1D image: only its first %u (the device's 1D images)", t->elems, w); }
    }
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = t->f.vk != t->f.view ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0, .imageType = is2d ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D,
                            .format = t->f.vk, .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    timg x = {t->addr, t->bytes, 0, 0, t->elems, w, h, t->format, is2d, 0, 1, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkResult r = vk.vkCreateImage(V.dev, &ci, NULL, &x.img);
    if (!r) {
        VkMemoryRequirements mr; vk.vkGetImageMemoryRequirements(V.dev, x.img, &mr);
        uint32_t ty = vka_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0); if (ty == UINT32_MAX) ty = vka_mem_type(mr.memoryTypeBits, 0, 0);
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size, .memoryTypeIndex = ty};
        r = ty == UINT32_MAX ? VK_ERROR_OUT_OF_DEVICE_MEMORY : vk.vkAllocateMemory(V.dev, &ai, NULL, &x.mem);
        if (!r) r = vk.vkBindImageMemory(V.dev, x.img, x.mem, 0);
    }
    if (!r) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = x.img, .viewType = is2d ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D, .format = t->f.view,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        r = vk.vkCreateImageView(V.dev, &vi, NULL, &x.v);
    }
    if (r) {
        static _Atomic int k; if (atomic_fetch_add(&k, 1) < 8) VRED("buffer texture ID %d (%u elements, VkFormat %d): its %s image %ux%u could not be made (%d): not bound", t->id, t->elems, t->f.vk, is2d ? "2D" : "1D", w, h, r);
        x.laid = 0; timg *c = malloc(sizeof *c); if (c) { *c = x; destroy_img(c); }
        return NULL;
    }
    return timg_store_slot(x);
}
static inline void tbuf_img_warm(int id, int is2d, uint32_t tver0, uint64_t fr, uint64_t wg0, uint64_t ep0, VkImageView v, int slot) {
    if (v && tver0 && slot >= 0 && vka_tex_id_tver(id) == tver0 &&
        atomic_load_explicit(&g_timg_wgen, memory_order_acquire) == wg0 &&
        tbuf_epoch(atomic_load_explicit(&g_i_evict, memory_order_acquire)) == ep0) {
        vka_tbuf_img_memo4_put(&g_imemo, (uint32_t)id, is2d, tver0, fr, wg0, ep0, (uint64_t)(uintptr_t)v, (uint16_t)slot);
    }
}
int vka_tbuf_pending(int id, uint8_t dim) {
    int is2d;
    if (!vka_tex_id_is_buffer(id) || !tbuf_mode() || !shape(dim, &is2d)) return 0;
    int fast_own = vka_tbuf_fast_on() && id > 0 && id < (1 << 20) && tbuf_is_owner();
    uint32_t tver0 = fast_own ? vka_tex_id_tver(id) : 0u;
    uint64_t fr = vka_frame();
    uint64_t wg0 = fast_own ? atomic_load_explicit(&g_timg_wgen, memory_order_acquire) : 0ull;
    uint64_t ep0 = fast_own ? tbuf_epoch(atomic_load_explicit(&g_i_evict, memory_order_acquire)) : 0ull;
    if (fast_own && tver0) {
        uint64_t iv_u = 0; uint16_t hs = 0;
        if (vka_tbuf_img_memo4_find(&g_imemo, (uint32_t)id, is2d, tver0, fr, wg0, ep0, &iv_u, &hs)) return 0;
    }
    tb_t t; if (!tb_of(id, &t)) return 0;
    pthread_mutex_lock(&g_mu);
    const timg *x = img_find(&t, is2d);
    int p = !x || x->copied_frame != fr || x->dirty;
    VkImageView xv = !p ? x->v : VK_NULL_HANDLE;
    int slot = !p ? (int)(x - g_i) : -1;
    pthread_mutex_unlock(&g_mu);
    if (fast_own && !p) tbuf_img_warm(id, is2d, tver0, fr, wg0, ep0, xv, slot);
    return p;
}
void vka_tbuf_refresh(int id, uint8_t dim, VkCommandBuffer cb) {
    tb_t t; int is2d;
    int fast_own = vka_tbuf_fast_on() && id > 0 && id < (1 << 20) && tbuf_is_owner();
    uint32_t tver0 = fast_own ? vka_tex_id_tver(id) : 0u;
    uint64_t wg0 = fast_own ? atomic_load_explicit(&g_timg_wgen, memory_order_acquire) : 0ull;
    uint64_t ep0 = fast_own ? tbuf_epoch(atomic_load_explicit(&g_i_evict, memory_order_acquire)) : 0ull;
    if (!cb || !tb_of(id, &t)) return;
    if (!shape(dim, &is2d)) { VRED_ONCE("buffer texture ID %d read as an image of shape %#x (not 1D or 2D): not bound", id, dim); return; }
    pthread_mutex_lock(&g_mu);
    timg *x = img_find(&t, is2d); if (!x) x = img_make(&t, is2d);
    uint64_t fr = vka_frame();
    if (!x || (x->copied_frame == fr && !x->dirty)) {
        VkImageView xv = x ? x->v : VK_NULL_HANDLE; int slot = x ? (int)(x - g_i) : -1;
        pthread_mutex_unlock(&g_mu);
        if (fast_own && xv) tbuf_img_warm(id, is2d, tver0, fr, wg0, ep0, xv, slot);
        return;
    }
    uint64_t n = is2d ? t.elems : x->w, bytes = n * t.f.bpb;
    VkBuffer buf = VK_NULL_HANDLE; VkDeviceSize off = 0;
    if (vka_addr_run(t.addr, bytes, &buf, &off) < bytes) { pthread_mutex_unlock(&g_mu); not_placed(&t, "sampled image"); return; }
    if (off % t.f.bpb) {
        pthread_mutex_unlock(&g_mu); static _Atomic int k; if (atomic_fetch_add(&k, 1) < 8)
            VRED("buffer texture ID %d at %#llx: offset %#llx in its segment is not a multiple of its texel size (%u): not copied into its image", id, (unsigned long long)t.addr, (unsigned long long)off, t.f.bpb);
        return;
    }
    vka_barrier(cb);   // the work recorded before (stores into the memory) before the copy reads it
    if (!x->laid) {
        VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                  .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = x->img,
                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        vk.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
        x->laid = 1;
    }
    VkBufferImageCopy rg[2]; uint32_t nr = 0;
    if (is2d) {
        uint32_t full = t.elems / ROW, rem = t.elems % ROW;
        if (full) rg[nr++] = (VkBufferImageCopy){off, ROW, full, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {ROW, full, 1}};
        if (rem) rg[nr++] = (VkBufferImageCopy){off + (VkDeviceSize)full * ROW * t.f.bpb, rem, 1, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, (int32_t)full, 0}, {rem, 1, 1}};
    } else rg[nr++] = (VkBufferImageCopy){off, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {x->w, 1, 1}};
    vk.vkCmdCopyBufferToImage(cb, buf, x->img, VK_IMAGE_LAYOUT_GENERAL, nr, rg);
    vka_barrier(cb);   // the copy before the reads
    x->copied_frame = fr; x->dirty = 0; x->last = fr;
    int rslot = (int)(x - g_i); VkImageView rv = x->v;
    atomic_store_explicit(&g_i_last[rslot], fr, memory_order_relaxed);
    static int noted; if (!noted++) VLOG("buffer textures: read as %s images, copied from guest memory on the GPU at their first use in a frame and after GPU writes (first: texture ID %d, %u elements, VkFormat %d)",
                                         is2d ? "2D (16384-texel rows)" : "1D", id, t.elems, t.f.vk);
    pthread_mutex_unlock(&g_mu);
    if (fast_own) tbuf_img_warm(id, is2d, tver0, fr, wg0, ep0, rv, rslot);
}
VkImageView vka_tbuf_image_view(int id, uint8_t dim) {
    int is2d; if (!shape(dim, &is2d)) return VK_NULL_HANDLE;
    int fast_own = vka_tbuf_fast_on() && id > 0 && id < (1 << 20) && tbuf_is_owner();
    uint32_t tver0 = fast_own ? vka_tex_id_tver(id) : 0u;
    uint64_t fr = vka_frame();
    uint64_t wg0 = fast_own ? atomic_load_explicit(&g_timg_wgen, memory_order_acquire) : 0ull;
    uint64_t ep0 = fast_own ? tbuf_epoch(atomic_load_explicit(&g_i_evict, memory_order_acquire)) : 0ull;
    if (fast_own && tver0) {
        uint64_t iv_u = 0; uint16_t hs = 0;
        if (vka_tbuf_img_memo4_find(&g_imemo, (uint32_t)id, is2d, tver0, fr, wg0, ep0, &iv_u, &hs)) {
            if (hs < NIMG && atomic_load_explicit(&g_i_last[hs], memory_order_relaxed) != fr)
                atomic_store_explicit(&g_i_last[hs], fr, memory_order_relaxed);
            return (VkImageView)(uintptr_t)iv_u;
        }
    }
    tb_t t; if (!tb_of(id, &t)) return VK_NULL_HANDLE;
    pthread_mutex_lock(&g_mu);
    timg *x = img_find(&t, is2d); VkImageView v = VK_NULL_HANDLE; int slot = -1, clean = 0;
    if (x && x->copied_frame) { x->last = fr; slot = (int)(x - g_i); atomic_store_explicit(&g_i_last[slot], fr, memory_order_relaxed); v = x->v; clean = (x->copied_frame == fr && !x->dirty); }
    pthread_mutex_unlock(&g_mu);
    if (fast_own && clean) tbuf_img_warm(id, is2d, tver0, fr, wg0, ep0, v, slot);
    return v;
}
