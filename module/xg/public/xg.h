#pragma once

#include <std_module.h>

#include <wm.h>

/*

whats missing
    cmd buffer management/processing
    cmd buffer resource binding
    resource storage
        resource handles are returned by the allocator. all the backend needs to do
        is store those, create views, get descs
        two ypes of resources: renderer internal, and asset related. first are very rarely
        reallocated, second are allocated and released generally in block.
        might want to reflect this on the internal management? mighe be slighly more
        efficient to deallocate a block of resources rather than single picking all the
        resources in the block/level/... and updating the freelist
        what resources are in a block?
            textures
            buffers
            pipelines?
                having pipelines makes sense (shaders are level-related) but looking up
                a pipeline now becomes n lookups instead of 1
            to be honest basically all resources are level related. if resources are
            mass allocated/released, locality helps anyway even if it's not explicit
    resource allocator(s)
        see xg_vk_memory

module structure
    this module is meant to be the layer that depends on the hw api system layer and works
    as a platform independent layer for every other rendering layer. it remains close to the
    original api, although a few abstractions are taking place, mostly related to cmd buffers
    and resource binding.

*/

// -- Module --
#define xg_module_name_m xg
std_module_export_m void* xg_load ( void* );
std_module_export_m void xg_reload ( void*, void* );
std_module_export_m void xg_unload ( void );

// -- Handle types --
typedef uint64_t xg_display_h;
typedef uint64_t xg_device_h;
typedef uint64_t xg_pipeline_state_h;
typedef uint64_t xg_graphics_pipeline_state_h;
typedef uint64_t xg_compute_pipeline_state_h;
typedef uint64_t xg_resource_cmd_buffer_h;
typedef uint64_t xg_cmd_buffer_h;
typedef uint64_t xg_swapchain_h;
typedef uint64_t xg_renderpass_h;

typedef uint64_t xg_resource_h;
typedef uint64_t xg_buffer_h;
typedef uint64_t xg_texture_h;
typedef uint64_t xg_sampler_h;
typedef uint64_t xg_pipeline_resource_group_h;

typedef uint64_t xg_raytrace_geometry_h;
typedef uint64_t xg_raytrace_world_h;
typedef uint64_t xg_raytrace_pipeline_state_h;

#define xg_null_handle_m UINT64_MAX

/*
    Same underlying type, but different type name to make it clear
    which function calls expect which.

    // TODO decide what to do with this
*/
typedef xg_device_h xg_device_mask_t;

// -- Color types --
typedef enum {
    // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkFormat.html
    // e: exponent
    // d: depth
    // s: stencil
    // x: unused
    //
    xg_format_undefined_m = 0,
    // 4-4
    xg_format_r4g4_unorm_pack8_m = 1,
    // 4-4-4-4
    xg_format_r4g4b4a4_unorm_pack16_m = 2,
    xg_format_b4g4r4a4_unorm_pack16_m = 3,
    // 5-6-5
    xg_format_r5g6b5_unorm_pack16_m = 4,
    xg_format_b5g6r5_unorm_pack16_m = 5,
    // 5-5-5-1
    xg_format_r5g5b5a1_unorm_pack16_m = 6,
    xg_format_b5g5r5a1_unorm_pack16_m = 7,
    xg_format_a1r5g5b5_unorm_pack16_m = 8,
    // 8
    xg_format_r8_unorm_m = 9,
    xg_format_r8_snorm_m = 10,
    xg_format_r8_uscaled_m = 11,
    xg_format_r8_sscaled_m = 12,
    xg_format_r8_uint_m = 13,
    xg_format_r8_sint_m = 14,
    xg_format_r8_srgb_m = 15,
    // 8-8
    xg_format_r8g8_unorm_m = 16,
    xg_format_r8g8_snorm_m = 17,
    xg_format_r8g8_uscaled_m = 18,
    xg_format_r8g8_sscaled_m = 19,
    xg_format_r8g8_uint_m = 20,
    xg_format_r8g8_sint_m = 21,
    xg_format_r8g8_srgb_m = 22,
    // 8-8-8
    xg_format_r8g8b8_unorm_m = 23,
    xg_format_r8g8b8_snorm_m = 24,
    xg_format_r8g8b8_uscaled_m = 25,
    xg_format_r8g8b8_sscaled_m = 26,
    xg_format_r8g8b8_uint_m = 27,
    xg_format_r8g8b8_sint_m = 28,
    xg_format_r8g8b8_srgb_m = 29,
    xg_format_b8g8r8_unorm_m = 30,
    xg_format_b8g8r8_snorm_m = 31,
    xg_format_b8g8r8_uscaled_m = 32,
    xg_format_b8g8r8_sscaled_m = 33,
    xg_format_b8g8r8_uint_m = 34,
    xg_format_b8g8r8_sint_m = 35,
    xg_format_b8g8r8_srgb_m = 36,
    // 8-8-8-8
    xg_format_r8g8b8a8_unorm_m = 37,
    xg_format_r8g8b8a8_snorm_m = 38,
    xg_format_r8g8b8a8_uscaled_m = 39,
    xg_format_r8g8b8a8_sscaled_m = 40,
    xg_format_r8g8b8a8_uint_m = 41,
    xg_format_r8g8b8a8_sint_m = 42,
    xg_format_r8g8b8a8_srgb_m = 43,
    xg_format_b8g8r8a8_unorm_m = 44,
    xg_format_b8g8r8a8_snorm_m = 45,
    xg_format_b8g8r8a8_uscaled_m = 46,
    xg_format_b8g8r8a8_sscaled_m = 47,
    xg_format_b8g8r8a8_uint_m = 48,
    xg_format_b8g8r8a8_sint_m = 49,
    xg_format_b8g8r8a8_srgb_m = 50,
    xg_format_a8b8g8r8_unorm_pack32_m = 51,
    xg_format_a8b8g8r8_snorm_pack32_m = 52,
    xg_format_a8b8g8r8_uscaled_pack32_m = 53,
    xg_format_a8b8g8r8_sscaled_pack32_m = 54,
    xg_format_a8b8g8r8_uint_pack32_m = 55,
    xg_format_a8b8g8r8_sint_pack32_m = 56,
    xg_format_a8b8g8r8_srgb_pack32_m = 57,
    // 2-10-10-10
    xg_format_a2r10g10b10_unorm_pack32_m = 58,
    xg_format_a2r10g10b10_snorm_pack32_m = 59,
    xg_format_a2r10g10b10_uscaled_pack32_m = 60,
    xg_format_a2r10g10b10_sscaled_pack32_m = 61,
    xg_format_a2r10g10b10_uint_pack32_m = 62,
    xg_format_a2r10g10b10_sint_pack32_m = 63,
    xg_format_a2b10g10r10_unorm_pack32_m = 64,
    xg_format_a2b10g10r10_snorm_pack32_m = 65,
    xg_format_a2b10g10r10_uscaled_pack32_m = 66,
    xg_format_a2b10g10r10_sscaled_pack32_m = 67,
    xg_format_a2b10g10r10_uint_pack32_m = 68,
    xg_format_a2b10g10r10_sint_pack32_m = 69,
    // 16
    xg_format_r16_unorm_m = 70,
    xg_format_r16_snorm_m = 71,
    xg_format_r16_uscaled_m = 72,
    xg_format_r16_sscaled_m = 73,
    xg_format_r16_uint_m = 74,
    xg_format_r16_sint_m = 75,
    xg_format_r16_sfloat_m = 76,
    // 16-16
    xg_format_r16g16_unorm_m = 77,
    xg_format_r16g16_snorm_m = 78,
    xg_format_r16g16_uscaled_m = 79,
    xg_format_r16g16_sscaled_m = 80,
    xg_format_r16g16_uint_m = 81,
    xg_format_r16g16_sint_m = 82,
    xg_format_r16g16_sfloat_m = 83,
    // 16-16-16
    xg_format_r16g16b16_unorm_m = 84,
    xg_format_r16g16b16_snorm_m = 85,
    xg_format_r16g16b16_uscaled_m = 86,
    xg_format_r16g16b16_sscaled_m = 87,
    xg_format_r16g16b16_uint_m = 88,
    xg_format_r16g16b16_sint_m = 89,
    xg_format_r16g16b16_sfloat_m = 90,
    // 16-16-16-16
    xg_format_r16g16b16a16_unorm_m = 91,
    xg_format_r16g16b16a16_snorm_m = 92,
    xg_format_r16g16b16a16_uscaled_m = 93,
    xg_format_r16g16b16a16_sscaled_m = 94,
    xg_format_r16g16b16a16_uint_m = 95,
    xg_format_r16g16b16a16_sint_m = 96,
    xg_format_r16g16b16a16_sfloat_m = 97,
    // 32
    xg_format_r32_uint_m = 98,
    xg_format_r32_sint_m = 99,
    xg_format_r32_sfloat_m = 100,
    // 32-32
    xg_format_r32g32_uint_m = 101,
    xg_format_r32g32_sint_m = 102,
    xg_format_r32g32_sfloat_m = 103,
    // 32-32-32
    xg_format_r32g32b32_uint_m = 104,
    xg_format_r32g32b32_sint_m = 105,
    xg_format_r32g32b32_sfloat_m = 106,
    // 32-32-32-32
    xg_format_r32g32b32a32_uint_m = 107,
    xg_format_r32g32b32a32_sint_m = 108,
    xg_format_r32g32b32a32_sfloat_m = 109,
    // 64
    xg_format_r64_uint_m = 110,
    xg_format_r64_sint_m = 111,
    xg_format_r64_sfloat_m = 112,
    // 64-64
    xg_format_r64g64_uint_m = 113,
    xg_format_r64g64_sint_m = 114,
    xg_format_r64g64_sfloat_m = 115,
    // 64-64-64
    xg_format_r64g64b64_uint_m = 116,
    xg_format_r64g64b64_sint_m = 117,
    xg_format_r64g64b64_sfloat_m = 118,
    // 64-64-64-64
    xg_format_r64g64b64a64_uint_m = 119,
    xg_format_r64g64b64a64_sint_m = 120,
    xg_format_r64g64b64a64_sfloat_m = 121,
    // 10-11-11
    xg_format_b10g11r11_ufloat_pack32_m = 122,
    // e5-9-9-9
    xg_format_e5b9g9r9_ufloat_pack32_m = 123,
    // d16
    xg_format_d16_unorm_m = 124,
    // x8-d24
    xg_format_x8_d24_unorm_pack32_m = 125,
    // d32
    xg_format_d32_sfloat_m = 126,
    // s8
    xg_format_s8_uint_m = 127,
    // d16-s8
    xg_format_d16_unorm_s8_uint_m = 128,
    // d24-s8
    xg_format_d24_unorm_s8_uint_m = 129,
    // d32-s8-24x
    xg_format_d32_sfloat_s8_uint_m = 130,
    // https://docs.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11
    //
    // 64 (4x4 8-8-8)
    xg_format_bc1_rgb_unorm_block_m = 131,
    xg_format_bc1_rgb_srgb_block_m = 132,
    xg_format_bc1_rgba_unorm_block_m = 133,
    xg_format_bc1_rgba_srgb_block_m = 134,
    // 128 (4x4 8-8-8-8)
    xg_format_bc2_unorm_block_m = 135,
    xg_format_bc2_srgb_block_m = 136,
    // 128 (4x4 8-8-8-8)
    xg_format_bc3_unorm_block_m = 137,
    xg_format_bc3_srgb_block_m = 138,
    // 64 (4x4 8)
    xg_format_bc4_unorm_block_m = 139,
    xg_format_bc4_snorm_block_m = 140,
    // 128 (4x4 8-8)
    xg_format_bc5_unorm_block_m = 141,
    xg_format_bc5_snorm_block_m = 142,
    // 128 (4x4 16-16-16)
    xg_format_bc6h_ufloat_block_m = 143,
    xg_format_bc6h_sfloat_block_m = 144,
    // 128 (4x4 8-8-8-8)
    xg_format_bc7_unorm_block_m = 145,
    xg_format_bc7_srgb_block_m = 146,
    // https://vulkan.lunarg.com/doc/view/1.0.26.0/linux/vkspec.chunked/apbs02.html
    //
    // 64 (4x4 8-8-8)
    xg_format_etc2_r8g8b8_unorm_block_m = 147,
    xg_format_etc2_r8g8b8_srgb_block_m = 148,
    xg_format_etc2_r8g8b8a1_unorm_block_m = 149,
    xg_format_etc2_r8g8b8a1_srgb_block_m = 150,
    // 128 (4x4 8-8-8-8)
    xg_format_etc2_r8g8b8a8_unorm_block_m = 151,
    xg_format_etc2_r8g8b8a8_srgb_block_m = 152,
    // 64 (4x4 8)
    xg_format_eac_r11_unorm_block_m = 153,
    xg_format_eac_r11_snorm_block_m = 154,
    // 128 (4x4 8-8)
    xg_format_eac_r11g11_unorm_block_m = 155,
    xg_format_eac_r11g11_snorm_block_m = 156,
    // https://vulkan.lunarg.com/doc/view/1.0.30.0/linux/vkspec.chunked/apbs03.html
    //
    xg_format_astc_4_mx4_UNORM_BLOCK = 157,
    xg_format_astc_4_mx4_SRGB_BLOCK = 158,
    xg_format_astc_5_mx4_UNORM_BLOCK = 159,
    xg_format_astc_5_mx4_SRGB_BLOCK = 160,
    xg_format_astc_5_mx5_UNORM_BLOCK = 161,
    xg_format_astc_5_mx5_SRGB_BLOCK = 162,
    xg_format_astc_6_mx5_UNORM_BLOCK = 163,
    xg_format_astc_6_mx5_SRGB_BLOCK = 164,
    xg_format_astc_6_mx6_UNORM_BLOCK = 165,
    xg_format_astc_6_mx6_SRGB_BLOCK = 166,
    xg_format_astc_8_mx5_UNORM_BLOCK = 167,
    xg_format_astc_8_mx5_SRGB_BLOCK = 168,
    xg_format_astc_8_mx6_UNORM_BLOCK = 169,
    xg_format_astc_8_mx6_SRGB_BLOCK = 170,
    xg_format_astc_8_mx8_UNORM_BLOCK = 171,
    xg_format_astc_8_mx8_SRGB_BLOCK = 172,
    xg_format_astc_10_mx5_UNORM_BLOCK = 173,
    xg_format_astc_10_mx5_SRGB_BLOCK = 174,
    xg_format_astc_10_mx6_UNORM_BLOCK = 175,
    xg_format_astc_10_mx6_SRGB_BLOCK = 176,
    xg_format_astc_10_mx8_UNORM_BLOCK = 177,
    xg_format_astc_10_mx8_SRGB_BLOCK = 178,
    xg_format_astc_10_mx10_UNORM_BLOCK = 179,
    xg_format_astc_10_mx10_SRGB_BLOCK = 180,
    xg_format_astc_12_mx10_UNORM_BLOCK = 181,
    xg_format_astc_12_mx10_SRGB_BLOCK = 182,
    xg_format_astc_12_mx12_UNORM_BLOCK = 183,
    xg_format_astc_12_mx12_SRGB_BLOCK = 184,
    //
    xg_format_count_m
} xg_format_e;

