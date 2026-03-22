#include "aud_source.h"

#include "aud_device.h"

static aud_source_state_t* aud_source_state;

void aud_source_load ( aud_source_state_t* state ) {
    aud_source_state = state;

    state->sources_array = std_virtual_heap_alloc_array_m ( aud_source_t, aud_source_max_sources_m );
    state->sources_freelist = std_freelist_m ( state->sources_array, aud_source_max_sources_m );
    state->sources_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( aud_source_max_sources_m ) );
    std_mem_zero_array_m ( state->sources_bitset, std_bitset_u64_count_m ( aud_source_max_sources_m ) );
    std_mutex_init ( &state->sources_mutex );
    state->active_sources_count = 0;
}

void aud_source_reload ( aud_source_state_t* state ) {
    aud_source_state = state;
}

void aud_source_unload ( void ) {
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, aud_source_state->sources_bitset, idx, std_bitset_u64_count_m ( aud_source_max_sources_m ) ) ) {
        aud_source_destroy ( idx );
    }

    std_virtual_heap_free ( aud_source_state->sources_array );
    std_virtual_heap_free ( aud_source_state->sources_bitset );

    std_mutex_deinit ( &state->sources_mutex );
}

aud_source_h aud_source_create ( const aud_source_params_t* params ) {
    aud_source_t* source = std_list_pop_m ( &aud_source_state->sources_freelist );

    source->params = *params;
    uint64_t size = params->sample_count * params->bits_per_sample / 8;
    void* buffer = std_virtual_heap_alloc_m ( size, 8 );
    source->stack = std_fixed_stack_m ( buffer, size );
    source->time_played = 0;
    source->volume = 1;
    source->active_idx = UINT64_MAX;
    source->total_time = ( double ) params->sample_count / ( params->sample_frequency * params->channel_count );

    aud_source_h handle = ( aud_source_h ) ( source - aud_source_state->sources_array );
    std_bitset_set ( aud_source_state->sources_bitset, handle );
    return handle;
}

void aud_source_feed ( aud_source_h source_handle, const void* data, uint64_t size ) {
    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    std_verify_m ( std_stack_write ( &source->stack, data, size ) );
}

void aud_source_play ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state->sources_mutex );

    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    aud_source_state->active_sources[aud_source_state->active_sources_count] = source;
    source->active_idx = aud_source_state->active_sources_count++;

    std_mutex_unlock ( &aud_source_state->sources_mutex );
}

void aud_source_pause ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state->sources_mutex );

    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    if ( aud_source_state->active_sources_count > 1 ) {
        aud_source_t* swap_source = aud_source_state->active_sources[aud_source_state->active_sources_count - 1];
        aud_source_state->active_sources[source->active_idx] = swap_source;
        swap_source->active_idx = source->active_idx;
    }
    source->active_idx = UINT64_MAX;
    aud_source_state->active_sources_count -= 1;

    std_mutex_unlock ( &aud_source_state->sources_mutex );
}

void aud_source_reset ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state->sources_mutex );

    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    source->time_played = 0;

    std_mutex_unlock ( &aud_source_state->sources_mutex );
}

void aud_source_destroy ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state->sources_mutex );

    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    if ( source->active_idx != UINT64_MAX ) {
        if ( aud_source_state->active_sources_count > 1 ) {
            aud_source_t* swap_source = aud_source_state->active_sources[aud_source_state->active_sources_count - 1];
            aud_source_state->active_sources[source->active_idx] = swap_source;
            swap_source->active_idx = source->active_idx;
        }
        source->active_idx = UINT64_MAX;
        aud_source_state->active_sources_count -= 1;
    }
    std_virtual_heap_free ( source->stack.begin );
    std_list_push ( &aud_source_state->sources_freelist, source );
    std_bitset_clear ( aud_source_state->sources_bitset, source_handle );

    std_mutex_unlock ( &aud_source_state->sources_mutex );
}

void aud_source_set_volume_scale ( aud_source_h source_handle, float scale ) {
    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    source->volume = scale > 0 ? scale : 0;
}

bool aud_source_get_info ( aud_source_info_t* info, aud_source_h source_handle ) {
    aud_source_t* source = &aud_source_state->sources_array[source_handle];

    info->sample_frequency = source->params.sample_frequency;
    info->bits_per_sample = source->params.bits_per_sample;
    info->channel_count = source->params.channel_count;
    info->sample_count = source->params.sample_count;
    info->time_played = source->time_played;
    info->total_time = source->total_time;

    return true;
}

void aud_source_skip ( aud_source_h source_handle, float seconds ) {
    aud_source_t* source = &aud_source_state->sources_array[source_handle];
    double time = source->time_played; 
    time += seconds;
    if ( time < 0 ) {
        time = 0;
    }
    double total_time = source->total_time;
    if ( time > total_time ) {
        time = total_time;
    }
    source->time_played = time;
}

