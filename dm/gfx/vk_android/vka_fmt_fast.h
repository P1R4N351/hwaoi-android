// Fast paths for format LUT decoding, twin maps, lock-free feature cache, and upload pool claim/wake (Opt 49).
#ifndef VKA_FMT_FAST_H
#define VKA_FMT_FAST_H
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef T_W47_TEST_HOOKS
extern _Atomic int g_vka_fmt_fast_override;
extern _Atomic int g_aoi_w47_fault;
static inline void vka_fmt_fast_set_for_test(int v) {
    atomic_store_explicit(&g_vka_fmt_fast_override, v, memory_order_release);
}
static inline void aoi_w47_set_fault(int f) {
    atomic_store_explicit(&g_aoi_w47_fault, f, memory_order_release);
}
static inline int aoi_w47_fault_mode(void) {
    int f = atomic_load_explicit(&g_aoi_w47_fault, memory_order_relaxed);
    if (f >= 0) return f;
    const char *e = getenv("AOI_W47_FAULT");
    int v = (e && *e) ? atoi(e) : 0;
    atomic_store_explicit(&g_aoi_w47_fault, v, memory_order_relaxed);
    return v;
}
#else
static inline int aoi_w47_fault_mode(void) { return 0; }
#endif

static inline int vka_fmt_fast_on(void) {
#ifdef T_W47_TEST_HOOKS
    int ov = atomic_load_explicit(&g_vka_fmt_fast_override, memory_order_acquire);
    if (ov >= 0) return ov != 0;
#endif
    static _Atomic int g_on = -1;
    int v = atomic_load_explicit(&g_on, memory_order_acquire);
    if (v < 0) {
        const char *e = getenv("DM_VK_FMT_FAST");
        v = !(e && strcmp(e, "0") == 0);
        atomic_store_explicit(&g_on, v, memory_order_release);
    }
    return v;
}

