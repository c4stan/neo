#include "xg_vk_sampler.h"

#include "xg_vk_device.h"
#include "xg_vk_enum.h"

#include <std_mutex.h>

static xg_vk_sampler_state_t* xg_vk_sampler_state;

#define xg_vk_sampler_bitset_u64_count_m std_div_ceil_m ( xg_vk_max_samplers_m, 64 )

void xg_vk_sampler_load ( xg_vk_sampler_state_t* state ) {
    xg_vk_sampler_state = state;

    xg_vk_sampler_state->samplers_array = std_virtual_heap_alloc_array_m ( xg_vk_sampler_t, xg_vk_max_samplers_m );
    xg_vk_sampler_state->samplers_freelist = std_freelist_m ( xg_vk_sampler_state->samplers_array, xg_vk_max_samplers_m );
    xg_vk_sampler_state->samplers_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_sampler_bitset_u64_count_m );
    std_mem_zero_array_m ( xg_vk_sampler_state->samplers_bitset, xg_vk_sampler_bitset_u64_count_m );
    std_mutex_init ( &xg_vk_sampler_state->samplers_mutex );

    for ( uint32_t i = 0; i < xg_vk_max_devices_m; ++i ) {
        for ( uint32_t j = 0; j < xg_default_sampler_count_m; ++j ) {
            xg_vk_sampler_state->default_samplers[i][j] = xg_null_handle_m;
        }
    }
}

void xg_vk_sampler_reload ( xg_vk_sampler_state_t* state ) {
    xg_vk_sampler_state = state;
}

void xg_vk_sampler_unload ( void ) {
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_sampler_state->samplers_bitset, idx, xg_vk_sampler_bitset_u64_count_m ) ) {
        xg_sampler_h handle = idx;
        xg_vk_sampler_t* sampler = &xg_vk_sampler_state->samplers_array[idx];
        std_log_info_m ( "Destroying sampler " std_fmt_u64_m ": " std_fmt_str_m, idx, sampler->params.debug_name );
        xg_sampler_destroy ( handle );
        ++idx;
    }

    std_virtual_heap_free ( xg_vk_sampler_state->samplers_array );
    std_virtual_heap_free ( xg_vk_sampler_state->samplers_bitset );
    std_mutex_deinit ( &xg_vk_sampler_state->samplers_mutex );
}

xg_sampler_h xg_sampler_create ( const xg_sampler_params_t* params ) {
    const xg_vk_device_t* device = xg_vk_device_get ( params->device );

    VkSamplerCreateInfo create_info;
    create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create_info.pNext = NULL;
    create_info.flags = 0;
    create_info.magFilter = xg_sampler_filter_to_vk ( params->mag_filter );
    create_info.minFilter = xg_sampler_filter_to_vk ( params->min_filter );
    create_info.mipmapMode = xg_sampler_mipmap_filter_to_vk ( params->mipmap_filter );
    create_info.addressModeU = xg_sampler_address_mode_to_vk ( params->address_mode );
    create_info.addressModeV = xg_sampler_address_mode_to_vk ( params->address_mode );
    create_info.addressModeW = xg_sampler_address_mode_to_vk ( params->address_mode );
    create_info.mipLodBias = 0.0; // TODO use params
    create_info.anisotropyEnable = VK_FALSE;
    create_info.maxAnisotropy = 1;
    create_info.compareEnable = VK_FALSE;
    create_info.compareOp = VK_COMPARE_OP_NEVER;
    create_info.minLod = 0.0;
    create_info.maxLod = VK_LOD_CLAMP_NONE; // TODO use params
    create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    create_info.unnormalizedCoordinates = VK_FALSE;
    VkSampler vk_sampler;
    xg_vk_assert_m ( vkCreateSampler ( device->vk_handle, &create_info, NULL, &vk_sampler ) );

    std_mutex_lock ( &xg_vk_sampler_state->samplers_mutex );
    xg_vk_sampler_t* sampler = std_list_pop_m ( &xg_vk_sampler_state->samplers_freelist );
    std_mutex_unlock ( &xg_vk_sampler_state->samplers_mutex );

    sampler->vk_handle = vk_sampler;
    sampler->params = *params;

    uint64_t idx = sampler - xg_vk_sampler_state->samplers_array;
    std_bitset_set ( xg_vk_sampler_state->samplers_bitset, idx );

    xg_sampler_h handle = ( xg_sampler_h ) idx;
    return handle;
}

static xg_sampler_h xg_sampler_create_default ( xg_device_h device, xg_default_sampler_e sampler_enum ) {
    xg_sampler_params_t params;

    switch ( sampler_enum ) {
        case xg_default_sampler_point_clamp_m:
            params = xg_sampler_params_m (
                .device = device,
                .min_filter = xg_sampler_filter_point_m,
                .mag_filter = xg_sampler_filter_point_m,
                .mipmap_filter = xg_sampler_filter_point_m,
                .address_mode = xg_sampler_address_mode_clamp_m,
            );
            std_str_copy_static_m ( params.debug_name, "default point clamp" );
            break;

        case xg_default_sampler_linear_clamp_m:
            params = xg_sampler_params_m (
                .device = device,
                .min_filter = xg_sampler_filter_linear_m,
                .mag_filter = xg_sampler_filter_linear_m,
                .mipmap_filter = xg_sampler_filter_linear_m,
                .address_mode = xg_sampler_address_mode_clamp_m,
            );
            std_str_copy_static_m ( params.debug_name, "default linear clamp" );
            break;

        default:
            std_log_error_m ( "Unkown default sampler" );
            break;
    }

    return xg_sampler_create ( &params );
}

// TODO pre-create on device init?
xg_sampler_h xg_sampler_get_default ( xg_device_h device, xg_default_sampler_e sampler_enum ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device );
    xg_sampler_h sampler = xg_vk_sampler_state->default_samplers[device_idx][sampler_enum];

    if ( sampler == xg_null_handle_m ) {
        sampler = xg_sampler_create_default ( device, sampler_enum );
        xg_vk_sampler_state->default_samplers[device_idx][sampler_enum] = sampler;
    }

    return sampler;
}

bool xg_sampler_get_info ( xg_sampler_h sampler_handle, xg_sampler_info_t* info ) {
    xg_vk_sampler_t* sampler = &xg_vk_sampler_state->samplers_array[sampler_handle];

    info->device = sampler->params.device;
    info->mag_filter = sampler->params.mag_filter;
    info->min_filter = sampler->params.min_filter;
    info->mipmap_filter = sampler->params.mipmap_filter;
    info->anisotropic = sampler->params.enable_anisotropy;
    info->min_mip = sampler->params.min_mip;
    info->max_mip = sampler->params.max_mip;
    info->mip_bias = sampler->params.mip_bias;
    info->address_mode = sampler->params.address_mode;
    std_str_copy_static_m ( info->debug_name, sampler->params.debug_name );

    return true;
}

bool xg_sampler_destroy ( xg_sampler_h sampler_handle ) {
    uint64_t idx = sampler_handle;
    xg_vk_sampler_t* sampler = &xg_vk_sampler_state->samplers_array[idx];
    const xg_vk_device_t* device = xg_vk_device_get ( sampler->params.device );
    vkDestroySampler ( device->vk_handle, sampler->vk_handle, NULL );
    std_bitset_clear ( xg_vk_sampler_state->samplers_bitset, idx );
    return true;
}

const xg_vk_sampler_t* xg_vk_sampler_get ( xg_sampler_h sampler_handle ) {
    const xg_vk_sampler_t* sampler = &xg_vk_sampler_state->samplers_array[sampler_handle];
    return sampler;
}