void aud_source_output_to_device ( aud_device_h device_handle, uint64_t ms ) {
    // TODO avoid allocating on stack
    float buffer[aud_device_submit_block_max_ms_m * 44100 / 1000] = {0};
    
    double seconds = ms / 1000.0;

    aud_device_info_t device_info;
    aud_device_get_info ( &device_info, device_handle );    
    uint64_t frame_count = ( uint64_t ) ( seconds * device_info.sample_frequency );
    double frame_period = 1.0 / device_info.sample_frequency;
    uint64_t sample_count = frame_count * device_info.channel_count;

    std_mutex_lock ( &aud_source_state->sources_mutex );

    // TODO set a source to inactive once it's played all its samples
    for ( uint64_t source_it = 0; source_it < aud_source_state->active_sources_count; ++source_it ) {
        aud_source_t* source = aud_source_state->active_sources[source_it];
        uint32_t source_sample_stride = source->params.bits_per_sample / 8;

        uint64_t write_idx = 0;
        for ( uint64_t frame_it = 0; frame_it < frame_count; ++frame_it ) {
            double t = frame_it * frame_period + source->time_played;

            double source_frame = t * source->params.sample_frequency;
            uint64_t source_frame_idx_a = ( uint64_t ) source_frame;
            double decimal = source_frame - source_frame_idx_a;
            uint64_t source_frame_idx_b = ( uint64_t ) ( source_frame + 1 );

            float sample_value = 0;
            for ( uint32_t channel_it = 0; channel_it < device_info.channel_count; ++channel_it ) {
                // simple repeat of first sample if source has less channels than device
                // TODO better remap/force sources to be compatible with device
                if ( source->params.channel_count >= channel_it + 1 ) {
                    void* source_sample_a = source->stack.begin + source_frame_idx_a * source_sample_stride * source->params.channel_count + source_sample_stride * channel_it;
                    void* source_sample_b = source->stack.begin + source_frame_idx_b * source_sample_stride * source->params.channel_count + source_sample_stride * channel_it;
                    if ( source_sample_a >= source->stack.top || source_sample_b >= source->stack.top ) {
                        // update seconds to reflect the actual play time
                        seconds = ( double ) frame_it / device_info.sample_frequency;
                        goto next_source;
                    }
                    double sample_a = 0;
                    double sample_b = 0;

                    std_assert_m ( source_sample_a < source->stack.top );
                    std_assert_m ( source_sample_b < source->stack.top );

                    // source range -> [0,1]
                    switch ( source_sample_stride ) {
                        case 1:
                            sample_a = ( *( ( int8_t* ) ( source_sample_a ) ) ) / ( ( float ) UINT8_MAX );
                            sample_b = ( *( ( int8_t* ) ( source_sample_b ) ) ) / ( ( float ) UINT8_MAX );
                            break;

                        case 2:
                            sample_a = ( *( ( int16_t* ) ( source_sample_a ) ) ) / ( ( float ) UINT16_MAX );
                            sample_b = ( *( ( int16_t* ) ( source_sample_b ) ) ) / ( ( float ) UINT16_MAX );
                            break;

                        case 3:
                            std_not_implemented_m();
                            break;

                        case 4:
                            sample_a = ( *( ( int32_t* ) ( source_sample_a ) ) ) / ( ( float ) UINT32_MAX );
                            sample_b = ( *( ( int32_t* ) ( source_sample_b ) ) ) / ( ( float ) UINT32_MAX );
                            break;

                        default:
                            std_not_implemented_m();
                    }

                    sample_value = ( float ) ( sample_a * ( 1 - decimal ) + sample_b * decimal );
                    buffer[write_idx++] += sample_value * source->volume;
                } else {
                    buffer[write_idx++] += sample_value * source->volume;
                }

            }
        }

next_source:
        source->time_played += seconds;
    }

    std_mutex_unlock ( &aud_source_state->sources_mutex );

    char* device_buffer = aud_device_get_buffer ( device_handle );

    for ( uint64_t i = 0; i < sample_count; ++i ) {
        switch ( device_info.bits_per_sample ) {
            case 8:
                device_buffer[i] = ( uint8_t ) ( buffer[i] * ( float ) 0xff );
                break;

            case 16:
                * ( ( uint16_t* ) ( device_buffer + i * 2 ) ) = ( uint16_t ) ( buffer[i] * ( float ) 0xffff );
                break;

            case 24:
                std_not_implemented_m();

            case 32: {
                float v = buffer[i];
                * ( ( uint32_t* ) ( device_buffer + i * 4 ) ) = ( uint32_t ) ( v * ( float ) 0xffffffff );
            }
            break;

            default:
                std_not_implemented_m();
        }
    }

    aud_device_push_buffer ( device_handle, device_info.bits_per_sample / 8 * sample_count );
}