enum {
    VKA_FMT_VK_UNDEFINED = 0,
    VKA_FMT_VK_R5G6B5_UNORM_PACK16 = 4,
    VKA_FMT_VK_B5G6R5_UNORM_PACK16 = 5,
    VKA_FMT_VK_R8_UNORM = 9,
    VKA_FMT_VK_R8_SNORM = 10,
    VKA_FMT_VK_R8_UINT = 13,
    VKA_FMT_VK_R8_SINT = 14,
    VKA_FMT_VK_R8G8_UNORM = 16,
    VKA_FMT_VK_R8G8_SNORM = 17,
    VKA_FMT_VK_R8G8_UINT = 20,
    VKA_FMT_VK_R8G8_SINT = 21,
    VKA_FMT_VK_R8G8B8A8_UNORM = 37,
    VKA_FMT_VK_R8G8B8A8_SNORM = 38,
    VKA_FMT_VK_R8G8B8A8_UINT = 41,
    VKA_FMT_VK_R8G8B8A8_SINT = 42,
    VKA_FMT_VK_R8G8B8A8_SRGB = 43,
    VKA_FMT_VK_B8G8R8A8_UNORM = 44,
    VKA_FMT_VK_B8G8R8A8_SRGB = 50,
    VKA_FMT_VK_A8B8G8R8_UNORM_PACK32 = 51,
    VKA_FMT_VK_A8B8G8R8_SRGB_PACK32 = 57,
    VKA_FMT_VK_A2B10G10R10_UNORM_PACK32 = 64,
    VKA_FMT_VK_A2B10G10R10_SNORM_PACK32 = 65,
    VKA_FMT_VK_A2B10G10R10_UINT_PACK32 = 68,
    VKA_FMT_VK_A2B10G10R10_SINT_PACK32 = 69,
    VKA_FMT_VK_R16_UNORM = 70,
    VKA_FMT_VK_R16_SNORM = 71,
    VKA_FMT_VK_R16_UINT = 74,
    VKA_FMT_VK_R16_SINT = 75,
    VKA_FMT_VK_R16_SFLOAT = 76,
    VKA_FMT_VK_R16G16_UNORM = 77,
    VKA_FMT_VK_R16G16_SNORM = 78,
    VKA_FMT_VK_R16G16_UINT = 81,
    VKA_FMT_VK_R16G16_SINT = 82,
    VKA_FMT_VK_R16G16_SFLOAT = 83,
    VKA_FMT_VK_R16G16B16A16_UNORM = 91,
    VKA_FMT_VK_R16G16B16A16_SNORM = 92,
    VKA_FMT_VK_R16G16B16A16_UINT = 95,
    VKA_FMT_VK_R16G16B16A16_SINT = 96,
    VKA_FMT_VK_R16G16B16A16_SFLOAT = 97,
    VKA_FMT_VK_R32_UINT = 98,
    VKA_FMT_VK_R32_SINT = 99,
    VKA_FMT_VK_R32_SFLOAT = 100,
    VKA_FMT_VK_R32G32_UINT = 101,
    VKA_FMT_VK_R32G32_SINT = 102,
    VKA_FMT_VK_R32G32_SFLOAT = 103,
    VKA_FMT_VK_R32G32B32_UINT = 104,
    VKA_FMT_VK_R32G32B32_SINT = 105,
    VKA_FMT_VK_R32G32B32_SFLOAT = 106,
    VKA_FMT_VK_R32G32B32A32_UINT = 107,
    VKA_FMT_VK_R32G32B32A32_SINT = 108,
    VKA_FMT_VK_R32G32B32A32_SFLOAT = 109,
    VKA_FMT_VK_B10G11R11_UFLOAT_PACK32 = 122,
    VKA_FMT_VK_E5B9G9R9_UFLOAT_PACK32 = 123,
    VKA_FMT_VK_D16_UNORM = 124,
    VKA_FMT_VK_X8_D24_UNORM_PACK32 = 125,
    VKA_FMT_VK_D32_SFLOAT = 126,
    VKA_FMT_VK_S8_UINT = 127,
    VKA_FMT_VK_D24_UNORM_S8_UINT = 129,
    VKA_FMT_VK_D32_SFLOAT_S8_UINT = 130,
    VKA_FMT_VK_BC1_RGB_UNORM_BLOCK = 131,
    VKA_FMT_VK_BC1_RGB_SRGB_BLOCK = 132,
    VKA_FMT_VK_BC1_RGBA_UNORM_BLOCK = 133,
    VKA_FMT_VK_BC1_RGBA_SRGB_BLOCK = 134,
    VKA_FMT_VK_BC2_UNORM_BLOCK = 135,
    VKA_FMT_VK_BC2_SRGB_BLOCK = 136,
    VKA_FMT_VK_BC3_UNORM_BLOCK = 137,
    VKA_FMT_VK_BC3_SRGB_BLOCK = 138,
    VKA_FMT_VK_BC4_UNORM_BLOCK = 139,
    VKA_FMT_VK_BC4_SNORM_BLOCK = 140,
    VKA_FMT_VK_BC5_UNORM_BLOCK = 141,
    VKA_FMT_VK_BC5_SNORM_BLOCK = 142,
    VKA_FMT_VK_BC6H_UFLOAT_BLOCK = 143,
    VKA_FMT_VK_BC6H_SFLOAT_BLOCK = 144,
    VKA_FMT_VK_BC7_UNORM_BLOCK = 145,
    VKA_FMT_VK_BC7_SRGB_BLOCK = 146,
    VKA_FMT_VK_ASTC_4x4_UNORM_BLOCK = 157,
    VKA_FMT_VK_ASTC_12x12_SRGB_BLOCK = 184,
    VKA_FMT_VK_A4B4G4R4_UNORM_PACK16 = 1000340001u,
    VKA_FMT_VK_A1B5G5R5_UNORM_PACK16 = 1000470000u
};

typedef struct {
    uint32_t vk;
    uint8_t kind;
    uint8_t srgb;
} vka_fmt_lut_ent_t;