typedef enum {
    xg_colorspace_linear_m,
    xg_colorspace_srgb_m,     // https://en.wikipedia.org/wiki/SRGB
    xg_colorspace_hdr_hlg_m,  // https://en.wikipedia.org/wiki/Hybrid_Log-Gamma
    xg_colorspace_hdr_pq_m,   // https://en.wikipedia.org/wiki/High-dynamic-range_video#Perceptual_Quantizer
} xg_color_space_e;

// -- Device --
typedef enum {
    xg_device_unknown_m,
    xg_device_discrete_m,
    xg_device_integrated_m,
    xg_device_virtual_m,
    xg_device_cpu_m
} xg_device_type_e;

typedef struct {
    uint32_t api_version;
    uint32_t driver_version;
    uint32_t vendor_id;
    uint32_t product_id;
    xg_device_type_e type;
    char name[xg_device_name_size_m];
    bool dedicated_compute_queue;
    bool dedicated_copy_queue;
    bool supports_raytrace;
    uint64_t uniform_buffer_alignment;
} xg_device_info_t;

// see xg_instance_enabled_runtime_layers_m
typedef enum {
    xg_runtime_layer_bit_none_m       =      0,
    xg_runtime_layer_bit_debug_m      = 1 << 0,
    xg_runtime_layer_bit_renderdoc_m  = 1 << 1,
} xg_runtime_layer_bit_e;

// -- Display --
// TODO is this working?
typedef struct {
    uint64_t id;
    size_t width;
    size_t height;
    size_t refresh_rate;
} xg_display_mode_t;

typedef struct {
    xg_display_h display_handle;
    size_t pixel_width;
    size_t pixel_height;
    size_t millimeter_width;
    size_t millimeter_height;
    char name[xg_display_name_size_m];
    xg_display_mode_t display_modes[xg_display_max_modes_m];
} xg_display_info_t;

// -- Swapchain --
typedef enum {
    xg_present_mode_immediate_m,        // no wait for vblank, tear
    xg_present_mode_mailbox_m,          // wait for vblank, single slot fifo
    xg_present_mode_fifo_m,             // wait for vblank, queue to prev requests
    xg_present_mode_fifo_relaxed_m,     // wait for vblank, tear if prev vblank was missed
} xg_present_mode_e;

typedef struct {
    bool acquired;
    uint32_t acquired_texture_idx;
    size_t texture_count;
    xg_texture_h textures[xg_swapchain_max_textures_m];
    xg_format_e format;
    xg_color_space_e color_space;
    size_t width;
    size_t height;
    xg_present_mode_e present_mode;
    xg_device_h device;
    wm_window_h window;
    xg_display_h display;
    char debug_name[xg_debug_name_size_m];
} xg_swapchain_info_t;

typedef struct {
    size_t texture_count;
    xg_format_e format;
    xg_color_space_e color_space;
    xg_present_mode_e present_mode;
    xg_device_h device;
    wm_window_h window;
    char debug_name[xg_debug_name_size_m];
} xg_swapchain_window_params_t;

#define xg_swapchain_window_params_m( ... ) ( xg_swapchain_window_params_t ) { \
    .texture_count = 0, \
    .format = xg_format_undefined_m, \
    .color_space = xg_colorspace_srgb_m, \
    .present_mode = xg_present_mode_fifo_m, \
    .debug_name = "swapchain", \
    .device = xg_null_handle_m, \
    .window = wm_null_handle_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    size_t texture_count;
    xg_format_e format;
    xg_color_space_e color_space;
    xg_present_mode_e present_mode;
    xg_device_h device;
    xg_display_h display;
    char debug_name[xg_debug_name_size_m];
} xg_swapchain_display_params_t;

typedef struct {
    size_t texture_count;
    xg_format_e format;
    xg_color_space_e color_space;
    size_t width;
    size_t height;
    xg_device_h device;
    char debug_name[xg_debug_name_size_m];
} xg_swapchain_virtual_params_t;

// -- Device Memory --
/*
    We keep memory management explicit to the user. The backend only targets modern apis.
    GPU Allocators work much like cpu allocators. An allocator is required as parameter
    every time a resource is created.
*/

// TODO remove allocators interface entirely, just pass memory type enum to resource creation api?

typedef struct {
    uint64_t id;
    uint64_t size : 56;
    uint64_t device : 4; // TODO use log2 macro on xg_max_active_devices_m
    uint64_t type :  4;
} xg_memory_h;

// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkMemoryPropertyFlagBits.html
// xg_memory_type_device_m tells whether the memory is resident on GPU or CPU (host)
// xg_memory_type_mappable_m can only be set if memory is on GPU and tells whether that memory can be memmapped by the host
// xg_memory_type_cached_m tells whether the memory gets cached on the host.
// xg_memory_type_coherent_m tells whether the memory is coherent.
// Non-coherent memory means the host needs to manually flush its writes for the device to be able to see them, and also needs to manually invalidate its own memory in order to see device writes.
// All current desktop retail GPUs are also coherent when host-visible. Coherent cached memory is good for readback/staging download from GPU. Coherent only memory is good for staging upload to GPU.
// TODO does non-coherent memory mean write-combine? e.g. a read after a write on host doesn't return the written value? https://fgiesen.wordpress.com/2013/01/29/write-combining-is-not-your-friend/
typedef enum {
    xg_memory_type_bit_device_m   = 1 << 0,
    xg_memory_type_bit_mappable_m = 1 << 1,
    xg_memory_type_bit_cached_m   = 1 << 2,
    xg_memory_type_bit_coherent_m = 1 << 3,
    //xg_memory_type_for_gpu_only_m = xg_memory_type_device_m,
    //xg_memory_type_for_dynamic_data_m = xg_memory_type_device_m | xg_memory_type_mappable_m | xg_memory_type_coherent_m,
    //xg_memory_type_for_upload_m = xg_memory_type_mappable_m | xg_memory_type_coherent_m,
    //xg_memory_type_for_readback_m = xg_memory_type_mappable_m | xg_memory_type_coherent_m | xg_memory_type_cached_m
} xg_memory_flag_bit_e;
/*
    Handle is internal to the allocator only, it's unique per valid xg_alloc_t but (usually) has no meaning to the backend API
    Base is the base gpu memory address of the allocation
    In vulkan (TODO d3d12?) this is also the backend API object result of the backend API function call
    Offset is relative to Base, and two xg_alloc_t result of two different xg_alloc call can have same Base but different Offset fields
        This tipically translates in one underlying API gpu memory chunk being shared between two xg_alloc_t
    Device and Flags are there to provide additional info
*/
typedef struct {
    xg_memory_h handle;
    uint64_t base;
    uint64_t offset;
    uint64_t size;
    void* mapped_address;
    xg_memory_flag_bit_e flags;
    xg_device_h device;
} xg_alloc_t;

#define xg_null_memory_handle_m ( xg_memory_h ) { -1, -1, -1, -1  }
#define xg_memory_handle_is_null_m( handle ) ( handle.id == UINT64_MAX || handle.size == UINT64_MAX )

#define xg_null_alloc_m ( xg_alloc_t ) { \
    .handle = xg_null_memory_handle_m, \
    .base = 0, \
    .offset = 0, \
    .size = 0, \
    .mapped_address = NULL, \
    .flags = 0, \
    .device = xg_null_handle_m \
}

#if 0
// TODO remove completely, just have an alloc/free main API that takes in an enum for the mem type,
// similar to get_default_allocator but does the alloc/free directly. can then store the mem type into the handle
// Storing function pointers like this causes crashes when hot reloading the xg dll
typedef xg_alloc_t ( xg_alloc_f ) ( void* allocator, xg_device_h device, size_t size, size_t alignment );
typedef void ( xg_free_f ) ( void* allocator, xg_memory_h mem );

typedef struct {
    void* impl;
    xg_alloc_f* alloc;
    xg_free_f* free;
} xg_allocator_i;

#define xg_null_allocator_m (xg_allocator_i) { NULL, NULL, NULL }
#define xg_allocator_is_null_m( allocator ) ( allocator.alloc == NULL || allocator.free == NULL )
#endif

typedef enum {
    xg_memory_type_gpu_only_m,
    xg_memory_type_gpu_mappable_m,
    xg_memory_type_upload_m,
    xg_memory_type_readback_m,
    xg_memory_type_count_m,
    xg_memory_type_null_m = xg_memory_type_count_m
} xg_memory_type_e;

