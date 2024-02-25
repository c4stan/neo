#include "aud_mixer.h"

#include <std_log.h>

#include "aud_source.h"
#include "aud_device.h"

void aud_mixer_mix_sources ( aud_source_t* sources, uint64_t count, aud_device_h device_handle, double milliseconds ) {
    aud_device_t* device = aud_device_get ( device_handle );

    double device_sample_period = 1.0 / device->params.sample_frequency;

    if ( device_sample_period * 1000 < milliseconds ) {
        std_log_warn_m ( "Requested mixing time duration is smaller than the selected device sample period" );
        return;
    }

    // amount of samples to feed to the device
    uint64_t sample_count = ( uint64_t ) ( ms * device->params.sample_frequency / 1000.0 );
    std_assert_m ( sample_count > 0 );

    for ( uint64_t source_it = 0; source_it < aud_source_state.active_sources_count; ++source_it ) {
        aud_source_t* source = sources[source_it];

        // resample the source data according to the device format
        uint64_t source_sample_stride = source->params.bits_per_sample / 8;

        for ( uint64_t sample_it = 0; sample_it < sample_count; ++sample_it ) {
            double t = sample_it * device_sample_period + source->played_time;

            double source_sample = t * source->sample_frequency;
            uint64_t source_sample_idx_a = ( uint64_t ) source_sample;
            double decimal = source_sample - source_sample_idx_a;
            uint64_t source_sample_idx_b = ( uint64_t ) ( source_sample + 1 );

            char* source_sample_a = source->buffer.base + source_sample_idx_a * source_sample_stride;
            char* source_sample_b = source->buffer.base + source_sample_idx_b * source_sample_stride;

            float sample_a, sample_b;

            switch ( source_sample_stride ) {
                case 8:
                    sample_a = ( ( int8_t ) ( *source_sample_a ) ) / ( ( float ) INT8_MAX );
                    sample_a = ( ( int8_t ) ( *source_sample_b ) ) / ( ( float ) INT8_MAX );
                    break;

                case 16:
                    sample_a = ( ( int16_t ) ( *source_sample_a ) ) / ( ( float ) INT16_MAX );
                    sample_a = ( ( int16_t ) ( *source_sample_b ) ) / ( ( float ) INT16_MAX );
                    break;

                case 24:
                    std_not_implemented_m();
                    break;

                case 32:
                    sample_a = ( ( int32_t ) ( *source_sample_a ) ) / ( ( float ) INT32_MAX );
                    sample_a = ( ( int32_t ) ( *source_sample_b ) ) / ( ( float ) INT32_MAX );
                    break;
            }

            float result = sample_a * ( 1 - decimal ) + sample_b * decimal;
            std_unused_m ( result ); // TODO
        }
    }
}