static const vka_fmt_lut_ent_t g_vka_fmt_lut[0x9D] = {
    [0x01] = {VKA_FMT_VK_R8_UNORM, 0, 0}, [0x02] = {VKA_FMT_VK_R8_SNORM, 0, 0},
    [0x03] = {VKA_FMT_VK_R8_UINT, 1, 0}, [0x04] = {VKA_FMT_VK_R8_SINT, 2, 0},
    [0x05] = {VKA_FMT_VK_R16_SFLOAT, 0, 0}, [0x06] = {VKA_FMT_VK_R16_UNORM, 0, 0},
    [0x07] = {VKA_FMT_VK_R16_SNORM, 0, 0}, [0x08] = {VKA_FMT_VK_R16_UINT, 1, 0},
    [0x09] = {VKA_FMT_VK_R16_SINT, 2, 0}, [0x0A] = {VKA_FMT_VK_R32_SFLOAT, 0, 0},
    [0x0B] = {VKA_FMT_VK_R32_UINT, 1, 0}, [0x0C] = {VKA_FMT_VK_R32_SINT, 2, 0},
    [0x0D] = {VKA_FMT_VK_R8G8_UNORM, 0, 0}, [0x0E] = {VKA_FMT_VK_R8G8_SNORM, 0, 0},
    [0x0F] = {VKA_FMT_VK_R8G8_UINT, 1, 0}, [0x10] = {VKA_FMT_VK_R8G8_SINT, 2, 0},
    [0x11] = {VKA_FMT_VK_R16G16_SFLOAT, 0, 0}, [0x12] = {VKA_FMT_VK_R16G16_UNORM, 0, 0},
    [0x13] = {VKA_FMT_VK_R16G16_SNORM, 0, 0}, [0x14] = {VKA_FMT_VK_R16G16_UINT, 1, 0},
    [0x15] = {VKA_FMT_VK_R16G16_SINT, 2, 0}, [0x16] = {VKA_FMT_VK_R32G32_SFLOAT, 0, 0},
    [0x17] = {VKA_FMT_VK_R32G32_UINT, 1, 0}, [0x18] = {VKA_FMT_VK_R32G32_SINT, 2, 0},
    [0x22] = {VKA_FMT_VK_R32G32B32_SFLOAT, 0, 0}, [0x23] = {VKA_FMT_VK_R32G32B32_UINT, 1, 0},
    [0x24] = {VKA_FMT_VK_R32G32B32_SINT, 2, 0}, [0x25] = {VKA_FMT_VK_R8G8B8A8_UNORM, 0, 0},
    [0x26] = {VKA_FMT_VK_R8G8B8A8_SNORM, 0, 0}, [0x27] = {VKA_FMT_VK_R8G8B8A8_UINT, 1, 0},
    [0x28] = {VKA_FMT_VK_R8G8B8A8_SINT, 2, 0}, [0x29] = {VKA_FMT_VK_R16G16B16A16_SFLOAT, 0, 0},
    [0x2A] = {VKA_FMT_VK_R16G16B16A16_UNORM, 0, 0}, [0x2B] = {VKA_FMT_VK_R16G16B16A16_SNORM, 0, 0},
    [0x2C] = {VKA_FMT_VK_R16G16B16A16_UINT, 1, 0}, [0x2D] = {VKA_FMT_VK_R16G16B16A16_SINT, 2, 0},
    [0x2E] = {VKA_FMT_VK_R32G32B32A32_SFLOAT, 0, 0}, [0x2F] = {VKA_FMT_VK_R32G32B32A32_UINT, 1, 0},
    [0x30] = {VKA_FMT_VK_R32G32B32A32_SINT, 2, 0}, [0x31] = {VKA_FMT_VK_S8_UINT, 4, 0},
    [0x32] = {VKA_FMT_VK_D16_UNORM, 3, 0}, [0x33] = {VKA_FMT_VK_X8_D24_UNORM_PACK32, 3, 0},
    [0x34] = {VKA_FMT_VK_D32_SFLOAT, 3, 0}, [0x35] = {VKA_FMT_VK_D24_UNORM_S8_UINT, 5, 0},
    [0x36] = {VKA_FMT_VK_D32_SFLOAT_S8_UINT, 5, 0},
    [0x37] = {VKA_FMT_VK_R8G8B8A8_UNORM, 0, 1}, [0x38] = {VKA_FMT_VK_R8G8B8A8_UNORM, 0, 1},
    [0x39] = {VKA_FMT_VK_A4B4G4R4_UNORM_PACK16, 0, 0},
    [0x3A] = {VKA_FMT_VK_A1B5G5R5_UNORM_PACK16, 0, 0}, [0x3B] = {VKA_FMT_VK_A1B5G5R5_UNORM_PACK16, 0, 0},
    [0x3C] = {VKA_FMT_VK_B5G6R5_UNORM_PACK16, 0, 0}, [0x3D] = {VKA_FMT_VK_A2B10G10R10_UNORM_PACK32, 0, 0},
    [0x3E] = {VKA_FMT_VK_A2B10G10R10_UINT_PACK32, 1, 0}, [0x3F] = {VKA_FMT_VK_B10G11R11_UFLOAT_PACK32, 0, 0},
    [0x40] = {VKA_FMT_VK_E5B9G9R9_UFLOAT_PACK32, 0, 0},
    [0x41] = {VKA_FMT_VK_BC1_RGB_UNORM_BLOCK, 0, 0}, [0x42] = {VKA_FMT_VK_BC1_RGBA_UNORM_BLOCK, 0, 0},
    [0x43] = {VKA_FMT_VK_BC2_UNORM_BLOCK, 0, 0}, [0x44] = {VKA_FMT_VK_BC3_UNORM_BLOCK, 0, 0},
    [0x45] = {VKA_FMT_VK_BC1_RGB_UNORM_BLOCK, 0, 1}, [0x46] = {VKA_FMT_VK_BC1_RGBA_UNORM_BLOCK, 0, 1},
    [0x47] = {VKA_FMT_VK_BC2_UNORM_BLOCK, 0, 1}, [0x48] = {VKA_FMT_VK_BC3_UNORM_BLOCK, 0, 1},
    [0x49] = {VKA_FMT_VK_BC4_UNORM_BLOCK, 0, 0}, [0x4A] = {VKA_FMT_VK_BC4_SNORM_BLOCK, 0, 0},
    [0x4B] = {VKA_FMT_VK_BC5_UNORM_BLOCK, 0, 0}, [0x4C] = {VKA_FMT_VK_BC5_SNORM_BLOCK, 0, 0},
    [0x4D] = {VKA_FMT_VK_BC7_UNORM_BLOCK, 0, 0}, [0x4E] = {VKA_FMT_VK_BC7_UNORM_BLOCK, 0, 1},
    [0x4F] = {VKA_FMT_VK_BC6H_SFLOAT_BLOCK, 0, 0}, [0x50] = {VKA_FMT_VK_BC6H_UFLOAT_BLOCK, 0, 0},
    [0x51] = {VKA_FMT_VK_R8_UINT, 1, 0}, [0x52] = {VKA_FMT_VK_R8_SINT, 2, 0},
    [0x53] = {VKA_FMT_VK_R16_UINT, 1, 0}, [0x54] = {VKA_FMT_VK_R16_SINT, 2, 0},
    [0x55] = {VKA_FMT_VK_R32_UINT, 1, 0}, [0x56] = {VKA_FMT_VK_R32_SINT, 2, 0},
    [0x57] = {VKA_FMT_VK_R8G8_UINT, 1, 0}, [0x58] = {VKA_FMT_VK_R8G8_SINT, 2, 0},
    [0x59] = {VKA_FMT_VK_R16G16_UINT, 1, 0}, [0x5A] = {VKA_FMT_VK_R16G16_SINT, 2, 0},
    [0x5B] = {VKA_FMT_VK_R32G32_UINT, 1, 0}, [0x5C] = {VKA_FMT_VK_R32G32_SINT, 2, 0},
    [0x61] = {VKA_FMT_VK_R32G32B32_UINT, 1, 0}, [0x62] = {VKA_FMT_VK_R32G32B32_SINT, 2, 0},
    [0x63] = {VKA_FMT_VK_R8G8B8A8_UINT, 1, 0}, [0x64] = {VKA_FMT_VK_R8G8B8A8_SINT, 2, 0},
    [0x65] = {VKA_FMT_VK_R16G16B16A16_UINT, 1, 0}, [0x66] = {VKA_FMT_VK_R16G16B16A16_SINT, 2, 0},
    [0x67] = {VKA_FMT_VK_R32G32B32A32_UINT, 1, 0}, [0x68] = {VKA_FMT_VK_R32G32B32A32_SINT, 2, 0},
    [0x69] = {VKA_FMT_VK_A2B10G10R10_SNORM_PACK32, 0, 0}, [0x6A] = {VKA_FMT_VK_A2B10G10R10_SINT_PACK32, 2, 0},
    [0x6B] = {VKA_FMT_VK_A2B10G10R10_UINT_PACK32, 1, 0}, [0x6C] = {VKA_FMT_VK_A2B10G10R10_SINT_PACK32, 2, 0},
    [0x6D] = {VKA_FMT_VK_R8G8B8A8_UNORM, 0, 0}, [0x6E] = {VKA_FMT_VK_R8G8B8A8_SNORM, 0, 0},
    [0x6F] = {VKA_FMT_VK_R8G8B8A8_UINT, 1, 0}, [0x70] = {VKA_FMT_VK_R8G8B8A8_SINT, 2, 0},
    [0x71] = {VKA_FMT_VK_R16G16B16A16_SFLOAT, 0, 0}, [0x72] = {VKA_FMT_VK_R16G16B16A16_UNORM, 0, 0},
    [0x73] = {VKA_FMT_VK_R16G16B16A16_SNORM, 0, 0}, [0x74] = {VKA_FMT_VK_R16G16B16A16_UINT, 1, 0},
    [0x75] = {VKA_FMT_VK_R16G16B16A16_SINT, 2, 0}, [0x76] = {VKA_FMT_VK_R32G32B32A32_SFLOAT, 0, 0},
    [0x77] = {VKA_FMT_VK_R32G32B32A32_UINT, 1, 0}, [0x78] = {VKA_FMT_VK_R32G32B32A32_SINT, 2, 0},
    [0x79] = {157, 0, 0}, [0x7A] = {159, 0, 0}, [0x7B] = {161, 0, 0}, [0x7C] = {163, 0, 0},
    [0x7D] = {165, 0, 0}, [0x7E] = {167, 0, 0}, [0x7F] = {169, 0, 0}, [0x80] = {171, 0, 0},
    [0x81] = {173, 0, 0}, [0x82] = {175, 0, 0}, [0x83] = {177, 0, 0}, [0x84] = {179, 0, 0},
    [0x85] = {181, 0, 0}, [0x86] = {183, 0, 0},
    [0x87] = {157, 0, 1}, [0x88] = {159, 0, 1}, [0x89] = {161, 0, 1}, [0x8A] = {163, 0, 1},
    [0x8B] = {165, 0, 1}, [0x8C] = {167, 0, 1}, [0x8D] = {169, 0, 1}, [0x8E] = {171, 0, 1},
    [0x8F] = {173, 0, 1}, [0x90] = {175, 0, 1}, [0x91] = {177, 0, 1}, [0x92] = {179, 0, 1},
    [0x93] = {181, 0, 1}, [0x94] = {183, 0, 1},
    [0x95] = {VKA_FMT_VK_R5G6B5_UNORM_PACK16, 0, 0},
    [0x99] = {VKA_FMT_VK_B8G8R8A8_UNORM, 0, 0}, [0x9A] = {VKA_FMT_VK_B8G8R8A8_UNORM, 0, 0},
    [0x9B] = {VKA_FMT_VK_B8G8R8A8_UNORM, 0, 1}, [0x9C] = {VKA_FMT_VK_B8G8R8A8_UNORM, 0, 1}
};