typedef struct {
    uint64_t allocated_size;
    uint64_t reserved_size;
    uint64_t system_size;
} xg_allocator_info_t;

// -- Input Layout --
typedef enum {
    xg_vertex_attribute_pos_m,
    xg_vertex_attribute_nor_m,
    xg_vertex_attribute_tan_m,
    xg_vertex_attribute_uv_m,
    xg_vertex_attribute_color_m,
} xg_vertex_attribute_e;

typedef struct {
    xg_vertex_attribute_e   name;
    xg_format_e             format;
    size_t                  id;
} xg_vertex_attribute_t;

typedef struct {
    size_t                  id;
    size_t                  attribute_count;
    xg_vertex_attribute_t   attributes[xg_input_layout_stream_max_attributes_m];
} xg_vertex_stream_t;

typedef struct {
    size_t                  stream_count;
    xg_vertex_stream_t      streams[xg_input_layout_max_streams_m];
} xg_input_layout_t;

#define xg_input_layout_m(...) ( xg_input_layout_t ) { \
    .stream_count = 0, \
    .streams = { 0 } \
    ##__VA_ARGS__ \
}

// -- Rasterizer State --
typedef enum {
    xg_polygon_mode_fill_m,
    xg_polygon_mode_line_m,
    xg_polygon_mode_point_m,
} xg_polygon_mode_e;

typedef enum {
    xg_cull_mode_bit_none_m   = 0,
    xg_cull_mode_bit_front_m  = 1 << 0,
    xg_cull_mode_bit_back_m   = 1 << 1,
} xg_cull_mode_bit_e;

typedef enum {
    xg_winding_clockwise_m,
    xg_winding_counter_clockwise_m,
} xg_winding_mode_e;

typedef enum {
    xg_antialiasing_msaa_m,
    xg_antialiasing_ssaa_m,
    xg_antialiasing_none_m,
} xg_antialiasing_mode_e;

typedef enum {
    xg_sample_count_1_m,
    xg_sample_count_2_m,
    xg_sample_count_4_m,
    xg_sample_count_8_m,
    xg_sample_count_16_m,
    xg_sample_count_32_m,
    xg_sample_count_64_m,
} xg_sample_count_e;

typedef struct {
    xg_antialiasing_mode_e  mode;
    xg_sample_count_e       sample_count;
} xg_antialiasing_state_t;

#define xg_antialiasing_state_m( ... ) ( xg_antialiasing_state_t ) { \
    .mode = xg_antialiasing_none_m, \
    .sample_count = xg_sample_count_1_m, \
    ##__VA_ARGS__ \
}

// https://vulkan.lunarg.com/doc/view/1.0.37.0/linux/vkspec.chunked/ch24s07.html
// m = max ( dz / dx, dz / dy )
// r: minimum representable value in the depth buffer, e.g. r = 1 / 2^24 for a 24 bit depth buffer
// if clamp == 0, bias = m * slope_factor + r * const_factor
// if clamp > 0,  bias = max(clamp, m * slope_factor + r * const_factor)
// if clamp < 0,  bias = min(clamp, m * slope_factor + r * const_factor)
typedef struct {
    bool    enable;
    float   const_factor;
    float   slope_factor;
    float   clamp;
} xg_depth_bias_state_t;

#define xg_depth_bias_state_m( ... ) { \
    .enable = false, \
    .const_factor = 0, \
    .slope_factor = 0, \
    .clamp = 0 \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_polygon_mode_e       polygon_mode;
    xg_cull_mode_bit_e      cull_mode;
    xg_winding_mode_e       frontface_winding_mode;
    xg_antialiasing_state_t antialiasing_state;
    xg_depth_bias_state_t   depth_bias_state;
    float                   line_width;
    bool                    enable_depth_clamp;    // Clamp on the Z axis instead of clipping using clip planes
    bool                    disable_rasterization;
} xg_rasterizer_state_t;

#define xg_rasterizer_state_m( ... ) ( xg_rasterizer_state_t ) { \
    .polygon_mode = xg_polygon_mode_fill_m, \
    .cull_mode = xg_cull_mode_bit_back_m, \
    .frontface_winding_mode = xg_winding_clockwise_m, \
    .antialiasing_state = xg_antialiasing_state_m(), \
    .depth_bias_state = xg_depth_bias_state_m(), \
    .line_width = 1.f, \
    .enable_depth_clamp = false, \
    .disable_rasterization = false, \
    ##__VA_ARGS__ \
}

// -- Depth Stencil State --
typedef enum {
    xg_compare_op_never_m,
    xg_compare_op_less_m,
    xg_compare_op_equal_m,
    xg_compare_op_less_or_equal_m,
    xg_compare_op_greater_m,
    xg_compare_op_not_equal_m,
    xg_compare_op_greater_or_equal_m,
    xg_compare_op_always_m,
} xg_compare_op_e;

typedef enum {
    xg_stencil_op_keep_m,
    xg_stencil_op_zero_m,
    xg_stencil_op_replace_m,
    xg_stencil_op_inc_and_clamp_m,
    xg_stencil_op_dec_and_clamp_m,
    xg_stencil_op_invert_m,
    xg_stencil_op_inc_and_wrap_m,
    xg_stencil_op_dec_and_wrap_m,
} xg_stencil_op_e;

typedef struct {
    xg_stencil_op_e stencil_fail_op;
    xg_stencil_op_e stencil_depth_pass_op;
    xg_stencil_op_e stencil_pass_depth_fail_op;
    xg_compare_op_e compare_op;
    uint32_t compare_mask;
    uint32_t write_mask;
    uint32_t reference;
} xg_stencil_op_state_t;

#define xg_stencil_op_state_m( ... ) ( xg_stencil_op_state_t ) { \
    .stencil_fail_op = xg_stencil_op_zero_m, \
    .stencil_depth_pass_op = xg_stencil_op_zero_m, \
    .stencil_pass_depth_fail_op = xg_stencil_op_zero_m, \
    .compare_op = xg_compare_op_never_m, \
    .compare_mask = 0, \
    .write_mask = 0, \
    .reference = 0 \
    ##__VA_ARGS__ \
}

typedef struct {
    bool enable_test;
    xg_stencil_op_state_t front_face_op;
    xg_stencil_op_state_t back_face_op;
} xg_stencil_state_t;

#define xg_stencil_state_m( ... ) ( xg_stencil_state_t ) { \
    .enable_test = false, \
    .front_face_op = xg_stencil_op_state_m(), \
    .back_face_op = xg_stencil_op_state_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    float min_bound;
    float max_bound;
    bool enable_bound_test;
    xg_compare_op_e compare_op;
    bool enable_test;
    bool enable_write;
} xg_depth_state_t;

#define xg_depth_state_m( ... ) ( xg_depth_state_t ) { \
    .min_bound = 0, \
    .max_bound = 1, \
    .enable_bound_test = false, \
    .compare_op = xg_compare_op_less_m, \
    .enable_test = false, \
    .enable_write = false \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_depth_state_t depth;
    xg_stencil_state_t stencil;
} xg_depth_stencil_state_t;

#define xg_depth_stencil_state_m( ... ) ( xg_depth_stencil_state_t ) { \
    .depth = xg_depth_state_m(), \
    .stencil = xg_stencil_state_m() \
    ##__VA_ARGS__ \
}

// -- Blend State --
typedef enum {
    xg_blend_factor_zero_m,
    xg_blend_factor_one_m,
    xg_blend_factor_src_color_m,
    xg_blend_factor_one_minus_src_color_m,
    xg_blend_factor_dst_color_m,
    xg_blend_factor_one_minus_dst_color_m,
    xg_blend_factor_src_alpha_m,
    xg_blend_factor_one_minus_src_alpha_m,
    xg_blend_factor_dst_alpha_m,
    xg_blend_factor_one_minus_dst_alpha_m,
} xg_blend_factor_e;

typedef enum {
    xg_blend_op_add_m,
    xg_blend_op_subtract_m,
    xg_blend_op_reverse_subtract_m,
    xg_blend_op_min_m,
    xg_blend_op_max_m,
} xg_blend_op_e;

typedef struct {
    size_t id;
    bool enable_blend;
    xg_blend_factor_e color_src;
    xg_blend_factor_e color_dst;
    xg_blend_op_e color_op;
    xg_blend_factor_e alpha_src;
    xg_blend_factor_e alpha_dst;
    xg_blend_op_e alpha_op;
} xg_render_target_blend_state_t;

// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkLogicOp.html
typedef enum {
    xg_blend_logic_op_clear_m,
    xg_blend_logic_op_and_m,
    xg_blend_logic_op_and_reverse_m,
    xg_blend_logic_op_copy_m,
    xg_blend_logic_op_and_inverted_m,
    xg_blend_logic_op_no_op_m,
    xg_blend_logic_op_xor_m,
    xg_blend_logic_op_or_m,
    xg_blend_logic_op_nor_m,
    xg_blend_logic_op_equivalent_m,
    xg_blend_logic_op_invert_m,
    xg_blend_logic_op_or_reverse_m,
    xg_blend_logic_op_copy_inverted_m,
    xg_blend_logic_op_or_inverted_m,
    xg_blend_logic_op_nand_m,
    xg_blend_logic_op_set_m,
    xg_blend_logic_op_invalid_m,
} xg_blend_logic_op_e;

typedef struct {
    size_t render_targets_count;
    xg_render_target_blend_state_t render_targets[xg_pipeline_output_max_color_targets_m];
    xg_blend_logic_op_e blend_logic_op;
    bool enable_blend_logic_op;
} xg_blend_state_t;

#define xg_blend_state_m( ... ) ( xg_blend_state_t ) { \
    .render_targets_count = 0, \
    .render_targets = { 0 }, \
    .blend_logic_op = xg_blend_logic_op_invalid_m, \
    .enable_blend_logic_op = false, \
    ##__VA_ARGS__ \
}

// -- Viewport --
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float min_depth;
    float max_depth;
} xg_viewport_state_t;

#define xg_viewport_state_m( ... ) ( xg_viewport_state_t ) { \
    .x = 0, \
    .y = 0, \
    .width = 0, \
    .height = 0, \
    .min_depth = 0, \
    .max_depth = 1, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} xg_scissor_state_t;

#define xi_scissor_width_full_m 0xffffffff
#define xi_scissor_height_full_m 0xffffffff

#define xg_scissor_state_m( ... ) ( xg_scissor_state_t ) { \
    .x = 0, \
    .y = 0, \
    .width = 0xffffffff, \
    .height = 0xffffffff, \
    ##__VA_ARGS__ \
}

// -- Render Textures (attachments) --

typedef union {
    uint64_t u64;
    struct {
        uint64_t mip_base       :  8;
        uint64_t mip_count      :  8;
        uint64_t array_base     : 16;
        uint64_t array_count    : 16;
        uint64_t format         : 13; // xg_format_e - can shave off another 3-5 bits if needed
        uint64_t aspect         :  3; // xg_texture_aspect_e
    };
} xg_texture_view_t;

#define xg_texture_all_mips_m     UINT8_MAX
// TODO rename to ALL_SLICES?
#define xg_texture_whole_array_m  UINT16_MAX

// Will automatically fallback to the texture creation format
#define xg_texture_view_default_format_m (xg_format_count_m + 1)

#define xg_texture_view_m( ... ) ( xg_texture_view_t ) { \
    .format = xg_texture_view_default_format_m, \
    .aspect = xg_texture_aspect_default_m, \
    .mip_base = 0, \
    .mip_count = xg_texture_all_mips_m, \
    .array_base = 0, \
    .array_count = xg_texture_whole_array_m, \
    ##__VA_ARGS__ \
}
#define xg_default_texture_view_m ( xg_texture_view_m() )

typedef struct {
    size_t slot;
    xg_format_e format;
    xg_sample_count_e samples_per_pixel;
} xg_render_target_layout_t;

