#include <std_main.h>

#include <std_thread.h>
#include <std_time.h>

#include <aud.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

// http://countercomplex.blogspot.com/2011/10/algorithmic-symphonies-from-one-line-of.html
static void write_source_wave ( char* buffer, size_t size, uint32_t id ) {
    for ( uint32_t t = 0; t < size; ++t ) {
        switch ( id ) {
            case 0:
                buffer[t] = ( char ) ( ( ( t >> 6 | t | t >> ( t >> 16 ) ) * 10 + ( ( t >> 11 ) & 7 ) ) & 0xff );
                break;

            case 1:
                buffer[t] = ( char ) ( ( ( t >> 7 | t | t >> 6 ) * 10 + 4 * ( t & t >> 13 | t >> 6 ) ) & 0xff );
                break;

            case 2:
                buffer[t] = ( char ) ( ( ( ( t * ( t >> 8 | t >> 9 ) & 46 & t >> 8 ) ) ^ ( t & t >> 13 | t >> 6 ) ) & 0xff );
                break;

            default:
                std_not_implemented_m();
        }

    }
}

static void run_aud_test ( void ) {
    aud_i* aud = std_module_load_m ( aud_module_name_m );
    std_assert_m ( aud );

    // device
    size_t device_count = aud->get_devices_count();
    std_log_info_m ( "Device count: " std_fmt_size_m, device_count );

    aud_device_h devices[aud_device_max_devices_m];
    aud->get_devices ( devices, aud_device_max_devices_m );

    for ( size_t i = 0; i < device_count; ++i ) {
        aud_device_info_t info;
        bool result = aud->get_device_info ( &info, devices[i] );
        std_assert_m ( result );
        std_log_info_m ( "Device " std_fmt_size_m ": " std_fmt_str_m, i, info.name );
    }

    aud_device_h device = devices[0];
    {
        aud_device_params_t params;
        params.channel_count = 2;
        params.sample_frequency = 48000;
        params.bits_per_sample = 32;
        bool result = aud->activate_device ( device, &params );
        std_assert_m ( result );
        std_log_info_m ( "\tChannels: " std_fmt_size_m, params.channel_count );
        std_log_info_m ( "\tSample frequency: " std_fmt_size_m, params.sample_frequency );
        std_log_info_m ( "\tbpp: " std_fmt_size_m, params.bits_per_sample );
    }

    aud_device_info_t device_info;
    aud->get_device_info ( &device_info, device );
    std_log_info_m ( "Picking device 0: " std_fmt_str_m, device_info.name );

    // source0
    aud_source_h source0;
    {
        std_log_info_m ( "Creating audio source 0..." );
        aud_source_params_t params = {
            .sample_frequency = 8000,
            .bits_per_sample = 8,
            .sample_count = 8000 * 60,
            .channel_count = 1,
        };
        source0 = aud->create_source ( &params );
        std_log_info_m ( "\tsample frequency: " std_fmt_size_m, params.sample_frequency );
        std_log_info_m ( "\tbps: " std_fmt_size_m, params.bits_per_sample );
        std_log_info_m ( "\tchannels: " std_fmt_size_m, params.channel_count );

        char buffer[8000 * 60] = {0};
        write_source_wave ( buffer, sizeof ( buffer ), 0 );
        aud->feed_source ( source0, buffer, sizeof ( buffer ) );
    }

    // source1
    aud_source_h source1;
    {
        std_log_info_m ( "Creating audio source 1..." );
        aud_source_params_t params = {
            .sample_frequency = 8000,
            .bits_per_sample = 8,
            .sample_count = 8000 * 60,
            .channel_count = 1,
        };
        source1 = aud->create_source ( &params );
        std_log_info_m ( "\tsample frequency: " std_fmt_size_m, params.sample_frequency );
        std_log_info_m ( "\tbps: " std_fmt_size_m, params.bits_per_sample );
        std_log_info_m ( "\tchannels: " std_fmt_size_m, params.channel_count );

        char buffer[8000 * 60] = {0};
        write_source_wave ( buffer, sizeof ( buffer ), 1 );
        aud->feed_source ( source1, buffer, sizeof ( buffer ) );
    }

    // source2
    aud_source_h source2 = aud_null_handle_m;
    {
        const char* input_filename = "c.a.n.d.y..mp3";
        //const char* input_filename = "output.mp3";
        mp3dec_ex_t mp3dec;
        if ( mp3dec_ex_open ( &mp3dec, input_filename, MP3D_SEEK_TO_SAMPLE ) == 0 ) {
            std_log_info_m ( "Creating audio source 2..." );
            aud_source_params_t params = {
                .sample_frequency = mp3dec.info.hz,
                .bits_per_sample = 16,
                .sample_count = mp3dec.samples,
                .channel_count = 2,
            };
            source2 = aud->create_source ( &params );
            std_log_info_m ( "\tsample frequency: " std_fmt_size_m, params.sample_frequency );
            std_log_info_m ( "\tbps: " std_fmt_size_m, params.bits_per_sample );
            std_log_info_m ( "\tchannels: " std_fmt_size_m, params.channel_count );

            int16_t* buffer = std_virtual_heap_alloc_array_m ( int16_t, mp3dec.samples );
            uint64_t samples_read = mp3dec_ex_read ( &mp3dec, buffer, mp3dec.samples );
            std_verify_m ( samples_read == mp3dec.samples );
            aud->feed_source ( source2, buffer, sizeof ( int16_t ) * mp3dec.samples );
            std_virtual_heap_free ( buffer );
            mp3dec_ex_close ( &mp3dec );
        }
    }

    // play
    std_log_info_m ( "Playing sources..." );
    if ( source2 != aud_null_handle_m ) {
        aud->play_source ( source2 );
        aud->set_source_volume ( source2, 0.2f );
    } else {
        aud->play_source ( source0 );
        aud->set_source_volume ( source0, 0.2f );
        aud->play_source ( source1 );
        aud->set_source_volume ( source1, 0.2f );
        source2 = source0;
    }

    const std_ring_t* device_ring = aud->get_device_ring ( device );
    uint64_t ring_capacity = std_ring_capacity ( device_ring );

    bool first_print = true;
    uint64_t step_ms = 50;

    while ( true ) {
        aud_source_info_t source_info;
        aud->get_source_info ( &source_info, source2 );
        float total_duration = source_info.sample_count / source_info.sample_frequency / device_info.channel_count;
        float bar_tick = total_duration * 1000.f / 60.f;

        aud->update_device_ring ( device );
        uint64_t ring_count = std_ring_count ( device_ring );

        {
            char bar[100];
            size_t i = 0;

            bar[i++] = '[';

            uint64_t ms = ( uint64_t ) ( source_info.time_played * 1000.f );

            while ( ms > bar_tick && i < 60 ) {
                bar[i++] = '=';
                ms -= bar_tick;
            }

            while ( i < 60 ) {
                bar[i++] = ' ';
            }

            bar[i++] = ']';

            const char* prefix = "";

            if ( first_print ) {
                prefix = "\n";
            }

            first_print = false;

            std_log_m ( 0, std_fmt_str_m std_fmt_prevline_m std_fmt_str_m " " std_fmt_u64_pad_m ( 2 ) "/" std_fmt_u64_m, prefix, bar, ring_count, ring_capacity );
        }

        if ( source_info.time_played < total_duration ) {
            if ( ring_count < ring_capacity ) {
                aud->output_to_device ( device, step_ms );
            }
        } else if ( ring_count == 0 ) {
            break;
        }

        std_thread_this_sleep ( step_ms / 2 );
    }

    std_module_unload_m ( aud );
}

void std_main ( void ) {
    run_aud_test();
    std_log_info_m ( "AUD_TEST COMPLETE!" );
}