static inline vka_fmt_lut_ent_t vka_fmt_lut_lookup(int f) {
    if ((uint32_t)f > 0x9Cu) return (vka_fmt_lut_ent_t){0u, 0u, 0u};
    vka_fmt_lut_ent_t e = g_vka_fmt_lut[(uint32_t)f];
    if (__builtin_expect(aoi_w47_fault_mode() == 1 && f == 0x37, 0)) e.srgb = 0u;
    return e;
}

static inline uint32_t vka_srgb_twin_fast(uint32_t f) {
    if (aoi_w47_fault_mode() == 2 && f == VKA_FMT_VK_BC1_RGBA_UNORM_BLOCK) return f;
    if (f >= VKA_FMT_VK_ASTC_4x4_UNORM_BLOCK && f <= VKA_FMT_VK_ASTC_12x12_SRGB_BLOCK) return f + 1u;
    if (f == VKA_FMT_VK_R8G8B8A8_UNORM) return VKA_FMT_VK_R8G8B8A8_SRGB;
    if (f == VKA_FMT_VK_B8G8R8A8_UNORM) return VKA_FMT_VK_B8G8R8A8_SRGB;
    if (f >= VKA_FMT_VK_BC1_RGB_UNORM_BLOCK && f <= VKA_FMT_VK_BC7_UNORM_BLOCK) {
        static const uint16_t k_bc[15] = {
            VKA_FMT_VK_BC1_RGB_SRGB_BLOCK, 0, VKA_FMT_VK_BC1_RGBA_SRGB_BLOCK, 0,
            VKA_FMT_VK_BC2_SRGB_BLOCK, 0, VKA_FMT_VK_BC3_SRGB_BLOCK, 0,
            0, 0, 0, 0, 0, 0, VKA_FMT_VK_BC7_SRGB_BLOCK
        };
        uint16_t hit = k_bc[f - VKA_FMT_VK_BC1_RGB_UNORM_BLOCK];
        return hit ? (uint32_t)hit : f;
    }
    return f;
}