typedef struct {
    xg_format_e format;
    xg_sample_count_e samples_per_pixel;
} xg_depth_stencil_layout_t;

typedef struct {
    size_t                      render_targets_count;
    xg_render_target_layout_t   render_targets[xg_pipeline_output_max_color_targets_m];
    bool                        depth_stencil_enabled;
    xg_depth_stencil_layout_t   depth_stencil;
    // TODO other attachments?
} xg_render_textures_layout_t;

#define xg_render_textures_layout_m( ... ) ( xg_render_textures_layout_t ) { \
    .render_targets_count = 0, \
    .render_targets = { 0 }, \
    .depth_stencil_enabled = false, \
    .depth_stencil = { 0 } \
    ##__VA_ARGS__ \
}

typedef union {
    float    f32[4];
    int32_t  i32[4];
    uint32_t u32[4];
} xg_color_clear_t;

#define xg_color_clear_m( ... ) ( xg_color_clear_t ) { \
    .u32 = {0, 0, 0, 0}, \
    __VA_ARGS__ \
}

typedef struct {
    //size_t id;
    // TODO currently the clear value is ignored! all rendertarget attachemnts use VK_ATTACHMENT_LOAD_OP_LOAD, look at xg_vk_pipeline.c
    //      https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkRenderPassBeginInfo.html
    //xg_color_clear_t clear;
    xg_texture_h texture;
    xg_texture_view_t view;
} xg_render_target_binding_t;

#define xg_render_target_binding_m( ... ) ( xg_render_target_binding_t ) { \
    .texture = xg_null_handle_m, \
    .view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    float    depth;
    uint32_t stencil;
} xg_depth_stencil_clear_t;

#define xg_depth_clear_regular_m 1.f
#define xg_depth_clear_reverse_m 0.f

#define xg_depth_stencil_clear_m( ... ) ( xg_depth_stencil_clear_t ) { \
    .depth = xg_depth_clear_regular_m, \
    .stencil = 0, \
    __VA_ARGS__ \
}

typedef struct {
    //xg_depth_stencil_clear_t clear;
    xg_texture_h texture;
} xg_depth_stencil_binding_t;

// depends only on attachments
typedef struct {
    size_t                      render_targets_count;
    xg_render_target_binding_t  render_targets[xg_pipeline_output_max_color_targets_m];
    xg_depth_stencil_binding_t  depth_stencil;
    // TODO other attachments?
} xg_render_textures_binding_t;

#define xg_render_textures_binding_m( ... ) ( xg_render_textures_binding_t ) { \
    .render_targets_count = 0, \
    .render_targets = { [0 ... xg_pipeline_output_max_color_targets_m-1] = xg_render_target_binding_m() }, \
    .depth_stencil = { .texture = xg_null_handle_m }, \
    ##__VA_ARGS__ \
}

// -- Resource Bindings --
typedef enum {
    // graphics
    xg_shading_stage_vertex_m,
    xg_shading_stage_fragment_m,
    // compute
    xg_shading_stage_compute_m,
    // raytrace
    xg_shading_stage_ray_gen_m,
    xg_shading_stage_ray_miss_m,
    xg_shading_stage_ray_hit_closest_m,
    //xg_shading_stage_ray_hit_any_m,
    //xg_shading_stage_ray_intersection_m,
    //
    xg_shading_stage_count_m,
    xg_shading_stage_null_m
} xg_shading_stage_e;

typedef enum {
    // graphics
    xg_shading_stage_bit_vertex_m           = 1 << xg_shading_stage_vertex_m,
    xg_shading_stage_bit_fragment_m         = 1 << xg_shading_stage_fragment_m,
    // compute
    xg_shading_stage_bit_compute_m          = 1 << xg_shading_stage_compute_m,
    // raytrace
    xg_shading_stage_bit_ray_gen_m          = 1 << xg_shading_stage_ray_gen_m,
    xg_shading_stage_bit_ray_miss_m         = 1 << xg_shading_stage_ray_miss_m,
    xg_shading_stage_bit_ray_hit_closest_m  = 1 << xg_shading_stage_ray_hit_closest_m,
    //xg_shading_stage_bit_ray_hit_any_m      = 1 << xg_shading_stage_ray_hit_any_m,
    //xg_shading_stage_bit_ray_intersection_m = 1 << xg_shading_stage_ray_intersection_m,
} xg_shading_stage_bit_e;

#define xg_shading_stage_enum_to_bit_m( stage ) ( 1 << ( stage ) )

// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkDescriptorType.html
typedef enum {
    xg_resource_binding_sampler_m,                      // VK_DESCRIPTOR_TYPE_SAMPLER - External texture sampler
    xg_resource_binding_texture_to_sample_m,            // VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE - A texture that can/will be sampled in the shader using an external sampler
    //xg_resource_binding_texture_and_sampler_m,        // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER - A sampled texture and sampler pairing, the texture will get sampled using this sampler only - Useless for now(?), just bind an external sampler
    xg_resource_binding_texture_storage_m,              // VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
    //xg_resource_binding_pipeline_output_m,            // VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT - Render target / depth stencil - Useless for now, as out only attachment is the render target.
    xg_resource_binding_buffer_uniform_m,               // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    xg_resource_binding_buffer_storage_m,               // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    xg_resource_binding_buffer_texel_uniform_m,         // VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
    xg_resource_binding_buffer_texel_storage_m,         // VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
    xg_resource_binding_raytrace_world_m,               // VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR / VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
    xg_resource_binding_count_m
    // TODO? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
} xg_resource_binding_e;

// TODO rename this to xg_resource_binding_update_frequency_e or xg_resource_binding_usage_e or xg_resource_binding_slot_e
// TODO reduce the number of these? remove per_shader?
typedef enum {
    xg_resource_binding_set_per_frame_m,
    xg_resource_binding_set_per_view_m,
    xg_resource_binding_set_per_material_m,
    xg_resource_binding_set_per_draw_m,
    xg_resource_binding_set_count_m,
    xg_resource_binding_set_invalid_m
} xg_resource_binding_set_e;

typedef struct {
    uint32_t shader_register;
    xg_shading_stage_bit_e stages;
    xg_resource_binding_e type;
    xg_resource_binding_set_e set;
} xg_resource_binding_layout_t;

typedef struct {
    xg_shading_stage_bit_e stages;
    uint32_t size;
    uint32_t id;
} xg_constant_binding_layout_t;

typedef struct {
    xg_resource_binding_layout_t binding_points[xg_pipeline_resource_max_bindings_m];
    size_t binding_points_count;
} xg_resource_bindings_layout_t;

typedef struct {
    xg_constant_binding_layout_t binding_points[xg_pipeline_constant_max_bindings_m];
    uint32_t binding_points_count;
} xg_constant_bindings_layout_t;

#define xg_resource_bindings_layout_m( ... ) ( xg_resource_bindings_layout_t ) { \
    .binding_points = { 0 }, \
    .binding_points_count = 0, \
    ##__VA_ARGS__ \
}

#define xg_constant_bindings_layout_m( ... ) ( xg_constant_bindings_layout_t ) { \
    .binding_points = { 0 }, \
    .binding_points_count = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    bool enable;
    uint64_t hash;
    std_buffer_t buffer;
} xg_pipeline_state_shader_t;

#define xg_pipeline_state_shader_m(...) ( xg_pipeline_state_shader_t ) { \
    .enable = false, \
    .hash = 0, \
    .buffer = std_null_buffer_m, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_graphics_pipeline_dynamic_state_viewport_m,
    xg_graphics_pipeline_dynamic_state_scissor_m,
    xg_graphics_pipeline_dynamic_state_count_m,
} xg_graphics_pipeline_dynamic_state_e;

typedef struct {
    // TODO bitset?
    bool enabled_states[xg_graphics_pipeline_dynamic_state_count_m];
} xg_graphics_pipeline_dynamic_state_t;

#define xg_graphics_pipeline_dynamic_state_m( ... ) ( xg_graphics_pipeline_dynamic_state_t ) { \
    .enabled_states = { [0 ... xg_graphics_pipeline_dynamic_state_count_m-1] = false }, \
}

// -- Graphics Pipeline State --
// depends on both bindings and attachments
// TODO rename to raster_pipeline?
typedef struct {
    xg_pipeline_state_shader_t              vertex_shader;
    xg_pipeline_state_shader_t              fragment_shader;

    xg_input_layout_t                       input_layout;
    xg_rasterizer_state_t                   rasterizer_state;
    xg_depth_stencil_state_t                depth_stencil_state;
    xg_blend_state_t                        blend_state;
    xg_viewport_state_t                     viewport_state;
    xg_scissor_state_t                      scissor_state;
    xg_graphics_pipeline_dynamic_state_t    dynamic_state;
} xg_graphics_pipeline_state_t;

#define xg_graphics_pipeline_state_m( ... ) ( xg_graphics_pipeline_state_t ) { \
    .vertex_shader = xg_pipeline_state_shader_m(), \
    .fragment_shader = xg_pipeline_state_shader_m(), \
    .input_layout = xg_input_layout_m(), \
    .rasterizer_state = xg_rasterizer_state_m(), \
    .depth_stencil_state = xg_depth_stencil_state_m(), \
    .blend_state = xg_blend_state_m(), \
    .viewport_state = xg_viewport_state_m(), \
    .scissor_state = xg_scissor_state_m(), \
    .dynamic_state = xg_graphics_pipeline_dynamic_state_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_graphics_pipeline_state_t    state;
    xg_render_textures_layout_t     render_textures; // TOOD rename in render_textures_layout?
    xg_resource_bindings_layout_t   resource_bindings; // TODO rename in resource_layout. etc.. below
    xg_constant_bindings_layout_t   constant_bindings;
    char                            debug_name[xg_debug_name_size_m];
} xg_graphics_pipeline_params_t;

#define xg_graphics_pipeline_params_m( ... ) ( xg_graphics_pipeline_params_t ) { \
    .state = xg_graphics_pipeline_state_m(), \
    .render_textures = xg_render_textures_layout_m(), \
    .resource_bindings = xg_resource_bindings_layout_m(), \
    .constant_bindings = xg_constant_bindings_layout_m(), \
    .debug_name = { 0 }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_pipeline_state_shader_t      compute_shader;
} xg_compute_pipeline_state_t;

#define xg_compute_pipeline_state_m( ... ) ( xg_compute_pipeline_state_t ) { \
    .compute_shader = xg_pipeline_state_shader_m(), \
}

typedef struct {
    xg_compute_pipeline_state_t     state;
    xg_resource_bindings_layout_t   resource_bindings;
    xg_constant_bindings_layout_t   constant_bindings;
    char                            debug_name[xg_debug_name_size_m];
} xg_compute_pipeline_params_t;

#define xg_compute_pipeline_params_m( ... ) ( xg_compute_pipeline_params_t ) { \
    .state = xg_compute_pipeline_state_m(), \
    .resource_bindings = xg_resource_bindings_layout_m(), \
    .constant_bindings = xg_constant_bindings_layout_m(), \
    .debug_name = { 0 }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_pipeline_state_shader_t ray_gen_shader;
    xg_pipeline_state_shader_t ray_hit_closest_shader;
    xg_pipeline_state_shader_t ray_miss_shader;
    //xg_pipeline_state_shader_t ray_intersection_shader;
    //xg_pipeline_state_shader_t ray_any_hit_shader;
} xg_raytrace_pipeline_state_t;

#define xg_raytrace_pipeline_state_m( ... ) ( xg_raytrace_pipeline_state_t ) { \
    .ray_gen_shader = xg_pipeline_state_shader_m(), \
    .ray_miss_shader = xg_pipeline_state_shader_m(), \
    .ray_hit_closest_shader = xg_pipeline_state_shader_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_raytrace_pipeline_state_t    state;
    xg_resource_bindings_layout_t   resource_bindings;
    xg_constant_bindings_layout_t   constant_bindings;
    char                            debug_name[xg_debug_name_size_m];
} xg_raytrace_pipeline_params_t;

