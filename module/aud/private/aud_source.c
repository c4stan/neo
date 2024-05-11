#include "aud_source.h"

#include "aud_device.h"

#include <std_list.h>
#include <std_mutex.h>
#include <std_allocator.h>

std_warnings_ignore_m ( "-Wunused-variable" )

typedef struct {
    aud_source_t* sources_array;
    aud_source_t* sources_freelist;
    std_mutex_t sources_mutex;

    aud_source_t* active_sources[aud_source_max_playing_sources_m];
    uint64_t active_sources_count;
} aud_source_state_t;

static aud_source_state_t aud_source_state;

void aud_source_init ( void ) {
    static aud_source_t sources_array[aud_source_max_sources_m];

    aud_source_state.sources_array = sources_array;
    aud_source_state.sources_freelist = std_static_freelist_m ( sources_array );
    std_mutex_init ( &aud_source_state.sources_mutex );

    aud_source_state.active_sources_count = 0;
}

aud_source_h aud_source_create ( const aud_source_params_t* params ) {
    aud_source_t* source = std_list_pop_m ( &aud_source_state.sources_freelist );

    source->params = *params;
    uint64_t size = params->capacity_ms * params->sample_frequency * params->bits_per_sample / 8 / 1000;
    //source->buffer = std_virtual_buffer_reserve ( std_align ( size, std_virtual_page_size() ) );
    void* buffer = std_virtual_heap_alloc ( size, 8 );
    source->stack = std_stack ( buffer, size );
    source->time_played = 0;
    source->volume = 1;
    source->active_idx = UINT64_MAX;

    aud_source_h handle = ( aud_source_h ) ( source - aud_source_state.sources_array );
    return handle;
}

void aud_source_feed ( aud_source_h source_handle, const void* data, uint64_t size ) {
    aud_source_t* source = &aud_source_state.sources_array[source_handle];
    //void* base = source->buffer.base + source->buffer.top;
    //std_virtual_buffer_push ( &source->buffer, size );
    //std_mem_copy ( base, data, size );
    std_stack_write ( &source->stack, data, size );
}

void aud_source_play ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state.sources_mutex );

    aud_source_t* source = &aud_source_state.sources_array[source_handle];
    aud_source_state.active_sources[aud_source_state.active_sources_count] = source;
    source->active_idx = aud_source_state.active_sources_count++;

    std_mutex_unlock ( &aud_source_state.sources_mutex );
}

void aud_source_pause ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state.sources_mutex );

    aud_source_t* source = &aud_source_state.sources_array[source_handle];
    aud_source_state.active_sources[source->active_idx] = aud_source_state.active_sources[aud_source_state.active_sources_count--];
    source->active_idx = UINT64_MAX;

    std_mutex_unlock ( &aud_source_state.sources_mutex );
}

void aud_source_reset ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state.sources_mutex );

    aud_source_t* source = &aud_source_state.sources_array[source_handle];
    source->time_played = 0;

    std_mutex_unlock ( &aud_source_state.sources_mutex );
}

void aud_source_destroy ( aud_source_h source_handle ) {
    std_mutex_lock ( &aud_source_state.sources_mutex );

    aud_source_t* source = &aud_source_state.sources_array[source_handle];
    aud_source_state.active_sources[source->active_idx] = aud_source_state.active_sources[aud_source_state.active_sources_count--];
    std_virtual_heap_free ( source->stack.begin );
    //std_virtual_buffer_free ( &source->buffer );
    std_list_push ( &aud_source_state.sources_freelist, source );

    std_mutex_unlock ( &aud_source_state.sources_mutex );
}

void aud_source_set_volume_scale ( aud_source_h source_handle, float scale ) {
    aud_source_t* source = &aud_source_state.sources_array[source_handle];

    source->volume = scale;
}

bool aud_source_get_info ( aud_source_info_t* info, aud_source_h source_handle ) {
    aud_source_t* source = &aud_source_state.sources_array[source_handle];

    info->sample_frequency = source->params.sample_frequency;
    info->bits_per_sample = source->params.bits_per_sample;
    info->time_played = source->time_played;

    return true;
}

void aud_source_output_to_device ( aud_device_h device_handle, uint64_t ms ) {
    // TODO avoid allocating on stack?
    float buffer[aud_device_submit_block_max_ms_m * 44100 / 1000] = {0};

    double seconds = ms / 1000.0;

    aud_device_info_t device_info;
    aud_device_get_info ( &device_info, device_handle );
    uint64_t sample_count = ( uint64_t ) ( seconds * device_info.sample_frequency );
    double sample_period = 1.0 / device_info.sample_frequency;

    std_mutex_lock ( &aud_source_state.sources_mutex );

    // TODO set a source to inactive once it's played all its samples
    for ( uint64_t source_it = 0; source_it < aud_source_state.active_sources_count; ++source_it ) {
        aud_source_t* source = aud_source_state.active_sources[source_it];
        uint64_t source_sample_stride = source->params.bits_per_sample / 8;

        for ( uint64_t sample_it = 0; sample_it < sample_count; ++sample_it ) {
            double t = sample_it * sample_period + source->time_played;

            double source_sample = t * source->params.sample_frequency;
            uint64_t source_sample_idx_a = ( uint64_t ) source_sample;
            double decimal = source_sample - source_sample_idx_a;
            uint64_t source_sample_idx_b = ( uint64_t ) ( source_sample + 1 );

            char* source_sample_a = source->stack.begin + source_sample_idx_a * source_sample_stride;
            char* source_sample_b = source->stack.begin + source_sample_idx_b * source_sample_stride;
            double sample_a = 0;
            double sample_b = 0;

            // source range -> [0,1]
            switch ( source_sample_stride ) {
                case 1:
                    sample_a = ( ( uint8_t ) ( *source_sample_a ) ) / ( ( float ) UINT8_MAX );
                    sample_b = ( ( uint8_t ) ( *source_sample_b ) ) / ( ( float ) UINT8_MAX );
                    break;

                case 2:
                    sample_a = ( ( int16_t ) ( *source_sample_a ) ) / ( ( float ) UINT16_MAX );
                    sample_b = ( ( int16_t ) ( *source_sample_b ) ) / ( ( float ) UINT16_MAX );
                    break;

                case 3:
                    std_not_implemented_m();
                    break;

                case 4:
                    sample_a = ( ( int32_t ) ( *source_sample_a ) ) / ( ( float ) UINT32_MAX );
                    sample_b = ( ( int32_t ) ( *source_sample_b ) ) / ( ( float ) UINT32_MAX );
                    break;

                default:
                    std_not_implemented_m();
            }

            float sample_value = ( float ) ( sample_a * ( 1 - decimal ) + sample_b * decimal );
            // TODO zero the buffer at some point
            buffer[sample_it] += sample_value * source->volume;
        }

        source->time_played += seconds;
    }

    std_mutex_unlock ( &aud_source_state.sources_mutex );

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