static inline uint32_t vka_raw_unorm_twin_fast(uint32_t f) {
    if (aoi_w47_fault_mode() == 3 && f == VKA_FMT_VK_A8B8G8R8_SRGB_PACK32) return f;
    if (f == VKA_FMT_VK_R8G8B8A8_SRGB) return VKA_FMT_VK_R8G8B8A8_UNORM;
    if (f == VKA_FMT_VK_B8G8R8A8_SRGB) return VKA_FMT_VK_B8G8R8A8_UNORM;
    if (f == VKA_FMT_VK_A8B8G8R8_SRGB_PACK32) return VKA_FMT_VK_A8B8G8R8_UNORM_PACK32;
    return f;
}

typedef uint32_t (*vka_feats_probe_fn)(uint32_t fmt, void *ctx);

typedef struct {
    _Atomic uint64_t core[256];
    pthread_mutex_t ext_mu;
    struct {
        uint32_t fmt;
        uint32_t ff;
    } ext[64];
    uint32_t ext_n;
} vka_feats_cache_t;

#define VKA_FEATS_CACHE_INIT { .ext_mu = PTHREAD_MUTEX_INITIALIZER }

static inline void vka_feats_cache_init(vka_feats_cache_t *c) {
    memset(c->core, 0, sizeof c->core);
    pthread_mutex_init(&c->ext_mu, NULL);
    c->ext_n = 0u;
}