#define xg_raytrace_pipeline_params_m( ... ) ( xg_raytrace_pipeline_params_t ) { \
    .state = xg_raytrace_pipeline_state_m(), \
    .resource_bindings = xg_resource_bindings_layout_m(), \
    .constant_bindings = xg_constant_bindings_layout_m(), \
    .debug_name = { 0 }, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_pipeline_graphics_m,
    xg_pipeline_compute_m,
    xg_pipeline_raytrace_m,
} xg_pipeline_e;

// -- Renderpass --

typedef struct {
    xg_device_h device;
    xg_render_textures_layout_t render_textures;
    uint32_t resolution_x;
    uint32_t resolution_y;
    char debug_name[xg_debug_name_size_m];
} xg_graphics_renderpass_params_t;

#define xg_graphics_renderpass_params_m( ... ) ( xg_graphics_renderpass_params_t ) { \
    .device = xg_null_handle_m, \
    .render_textures = xg_render_textures_layout_m(), \
    .resolution_x = -1, \
    .resolution_y = -1, \
    .debug_name = "", \
    ##__VA_ARGS__ \
}

// -- Resource Management --
// TODO simplyfy/abstract a bit the below?
// Where not specified, a usage encompasses both pipeline output and pipeline resource usage
/*
    ** TODO make sure all of what is said here is actually happening **

    Texture layouts are used to specify a physical layout for the data associated with a texture.
    Layouts are aexclusive, meaning that only one layout can be active at a time.

    Texture usages are used to specify all the possible ways a texture will be used in the future.
    Multiple usages can and ofter are specified for a single texture.
    In practice, some of those usages are exclusive with each other, meaning that one cannot
    always use a texture in any two different ways at the same time. For example, a textrure cannot
    both be a pipeline resource and a color output. If the runtime detects that a texture is
    being used in those two ways at the same time, an error will be thrown (TODO more info on this). Moreover,
    some usages are always incompatible. For example, a texture can never be a color output and
    a depth stencil output, not even at different times. In this case, an error will be thrown
    when trying to create such resource.

*/

typedef enum {
    xg_buffer_usage_bit_copy_source_m           = 1 << 0,
    xg_buffer_usage_bit_copy_dest_m             = 1 << 1,
    xg_buffer_usage_bit_texel_uniform_m         = 1 << 2,
    xg_buffer_usage_bit_texel_storage_m         = 1 << 3,
    xg_buffer_usage_bit_uniform_m               = 1 << 4,
    xg_buffer_usage_bit_storage_m               = 1 << 5,
    xg_buffer_usage_bit_index_buffer_m          = 1 << 6,
    xg_buffer_usage_bit_vertex_buffer_m         = 1 << 7,
    // Note: internal code might extend this, check before adding something
} xg_buffer_usage_bit_e;

typedef enum {
    xg_texture_layout_undefined_m,                    // UNDEFINED
    xg_texture_layout_all_m,                          // GENERAL
    xg_texture_layout_host_initialize_m,              // PREINITIALIZED
    xg_texture_layout_copy_source_m,                  // TRANSFER_SRC
    xg_texture_layout_copy_dest_m,                    // TRANSFER_DST
    xg_texture_layout_present_m,                      // PRESENT_SRC_KHR
    xg_texture_layout_shader_read_m,                  // SHADER_READ_ONLY_OPTIMAL
    xg_texture_layout_shader_write_m,                 // GENERAL
    xg_texture_layout_render_target_m,                // VK_COLOR_ATTACHMENT_OPTIMAL
    xg_texture_layout_depth_stencil_target_m,         // DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    xg_texture_layout_depth_stencil_read_m,           // DEPTH_STENCIL_READ_ONLY_OPTIMAL
    xg_texture_layout_depth_read_stencil_target_m,    // DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL
    xg_texture_layout_depth_target_stencil_read_m,    // DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL
} xg_texture_layout_e;

typedef enum {
    xg_texture_dimension_1d_m,
    xg_texture_dimension_2d_m,
    xg_texture_dimension_3d_m,
} xg_texture_dimension_e;

// TODO
typedef enum {
    xg_texture_usage_bit_copy_source_m      = 1 << 0,
    xg_texture_usage_bit_copy_dest_m        = 1 << 1,
    xg_texture_usage_bit_resource_m         = 1 << 2,
    xg_texture_usage_bit_storage_m          = 1 << 3,
    xg_texture_usage_bit_render_target_m    = 1 << 4,
    xg_texture_usage_bit_depth_stencil_m    = 1 << 5,
} xg_texture_usage_bit_e;

typedef enum {
    xg_texture_aspect_default_m,
    xg_texture_aspect_color_m,
    xg_texture_aspect_depth_m,
    xg_texture_aspect_stencil_m,
} xg_texture_aspect_e;

// -- Barriers --
// TODO simplify these and group barriers when translating
typedef enum {
    xg_pipeline_stage_bit_none_m                                = 0,
    xg_pipeline_stage_bit_top_of_pipe_m                         = 0x00000001,
    xg_pipeline_stage_bit_draw_m                                = 0x00000002,
    xg_pipeline_stage_bit_vertex_input_m                        = 0x00000004,
    xg_pipeline_stage_bit_vertex_shader_m                       = 0x00000008,
    // xg_pipeline_stage_tessellation_control_shader_m       = 0x00000010,
    // xg_pipeline_stage_tessellation_evaluation_shader_m    = 0x00000020,
    // xg_pipeline_stage_geometry_shader_m                   = 0x00000040,
    xg_pipeline_stage_bit_fragment_shader_m                     = 0x00000080,
    xg_pipeline_stage_bit_early_fragment_test_m                 = 0x00000100,
    xg_pipeline_stage_bit_late_fragment_test_m                  = 0x00000200,
    xg_pipeline_stage_bit_color_output_m                        = 0x00000400,
    xg_pipeline_stage_bit_compute_shader_m                      = 0x00000800,
    xg_pipeline_stage_bit_transfer_m                            = 0x00001000,
    xg_pipeline_stage_bit_bottom_of_pipe_m                      = 0x00002000,
    xg_pipeline_stage_bit_host_m                                = 0x00004000,
    xg_pipeline_stage_bit_all_graphics_m                        = 0x00008000,
    xg_pipeline_stage_bit_all_commands_m                        = 0x00010000,
    xg_pipeline_stage_bit_raytrace_shader_m                     = 0x00200000,
    xg_pipeline_stage_bit_ray_acceleration_structure_build_m    = 0x02000000,
} xg_pipeline_stage_bit_e;

/*
typedef struct {
    xg_pipeline_stage_bit_e blocker;    // srcStageMask
    xg_pipeline_stage_bit_e blocked;    // dstStageMask
} xg_execution_barrier_t;
*/

typedef enum {
    xg_memory_access_bit_none_m                       = 0,
    xg_memory_access_bit_command_read_m               = 0x00000001,
    xg_memory_access_bit_index_read_m                 = 0x00000002,
    xg_memory_access_bit_vertex_attribute_read_m      = 0x00000004,
    xg_memory_access_bit_uniform_read_m               = 0x00000008,
    xg_memory_access_bit_input_render_texture_read_m  = 0x00000010,
    xg_memory_access_bit_shader_read_m                = 0x00000020,
    xg_memory_access_bit_shader_write_m               = 0x00000040,
    xg_memory_access_bit_color_read_m                 = 0x00000080,
    xg_memory_access_bit_color_write_m                = 0x00000100,
    xg_memory_access_bit_depth_stencil_read_m         = 0x00000200,
    xg_memory_access_bit_depth_stencil_write_m        = 0x00000400,
    xg_memory_access_bit_transfer_read_m              = 0x00000800,
    xg_memory_access_bit_transfer_write_m             = 0x00001000,
    xg_memory_access_bit_host_read_m                  = 0x00002000,
    xg_memory_access_bit_host_write_m                 = 0x00004000,
    xg_memory_access_bit_memory_read_m                = 0x00008000,
    xg_memory_access_bit_memory_write_m               = 0x00010000,
} xg_memory_access_bit_e;

/*
typedef struct {
    //xg_execution_barrier_t execution;
    xg_memory_access_bit_e cache_flushes;           // make any cache that can possibly contain one of these memory accesses flush its content, thus making the changes visible from outside. srcAccessMask
    xg_memory_access_bit_e cache_invalidations;     // make any cache that can possibly contain one of these memory accesses invalidate its content, thus making it able to see outside changes. dstAccessMask
} xg_memory_barrier_t;

typedef struct {
    xg_texture_h texture;
    xg_texture_layout_e old_layout;
    xg_texture_layout_e new_layout;
    xg_memory_barrier_t memory;
} xg_texture_memory_barrier_t;
*/

/*
    execution dependencies ensure that work in specified stages completes fully before starting work on other specified stages
    memory dependencies ensure that data written on one cache will be visible on other caches by flushing the source cache to main memory and invalidating the dest cache

    RaR is dependency free
    WaR dependencies only require execution dependencies, since there's no cache write involved in the first operation
    RaW and WaW dependencies require a memory dependency in order to assure that the first write is visible to the following read, and that the 2 writes read main memory in the right order
    https://github.com/ARM-software/vulkan-sdk/issues/25
*/

typedef struct {
    xg_pipeline_stage_bit_e blocker; // the stage that needs to complete before blocked can be executed
    xg_pipeline_stage_bit_e blocked; // the stage that needs to wait for blocker to complete before executing
} xg_execution_dependency_t;

#define xg_execution_dependency_m( ... ) ( xg_execution_dependency_t ) { \
    .blocker = xg_pipeline_stage_bit_none_m, \
    .blocked = xg_pipeline_stage_bit_none_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_memory_access_bit_e flushes; // make any cache that can possibly contain one of these memory accesses flush its content, making the changes visible from outside
    xg_memory_access_bit_e invalidations; // make any cache that can possibly contain one of these memory accesses invalidate its content, making it able to see outside changes
} xg_memory_dependency_t;

#define xg_memory_dependency_m( ... ) ( xg_memory_dependency_t ) { \
    .flushes = xg_memory_access_bit_none_m, \
    .invalidations = xg_memory_access_bit_none_m, \
    ##__VA_ARGS__ \
}

// TODO rename to texture_layout_transition_t? not really a dependency
typedef struct {
    xg_texture_layout_e old;
    xg_texture_layout_e new;
} xg_texture_layout_dependency_t;

#define xg_layout_dependency_m( ... ) ( xg_texture_layout_dependency_t ) { \
    .old = xg_texture_layout_undefined_m, \
    .new = xg_texture_layout_undefined_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_execution_dependency_t execution;
    xg_memory_dependency_t memory;
} xg_memory_barrier_t;

typedef struct {
    xg_texture_h texture;
    //xg_texture_aspect_e aspect;
    uint32_t mip_base;
    uint32_t mip_count;
    uint32_t array_base;
    uint32_t array_count;
    xg_execution_dependency_t execution;
    xg_memory_dependency_t memory;
    xg_texture_layout_dependency_t layout;
} xg_texture_memory_barrier_t;

#define xg_texture_memory_barrier_m( ... ) ( xg_texture_memory_barrier_t ) { \
    .texture = xg_null_handle_m, \
    .mip_base = 0, \
    .mip_count = xg_texture_all_mips_m, \
    .array_base = 0, \
    .array_count = xg_texture_whole_array_m, \
    .execution = xg_execution_dependency_m(), \
    .memory = xg_memory_dependency_m(), \
    .layout = xg_layout_dependency_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_buffer_h buffer;
    uint64_t offset;
    uint64_t size;
    xg_execution_dependency_t execution;
    xg_memory_dependency_t memory;
} xg_buffer_memory_barrier_t;

#define xg_buffer_memory_barrier_m( ... ) ( xg_buffer_memory_barrier_t ) { \
    .buffer = xg_null_handle_m, \
    .offset = 0, \
    .size = 0, \
    .execution = xg_execution_dependency_m(), \
    .memory = xg_memory_dependency_m(), \
    ##__VA_ARGS__ \
}

// -- Commands --
typedef uint64_t xg_workload_h;
#define xg_workload_null_m UINT64_MAX;

typedef struct {
    xg_buffer_h buffer;
    uint32_t stream_id;
    uint32_t offset;
} xg_vertex_stream_binding_t;

#define xg_vertex_stream_binding_m( ... ) ( xg_vertex_stream_binding_t ) { \
    .buffer = xg_null_handle_m, \
    .stream_id = -1, \
    .offset = 0, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_buffer_binding_type_uniform_m,
    xg_buffer_binding_type_storage_m,
    xg_buffer_binding_type_texel_uniform_m,
    xg_buffer_binding_type_texel_storage_m,
    xg_buffer_binding_type_invalid_m,
} xg_buffer_binding_e;

#define xg_buffer_whole_size_m UINT64_MAX

typedef struct {
    xg_buffer_h handle;
    uint64_t offset;
    uint64_t size;
} xg_buffer_range_t;

#define xg_buffer_range_m( _handle, _offset, _size ) ( xg_buffer_range_t ) { \
    .handle = _handle, \
    .offset = _offset, \
    .size = _size, \
}

#define xg_default_buffer_range_m xg_buffer_range_m(xg_null_handle_m, 0, xg_buffer_whole_size_m )
#define xg_range_whole_buffer_m( handle ) xg_buffer_range_m ( handle, 0, xg_buffer_whole_size_m )

typedef enum {
    xg_resource_binding_id_shader_register_m,
    xg_resource_binding_id_name_m, // xss name
} xg_resource_binding_id_e;

typedef struct {
    xg_resource_binding_id_e type;
    union {
        uint32_t shader_register;
        uint64_t name;
    };
} xg_resource_binding_id_t;

#define xg_resource_binding_shader_register_m( _register ) ( xg_resource_binding_id_t ) { \
    .type = xg_resource_binding_id_type_shader_register_m, \
    .shader_register = _register, \
}

#define xg_resource_binding_name_hash_m( _hash ) ( xg_resource_binding_id_t ) { \
    .type = xg_resource_binding_id_type_name_m, \
    .name = _hash, \
}

#define xg_resource_binding_name_m( _name ) xg_resource_binding_name_hash_m ( std_str_hash_32 ( _name ) )

typedef struct {
    uint32_t shader_register;
    xg_buffer_binding_e type;
    xg_buffer_range_t range;
} xg_buffer_resource_binding_t;

#define xg_buffer_resource_binding_m(...) ( xg_buffer_resource_binding_t ) { \
    .shader_register = -1, \
    .type = xg_buffer_binding_type_invalid_m, \
    .range = { .handle = xg_null_handle_m, .offset = 0, .size = 0 }, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_texture_binding_type_to_sample_m,
    xg_texture_binding_type_storage_read_m,
    xg_texture_binding_type_storage_write_m,
    xg_texture_binding_type_invalid_m,
} xg_texture_binding_e;

typedef struct {
    //xg_resource_binding_id_t id;
    uint32_t shader_register;
    xg_texture_layout_e layout;
    xg_texture_h texture;
    xg_texture_view_t view;
} xg_texture_resource_binding_t;

#define xg_texture_resource_binding_m( ... ) ( xg_texture_resource_binding_t ) { \
    .shader_register = -1, \
    .layout = xg_texture_layout_undefined_m, \
    .texture = xg_null_handle_m, \
    .view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t shader_register;
    xg_sampler_h sampler;
} xg_sampler_resource_binding_t;

#define xg_sampler_resource_binding_m( ... ) ( xg_sampler_resource_binding_t ) { \
    .shader_register = -1, \
    .sampler = xg_null_handle_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t shader_register;
    xg_raytrace_world_h world;
} xg_raytrace_world_resource_binding_t;

#define xg_raytrace_world_resource_binding_m( ... ) ( xg_raytrace_world_resource_binding_t ) { \
    .shader_register = -1, \
    .world = xg_null_handle_m, \
    ##__VA_ARGS__ \
}

// TODO rename to xg_shader_resource_bindings_t?
// TODO store whole arrays instead of pointers here? makes filling this struct easier
#if 0
typedef struct {
    xg_resource_binding_set_e set;
    uint32_t buffer_count;
    uint32_t texture_count;
    uint32_t sampler_count;
    xg_buffer_resource_binding_t* buffers;
    xg_texture_resource_binding_t* textures;
    xg_sampler_resource_binding_t* samplers;
} xg_pipeline_resource_bindings_t;
#else
typedef struct {
    xg_resource_binding_set_e set;
    uint32_t buffer_count;
    uint32_t texture_count;
    uint32_t sampler_count;
    uint32_t raytrace_world_count;
    xg_buffer_resource_binding_t buffers[xg_pipeline_resource_max_buffers_per_set_m];
    xg_texture_resource_binding_t textures[xg_pipeline_resource_max_textures_per_set_m];
    xg_sampler_resource_binding_t samplers[xg_pipeline_resource_max_samplers_per_set_m];
    xg_raytrace_world_resource_binding_t raytrace_worlds[xg_pipeline_resource_max_raytrace_worlds_per_set_m];
} xg_pipeline_resource_bindings_t;
#endif

typedef struct {
    xg_device_h device;
    xg_pipeline_state_h pipeline;
    xg_pipeline_resource_bindings_t bindings;
} xg_pipeline_resource_group_params_t;

#if 0
#define xg_pipeline_resource_bindings_m( ... ) ( xg_pipeline_resource_bindings_t ) { \
    .set = xg_resource_binding_set_invalid_m, \
    .buffer_count = 0, \
    .texture_count = 0, \
    .sampler_count = 0, \
    .buffers = NULL, \
    .textures = NULL, \
    .samplers = NULL, \
    ##__VA_ARGS__ \
}
#else
#define xg_pipeline_resource_bindings_m( ... ) ( xg_pipeline_resource_bindings_t ) { \
    .set = xg_resource_binding_set_invalid_m, \
    .buffer_count = 0, \
    .texture_count = 0, \
    .sampler_count = 0, \
    .buffers =  { [0 ... xg_pipeline_resource_max_buffers_per_set_m-1]  = xg_buffer_resource_binding_m()  }, \
    .textures = { [0 ... xg_pipeline_resource_max_textures_per_set_m-1] = xg_texture_resource_binding_m() }, \
    .samplers = { [0 ... xg_pipeline_resource_max_samplers_per_set_m-1] = xg_sampler_resource_binding_m() }, \
    .raytrace_worlds = { [0 ... xg_pipeline_resource_max_raytrace_worlds_per_set_m-1] = xg_raytrace_world_resource_binding_m() }, \
    ##__VA_ARGS__ \
}
#endif

typedef struct {
    xg_shading_stage_bit_e stages;
    uint32_t write_offset;
    uint32_t size;
    void* base;
} xg_pipeline_constant_data_t;

typedef struct {
    //xg_execution_barrier_t execution_barrier;
    //xg_execution_barrier_t* execution_barriers;
    //size_t execution_barriers_count;
    xg_memory_barrier_t* memory_barriers;
    size_t memory_barriers_count;
    xg_buffer_memory_barrier_t* buffer_memory_barriers;
    size_t buffer_memory_barriers_count;
    xg_texture_memory_barrier_t* texture_memory_barriers;
    size_t texture_memory_barriers_count;
} xg_barrier_set_t;

#define xg_barrier_set_m( ... ) ( xg_barrier_set_t ) { \
    .memory_barriers = NULL, \
    .memory_barriers_count = 0, \
    .buffer_memory_barriers = NULL, \
    .buffer_memory_barriers_count = 0, \
    .texture_memory_barriers = NULL, \
    .texture_memory_barriers_count = 0, \
    ##__VA_ARGS__ \
}

// in VK fences can only be signaled when a queue submit has finished executing, so they're workloads in xg
typedef uint64_t xg_gpu_event_h;    // VK semaphore
// TODO event api: create_event(), destroy_event(), and on cmd buffer dispatch take one array of events to wait for before running and one array of events to signal when cmd buffer is executed
// TODO what about VkEvent?

typedef struct {
    xg_memory_type_e memory_type;
    xg_device_h device;
    size_t size;
    // TODO rename to allowed_gpu_usage?
    xg_buffer_usage_bit_e allowed_usage;
    char debug_name[xg_debug_name_size_m];
} xg_buffer_params_t;

#define xg_buffer_params_m( ... ) ( xg_buffer_params_t ) { \
    .memory_type = xg_memory_type_null_m, \
    .device = xg_null_handle_m, \
    .size = 0, \
    .allowed_usage = 0, \
    .debug_name = {0}, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_texture_view_access_default_only_m,
    xg_texture_view_access_separate_mips_m,
    xg_texture_view_access_dynamic_m,
} xg_texture_view_access_e;

typedef struct {
    uint32_t max_view_count;
} xg_texture_view_access_ext_dynamic_params_t;

typedef struct {
    xg_memory_type_e memory_type;
    xg_device_h device;
    size_t width;
    size_t height;
    size_t depth;           // defaults to 1
    size_t mip_levels;      // defaults to 1 - TODO rename mip_count
    size_t array_layers;    // defaults to 1 - TODO rename array_count
    xg_texture_dimension_e dimension;
    xg_format_e format;
    // TODO rename to allowed_gpu_usage?
    xg_texture_usage_bit_e allowed_usage;
    xg_texture_layout_e initial_layout; // TODO remove this, only accepted inital layout by vulkan is undefined
    xg_sample_count_e samples_per_pixel;
    xg_texture_view_access_e view_access;
    void* view_access_ext;
    char debug_name[xg_debug_name_size_m];
} xg_texture_params_t;

#define xg_texture_params_m(...) ( xg_texture_params_t ) { \
    .memory_type = xg_memory_type_null_m, \
    .device = xg_null_handle_m, \
    .width = 1, \
    .height = 1, \
    .depth = 1, \
    .mip_levels = 1, \
    .array_layers = 1, \
    .dimension = xg_texture_dimension_2d_m, \
    .format = xg_format_undefined_m, \
    .allowed_usage = 0, \
    .initial_layout = xg_texture_layout_undefined_m, \
    .samples_per_pixel = xg_sample_count_1_m, \
    .view_access = xg_texture_view_access_default_only_m, \
    .debug_name = {0}, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_sampler_filter_point_m,
    xg_sampler_filter_linear_m
} xg_sampler_filter_e;

typedef enum {
    xg_sampler_address_mode_wrap_m,
    xg_sampler_address_mode_clamp_m
} xg_sampler_address_mode_e;

typedef struct {
    xg_device_h device;
    xg_sampler_filter_e min_filter;
    xg_sampler_filter_e mag_filter;
    xg_sampler_filter_e mipmap_filter;
    bool enable_anisotropy;
    uint32_t min_mip; // TODO why are min and max mip floats in the Vulkan api?
    uint32_t max_mip;
    uint32_t mip_bias;
    xg_sampler_address_mode_e address_mode; // TODO separate value for u/v/w axes?
    char debug_name[xg_debug_name_size_m];
} xg_sampler_params_t;

#define xg_sampler_params_m(...) ( xg_sampler_params_t ) { \
    .min_filter = xg_sampler_filter_point_m, \
    .mag_filter = xg_sampler_filter_point_m, \
    .mipmap_filter = xg_sampler_filter_point_m, \
    .enable_anisotropy = false, \
    .min_mip = 0, \
    .max_mip = xg_texture_all_mips_m, \
    .mip_bias = 0, \
    .address_mode = xg_sampler_address_mode_clamp_m, \
    .debug_name = { 0 }, \
    ##__VA_ARGS__ \
}