static inline uint32_t vka_feats_fast_get(
    vka_feats_cache_t *c, uint32_t fmt, vka_feats_probe_fn probe, void *ctx) {
    if (fmt < 256u) {
        uint64_t cached = atomic_load_explicit(&c->core[fmt], memory_order_acquire);
        if ((cached & (UINT64_C(1) << 63)) != 0u) return (uint32_t)cached;
        uint32_t ff = probe(fmt, ctx);
        uint32_t val = aoi_w47_fault_mode() == 4 ? (ff ^ 1u) : ff;
        atomic_store_explicit(&c->core[fmt], (UINT64_C(1) << 63) | (uint64_t)val, memory_order_release);
        return ff;
    }
    pthread_mutex_lock(&c->ext_mu);
    for (uint32_t i = 0; i < c->ext_n && i < 64u; i++) {
        if (c->ext[i].fmt == fmt) {
            uint32_t ff = c->ext[i].ff;
            pthread_mutex_unlock(&c->ext_mu);
            return ff;
        }
    }
    uint32_t ff = probe(fmt, ctx);
    if (c->ext_n < 64u) {
        uint32_t idx = c->ext_n++;
        c->ext[idx].fmt = fmt;
        c->ext[idx].ff = ff;
    }
    pthread_mutex_unlock(&c->ext_mu);
    return ff;
}

static inline uint32_t vka_up_claim_fast(_Atomic uint32_t *next, uint32_t n) {
    if (atomic_load_explicit(next, memory_order_relaxed) >= n) return UINT32_MAX;
    uint32_t i = atomic_fetch_add_explicit(next, 1u, memory_order_relaxed);
    return i < n ? i : UINT32_MAX;
}

static inline int vka_up_finish_part_fast(
    _Atomic uint32_t *done, uint32_t n, _Atomic uint32_t *waiting,
    pthread_mutex_t *mu, pthread_cond_t *done_cv) {
    uint32_t prev = atomic_fetch_add_explicit(done, 1u, memory_order_seq_cst);
    if (prev + 1u != n) return 0;
    if (aoi_w47_fault_mode() == 5) return 0;
    if (atomic_load_explicit(waiting, memory_order_seq_cst) != 0u) {
        pthread_mutex_lock(mu);
        pthread_cond_signal(done_cv);
        pthread_mutex_unlock(mu);
    }
    return 1;
}

#endif