typedef enum {
    xg_texture_flag_bit_swapchain_texture_m = 1 << 0,
    xg_texture_flag_bit_render_target_texture_m = 1 << 1,
    //xg_texture_flag_bit_depth_stencil_texture_m = 1 << 2,
    xg_texture_flag_bit_depth_texture_m = 1 << 2,
    xg_texture_flag_bit_stencil_texture_m = 1 << 3,
} xg_texture_flag_bit_e;

typedef struct {
    // TODO rename to alloc?
    xg_alloc_t allocation;
    xg_device_h device;
    size_t width;
    size_t height;
    size_t depth;
    size_t mip_levels;
    size_t array_layers;
    xg_texture_dimension_e dimension;
    xg_format_e format;
    xg_texture_usage_bit_e allowed_usage;
    xg_sample_count_e samples_per_pixel;
    xg_texture_flag_bit_e flags;
    xg_texture_aspect_e default_aspect;
    uint64_t os_handle; // TODO replace with monotonically increasing resource uid
    char debug_name[xg_debug_name_size_m];
} xg_texture_info_t;

typedef uint64_t xg_buffer_address_t;

typedef struct {
    // TODO rename to alloc?
    xg_alloc_t allocation;
    xg_device_h device;
    size_t size;
    xg_buffer_usage_bit_e allowed_usage;
    char debug_name[xg_debug_name_size_m];
} xg_buffer_info_t;

typedef struct {
    xg_device_h device;
    xg_sampler_filter_e mag_filter;
    xg_sampler_filter_e min_filter;
    xg_sampler_filter_e mipmap_filter;
    bool anisotropic;
    uint32_t min_mip;
    uint32_t max_mip;
    uint32_t mip_bias;
    xg_sampler_address_mode_e address_mode;
    char debug_name[xg_debug_name_size_m];
} xg_sampler_info_t;

typedef enum {
    xg_resource_cmd_buffer_time_workload_start_m,
    xg_resource_cmd_buffer_time_workload_complete_m,
} xg_resource_cmd_buffer_time_e;

typedef enum {
    xg_debug_capture_stop_time_manual_trigger_m,
    xg_debug_capture_stop_time_workload_submit_m,
    xg_debug_capture_stop_time_workload_present_m,
} xg_debug_capture_stop_time_e;

#define xg_debug_region_color_light_green_m   0x99b433ff
#define xg_debug_region_color_green_m         0x00a300ff
#define xg_debug_region_color_dark_green_m    0x1e7145ff
#define xg_debug_region_color_magenta_m       0xff0097ff
#define xg_debug_region_color_light_purple_m  0x9f00a7ff
#define xg_debug_region_color_purple_m        0x7e3878ff
#define xg_debug_region_color_dark_purple_m   0x603cbaff
//#define xg_debug_region_color_black_m         0x1d1d1dff
#define xg_debug_region_color_teal_m          0x00aba9ff
#define xg_debug_region_color_light_blue_m    0xeff4ffff
#define xg_debug_region_color_blue_m          0x2d89efff
#define xg_debug_region_color_dark_blue_m     0x2b5797ff
#define xg_debug_region_color_yellow_m        0xffc40dff
#define xg_debug_region_color_orange_m        0xe3a21aff
#define xg_debug_region_color_dark_orange_m   0xda532cff
#define xg_debug_region_color_red_m           0xee1111ff
#define xg_debug_region_color_dark_red_m      0xb91d47ff
#define xg_debug_region_color_none_m          0x0

typedef struct {
    xg_texture_h texture;
    uint32_t mip_base;
    uint32_t array_base;
} xg_texture_copy_resource_t;

#define xg_texture_copy_resource_m( ... ) ( xg_texture_copy_resource_t ) { \
    .texture = xg_null_handle_m, \
    .mip_base = 0, \
    .array_base = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_texture_copy_resource_t source;
    xg_texture_copy_resource_t destination;
    uint32_t mip_count;
    uint32_t array_count;
    xg_texture_aspect_e aspect;
    xg_sampler_filter_e filter;
} xg_texture_copy_params_t;

#define xg_texture_copy_params_m( ... ) ( xg_texture_copy_params_t ) { \
    .source = xg_texture_copy_resource_m(), \
    .destination = xg_texture_copy_resource_m(), \
    .mip_count = xg_texture_all_mips_m, \
    .array_count = xg_texture_whole_array_m, \
    .aspect = xg_texture_aspect_default_m, \
    .filter = xg_sampler_filter_point_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_buffer_h source;
    uint64_t source_offset;
    xg_texture_h destination;
    uint32_t mip_base;
    uint32_t array_base;
    uint32_t array_count;
    //xg_texture_aspect_e aspect;
    //xg_texture_layout_e layout;
} xg_buffer_to_texture_copy_params_t;

#define xg_default_buffer_to_texture_copy_params_m ( xg_buffer_to_texture_copy_params_t ) { \
    .source = xg_null_handle_m, \
    .source_offset = 0, \
    .destination = xg_null_handle_m, \
    .mip_base = 0, \
    .array_base = 0, \
    .array_count = 1, \
}

typedef struct {
    xg_texture_h source;
    uint32_t mip_base;
    uint32_t array_base;
    uint32_t array_count;
    xg_buffer_h destination;
    uint64_t destination_offset;
} xg_texture_to_buffer_copy_params_t;

#define xg_default_texture_to_buffer_copy_params_m ( xg_texture_to_buffer_copy_params_t ) { \
    .source = xg_null_handle_m, \
    .mip_base = 0, \
    .array_base = 0, \
    .array_count = 1, \
    .destination = xg_null_handle_m, \
    .destination_offset = 0, \
}

// TODO move out of xg
typedef enum {
    xg_default_texture_r8g8b8a8_unorm_white_m,
    xg_default_texture_r8g8b8a8_unorm_black_m,
} xg_default_texture_e;

typedef enum {
    xg_default_sampler_point_clamp_m,
    xg_default_sampler_linear_clamp_m,
    xg_default_sampler_count_m,
} xg_default_sampler_e;

// TODO do view.mip_count >= mip_levels instead of ==?
#define xg_texture_view_is_default_m( view, _mip_levels, _array_layers, _format, _allowed_usage ) ( \
    view.mip_base == 0 && ( view.mip_count == xg_texture_all_mips_m || view.mip_count == _mip_levels ) \
    && view.array_base == 0 && ( view.array_count == xg_texture_whole_array_m || view.array_count == _array_layers ) \
    && ( view.format == xg_texture_view_default_format_m || view.format == _format ) \
    && ( view.aspect == xg_texture_aspect_default_m || view.aspect == ( _allowed_usage & xg_texture_usage_bit_depth_stencil_m ? xg_texture_aspect_depth_m : xg_texture_aspect_color_m ) ) \
)

typedef struct {
    xg_buffer_h vertex_buffer;
    uint64_t vertex_buffer_offset;
    xg_format_e vertex_format;
    uint32_t vertex_count;
    uint32_t vertex_stride;
    xg_buffer_h index_buffer;
    uint64_t index_buffer_offset;
    uint32_t index_count;
    xg_buffer_h transform_buffer;
    uint64_t transform_buffer_offset;
    char debug_name[xg_debug_name_size_m];
} xg_raytrace_geometry_data_t;

typedef struct {
    xg_device_h device;
    xg_raytrace_geometry_data_t* geometries;
    uint32_t geometries_count;
    char debug_name[xg_debug_name_size_m];
} xg_raytrace_geometry_params_t;

#define xg_raytrace_geometry_params_m( ... ) ( xg_raytrace_geometry_params_t ) { \
    .device = device, \
    .geometries = NULL, \
    .geometries_count = 0, \
    .debug_name = "", \
    ##__VA_ARGS__ \
}

// Row major storage
// r0 = m[0] = e00 e01 e02 e03
// r1 = m[1] = e04 e05 e06 e07
// r2 = m[2] = e08 e09 e10 e11
typedef union {
    float f[12];
    float m[3][4];
    struct {
        float r0[4];
        float r1[4];
        float r2[4];
    };
} xg_matrix_3x4_t;

typedef enum {
    xg_raytrace_instance_flag_bit_disable_face_cull_m = 1 << 0,
} xg_raytrace_instance_flag_bit_e;

typedef struct {
    xg_raytrace_geometry_h geometry;
    xg_matrix_3x4_t transform;
    uint8_t visibility_mask;
    xg_raytrace_instance_flag_bit_e flags;
} xg_raytrace_geometry_instance_t;

typedef struct {
    xg_device_h device;
    xg_raytrace_geometry_instance_t* instance_array;
    uint32_t instance_count;
    char debug_name[xg_debug_name_size_m];
} xg_raytrace_world_params_t;

#define xg_raytrace_world_params_m( ... ) ( xg_raytrace_world_params_t ) { \
    .device = xg_null_handle_m, \
    .instance_array = NULL,  \
    .instance_count = 0, \
    .debug_name = "", \
    ##__VA_ARGS__ \
}

// XG API
typedef struct {
    // Device
    size_t                  ( *get_devices_count )                  ( void );
    size_t                  ( *get_devices )                        ( xg_device_h* devices, size_t cap );
    bool                    ( *get_device_info )                    ( xg_device_info_t* info, xg_device_h device );

    bool                    ( *activate_device )                    ( xg_device_h device );
    bool                    ( *deactivate_device )                  ( xg_device_h deivce );

    // Display
    // TODO determine availability of this vulkan API and either add this to xg_test or remove it completely
    size_t                  ( *get_device_display_info_count )      ( xg_device_h device );
    size_t                  ( *get_device_display_info )            ( xg_display_info_t* displays, size_t cap, xg_device_h device );
    size_t                  ( *get_device_display_modes_count )     ( xg_display_info_t* display, xg_device_h device );
    size_t                  ( *get_device_display_modes )           ( xg_display_info_t* display, xg_display_mode_t* modes, size_t cap, xg_device_h device );

    // Swapchain
    xg_swapchain_h          ( *create_window_swapchain )            ( const xg_swapchain_window_params_t* params );
    xg_swapchain_h          ( *create_display_swapchain )           ( const xg_swapchain_display_params_t* params );
    xg_swapchain_h          ( *create_virtual_swapchain )           ( const xg_swapchain_virtual_params_t* params );
    bool                    ( *resize_swapchain )                   ( xg_swapchain_h swapchain, size_t width, size_t height );
    bool                    ( *get_swapchain_info )                 ( xg_swapchain_info_t* info, xg_swapchain_h swapchain );
    // TODO have the option to acquire the swapchain texture through a resource cmd buffer?
    uint32_t                ( *acquire_next_swapchain_texture )     ( xg_swapchain_h swapchain, xg_workload_h workload, bool* resize );
    xg_texture_h            ( *get_swapchain_texture )              ( xg_swapchain_h swapchain );
    void                    ( *present_swapchain )                  ( xg_swapchain_h swapchain, xg_workload_h workload );
    void                    ( *destroy_swapchain )                  ( xg_swapchain_h swapchain );

    // Workload
    xg_workload_h           ( *create_workload )                    ( xg_device_h device );
    xg_cmd_buffer_h         ( *create_cmd_buffer )                  ( xg_workload_h workload );
    //void                    ( *create_cmd_buffers )                 ( xg_cmd_buffer_h* cmd_buffers, size_t count, xg_workload_h workload );

    //void                    ( *close_cmd_buffers )                  ( xg_cmd_buffer_h* cmd_buffers, size_t count ); // TODO remove this
    //void                    ( *discard_cmd_buffers )                ( xg_cmd_buffer_h* cmd_buffers, size_t count );
    void                    ( *submit_workload )                    ( xg_workload_h workload );
    void                    ( *discard_workload )                   ( xg_workload_h workload );
    bool                    ( *is_workload_complete )               ( xg_workload_h workload );

    double                  ( *get_timestamp_ticks_per_us )         ( void );

    xg_resource_cmd_buffer_h ( *create_resource_cmd_buffer )        ( xg_workload_h workload );
    //void                    ( *create_resource_cmd_buffers )        ( xg_resource_cmd_buffer_h* cmd_buffers, size_t count, xg_workload_h workload );
    //void                    ( *close_resource_cmd_buffers )         ( xg_resource_cmd_buffer_h* cmd_buffers, size_t count );

    // Event
    xg_gpu_event_h          ( *create_event )                       ( xg_device_h device );

    xg_renderpass_h         ( *create_renderpass )                  ( const xg_graphics_renderpass_params_t* params );
    void                    ( *destroy_renderpass )                 ( xg_renderpass_h renderpass );

    // Command Buffer
    // TODO move these to a separate _i api? have them as static functions?
    // TODO rename all draw/dispatch/trace_rays cmds to graphics/compute/raytrace_dispatch ?
    void                    ( *cmd_clear_texture )                  ( xg_cmd_buffer_h cmd_buffer, xg_texture_h texture, xg_color_clear_t clear, uint64_t key );
    void                    ( *cmd_clear_depth_stencil_texture )    ( xg_cmd_buffer_h cmd_buffer, xg_texture_h texture, xg_depth_stencil_clear_t clear, uint64_t key );
    void                    ( *cmd_barrier_set )                    ( xg_cmd_buffer_h cmd_buffer, const xg_barrier_set_t* barrier_set, uint64_t key );
    void                    ( *cmd_set_vertex_streams )             ( xg_cmd_buffer_h cmd_buffer, const xg_vertex_stream_binding_t* bindings, size_t count, uint64_t key );
    void                    ( *cmd_draw )                           ( xg_cmd_buffer_h cmd_buffer, size_t vertex_count, size_t streams_base, uint64_t key );
    void                    ( *cmd_draw_indexed )                   ( xg_cmd_buffer_h cmd_buffer, xg_buffer_h ibuffer, size_t index_count, size_t index_base, uint64_t key );
    void                    ( *cmd_draw_instanced )                 ( xg_cmd_buffer_h cmd_buffer, size_t vertex_base, size_t vertex_count, size_t instance_base, size_t instance_count, uint64_t key );
    void                    ( *cmd_set_graphics_pipeline_state )    ( xg_cmd_buffer_h cmd_buffer, xg_graphics_pipeline_state_h pipeline_state, uint64_t key );
    void                    ( *cmd_set_compute_pipeline_state )     ( xg_cmd_buffer_h cmd_buffer, xg_compute_pipeline_state_h pipeline_state, uint64_t key );
    void                    ( *cmd_set_render_textures )            ( xg_cmd_buffer_h cmd_buffer, const xg_render_textures_binding_t* bindings, uint64_t key );
    // TODO rename to cmd_bind_pipeline_resource_set (rename set to something else?)
    void                    ( *cmd_set_pipeline_resources )         ( xg_cmd_buffer_h cmd_buffer, const xg_pipeline_resource_bindings_t* bindings, uint64_t key );
    void                    ( *cmd_write_pipeline_constant_data )   ( xg_cmd_buffer_h cmd_buffer, const xg_pipeline_constant_data_t* data, uint64_t key );
    void                    ( *cmd_dispatch_compute )               ( xg_cmd_buffer_h cmd_buffer, uint32_t workgroup_count_x, uint32_t workgroup_count_y, uint32_t workgroup_count_z, uint64_t key );
    void                    ( *cmd_begin_renderpass )               ( xg_cmd_buffer_h cmd_buffer, xg_renderpass_h renderpass, uint64_t key );
    void                    ( *cmd_end_renderpass )                 ( xg_cmd_buffer_h cmd_buffer, xg_renderpass_h renderpass, uint64_t key );

    void                    ( *cmd_set_raytrace_pipeline_state )    ( xg_cmd_buffer_h cmd_buffer, xg_raytrace_pipeline_state_h pipeline_state, uint64_t key );
    void                    ( *cmd_trace_rays )                     ( xg_cmd_buffer_h cmd_buffer, uint32_t ray_count_x, uint32_t ray_count_y, uint32_t ray_count_z, uint64_t key );

    void                    ( *cmd_destroy_renderpass )             ( xg_resource_cmd_buffer_h cmd_buffer, xg_renderpass_h renderpass, xg_resource_cmd_buffer_time_e destroy_time );

    //void                    ( *cmd_generate_texture_mips )          ( xg_cmd_buffer_h cmd_buffer, xg_texture_h texture, uint32_t mip_base, uint32_t mip_count, uint64_t key );

    xg_pipeline_resource_group_h    ( *cmd_create_pipeline_resource_group )     ( xg_resource_cmd_buffer_h cmd_buffer, const xg_pipeline_resource_group_params_t* params );
    void                            ( *cmd_destroy_pipeline_resource_group )    ( xg_resource_cmd_buffer_h cmd_buffer, xg_pipeline_resource_group_h group, xg_resource_cmd_buffer_time_e destroy_time );
    void                            ( *cmd_set_pipeline_resource_group )        ( xg_cmd_buffer_h cmd_buffer, xg_resource_binding_set_e set, xg_pipeline_resource_group_h group, uint64_t key );

    void                    ( *cmd_copy_texture )                   ( xg_cmd_buffer_h cmd_buffer, const xg_texture_copy_params_t* params, uint64_t key );
    void                    ( *cmd_copy_buffer )                    ( xg_cmd_buffer_h cmd_buffer, xg_buffer_h src, xg_buffer_h dest, uint64_t key ); // TODO better args
    void                    ( *cmd_copy_buffer_to_texture )         ( xg_cmd_buffer_h cmd_buffer, const xg_buffer_to_texture_copy_params_t* params, uint64_t key );
    // TOOD
    //void                    ( *cmd_signal_event )                   ( xg_cmd_buffer_h cmd_buffer, xg_gpu_event_h event, xg_pipeline_stage_bit_e signal_stages, uint64_t key );
    //void                    ( *cmd_wait_for_event )                 ( xg_cmd_buffer_h cmd_buffer, xg_gpu_event_h event, const xg_barrier_set_t* barrier_set, uint64_t key );
    //void                    ( *cmd_reset_event )                    ( xg_cmd_buffer_h cmd_buffer, xg_gpu_event_h event, uint64_t key );

    void                    ( *cmd_start_debug_capture )            ( xg_cmd_buffer_h cmd_buffer, xg_debug_capture_stop_time_e stop_time, uint64_t key );
    void                    ( *cmd_stop_debug_capture )             ( xg_cmd_buffer_h cmd_buffer, uint64_t key );

    void                    ( *cmd_begin_debug_region )             ( xg_cmd_buffer_h cmd_buffer, const char* name, uint32_t color, uint64_t key );
    void                    ( *cmd_end_debug_region )               ( xg_cmd_buffer_h cmd_buffer, uint64_t key );

    xg_buffer_h             ( *cmd_create_buffer )                  ( xg_resource_cmd_buffer_h cmd_buffer, const xg_buffer_params_t* params );
    xg_texture_h            ( *cmd_create_texture )                 ( xg_resource_cmd_buffer_h cmd_buffer, const xg_texture_params_t* params );
    void                    ( *cmd_destroy_buffer )                 ( xg_resource_cmd_buffer_h cmd_buffer, xg_buffer_h buffer, xg_resource_cmd_buffer_time_e time );
    void                    ( *cmd_destroy_texture )                ( xg_resource_cmd_buffer_h cmd_buffer, xg_texture_h texture, xg_resource_cmd_buffer_time_e time );

    //xg_query_buffer_h       ( *cmd_create_query_buffer )            ( xg_resource_cmd_buffer_h cmd_buffer, const xg_query_buffer_params_t* params );

    // TODO rename to set_viewport/scissor
    void                    ( *cmd_set_pipeline_viewport )          ( xg_cmd_buffer_h cmd_buffer, const xg_viewport_state_t* viewport, uint64_t key );
    void                    ( *cmd_set_pipeline_scissor )           ( xg_cmd_buffer_h cmd_buffer, const xg_scissor_state_t* scissor, uint64_t key );

    xg_buffer_h             ( *create_buffer )                      ( const xg_buffer_params_t* params );
    xg_texture_h            ( *create_texture )                     ( const xg_texture_params_t* params );

#if 0
    xg_query_pool_h         ( *create_query_pool )                  ( const xg_query_pool_params_t* params );
    void                    ( *cmd_query_timestamp )                ( xg_cmd_buffer_h cmd_buffer, xg_query_pool_h pool, xg_pipeline_stage_bit_e depenency, uint64_t key );
    void                    ( *cmd_readback_query_pool )            ( xg_cmd_buffer_h cmd_buffer, xg_buffer_h readback_buffer, uint64_t key );
    void                    ( *cmd_reset_query_pool )               ( xg_resource_cmd_buffer_h cmd_buffer, xg_query_pool_h pool, xg_resource_cmd_buffer_time_e time );
    void                    ( *cmd_destroy_query_pool )             ( xg_resource_cmd_buffer_h cmd_buffer, xg_query_pool_h pool, xg_resource_cmd_buffer_time_e time );
#endif

    void                    ( *enable_texture_view )                ( xg_texture_h texture, xg_texture_view_t view );

    bool                    ( *get_buffer_info )                    ( xg_buffer_info_t* info, xg_buffer_h buffer );
    bool                    ( *get_texture_info )                   ( xg_texture_info_t* info, xg_texture_h texture );

    xg_sampler_h            ( *create_sampler )                     ( const xg_sampler_params_t* params );
    xg_sampler_h            ( *get_default_sampler )                ( xg_device_h device, xg_default_sampler_e sampler );

    // TODO separate creation and build
    xg_raytrace_geometry_h  ( *create_raytrace_geometry )           ( const xg_raytrace_geometry_params_t* params );
    xg_raytrace_world_h     ( *create_raytrace_world )              ( const xg_raytrace_world_params_t* params );

    // Pipeline state
    // TODO move device into params struct? or always keep out? just pick one and standardize
    xg_graphics_pipeline_state_h    ( *create_graphics_pipeline )   ( xg_device_h device, const xg_graphics_pipeline_params_t* params );
    xg_compute_pipeline_state_h     ( *create_compute_pipeline )    ( xg_device_h device, const xg_compute_pipeline_params_t* params );
    xg_raytrace_pipeline_state_h    ( *create_raytrace_pipeline )   ( xg_device_h device, const xg_raytrace_pipeline_params_t* params );

    void                    ( *destroy_graphics_pipeline )          ( xg_graphics_pipeline_state_h pipeline );
    void                    ( *destroy_compute_pipeline )           ( xg_compute_pipeline_state_h pipeline );
    void                    ( *destroy_raytrace_pipeline )          ( xg_raytrace_pipeline_state_h pipeline );

    //xg_allocator_i          ( *get_default_allocator )              ( xg_device_h device, xg_memory_type_e type );
    void                    ( *get_allocator_info )                 ( xg_allocator_info_t* info, xg_device_h device, xg_memory_type_e type );

    // TODO add map_buffer/texture for convenience?
    //char*                 ( *map_alloc )                          ( const xg_alloc_t* alloc );
    //void                    ( *unmap_alloc )                        ( const xg_alloc_t* alloc );

    // TODO remove from workload, write a proper allocator for uniform data and use resource_cmd_buffer_time to free at workload completion
    xg_buffer_range_t       ( *write_workload_uniform )             ( xg_workload_h workload, void* data, size_t size );

    void                    ( *wait_all_workload_complete )         ( void );

#if xg_debug_enable_simple_frame_test_m
    void                    ( *debug_simple_frame )                 ( xg_device_h device, wm_window_h window );
#endif
} xg_i;
