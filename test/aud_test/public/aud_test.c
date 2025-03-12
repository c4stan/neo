#include <std_main.h>

#include <std_thread.h>
#include <std_time.h>

#include <aud.h>

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
    aud_device_info_t info;
    aud->get_device_info ( &info, device );
    std_log_info_m ( "Picking device 0: " std_fmt_str_m, info.name );

    {
        aud_device_params_t params;
        params.channels = 1;
        params.sample_frequency = 8000;
        params.bits_per_sample = 32;
        bool result = aud->activate_device ( device, &params );
        std_assert_m ( result );
        std_log_info_m ( "\tChannels: " std_fmt_size_m, params.channels );
        std_log_info_m ( "\tSample frequency: " std_fmt_size_m, params.sample_frequency );
        std_log_info_m ( "\tbpp: " std_fmt_size_m, params.bits_per_sample );
    }

    aud_source_h source0;
    {
        std_log_info_m ( "Creating audio source 1..." );
        aud_source_params_t params;
        params.sample_frequency = 8000;
        params.bits_per_sample = 8;
        params.capacity_ms = 100 * 1000;
        source0 = aud->create_source ( &params );
        std_log_info_m ( "\tSample frequency: " std_fmt_size_m, params.sample_frequency );
        std_log_info_m ( "\tbpp: " std_fmt_size_m, params.bits_per_sample );
    }

    char buffer0[8000 * 60] = {0};
    write_source_wave ( buffer0, sizeof ( buffer0 ), 0 );
    aud->feed_source ( source0, buffer0, sizeof ( buffer0 ) );

    aud_source_h source1;
    {
        std_log_info_m ( "Creating audio source 2..." );
        aud_source_params_t params;
        params.sample_frequency = 8000;
        params.bits_per_sample = 8;
        params.capacity_ms = 100 * 1000;
        source1 = aud->create_source ( &params );
        std_log_info_m ( "\tSample frequency: " std_fmt_size_m, params.sample_frequency );
        std_log_info_m ( "\tbpp: " std_fmt_size_m, params.bits_per_sample );
    }

    char buffer1[8000 * 60] = {0};
    write_source_wave ( buffer1, sizeof ( buffer1 ), 1 );
    aud->feed_source ( source1, buffer1, sizeof ( buffer1 ) );

    std_log_info_m ( "Playing sources..." );
    aud->play_source ( source0 );
    aud->set_source_volume ( source0, 0.2f );

    aud->play_source ( source1 );
    aud->set_source_volume ( source1, 0.2f );

    const std_ring_t* device_ring = aud->get_device_ring ( device );
    uint64_t ring_capacity = std_ring_capacity ( device_ring );

    aud_source_info_t source_info;
    aud->get_source_info ( &source_info, source0 );

    bool first_print = true;
    uint64_t step_ms = 50;

    while ( true ) {
        aud->get_source_info ( &source_info, source0 );

        aud->update_device_ring ( device );
        uint64_t ring_count = std_ring_count ( device_ring );

        {
            char bar[100];
            size_t i = 0;

            bar[i++] = '[';

            uint64_t ms = ( uint64_t ) ( source_info.time_played * 1000.f );

            while ( ms > 1000 && i < 60 ) {
                bar[i++] = '=';
                ms -= 1000;
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

        if ( source_info.time_played < 60 ) {
            if ( ring_count < ring_capacity ) {
                aud->output_to_device ( device, step_ms );
            }
        } else if ( ring_count == 0 ) {
            break;
        }

        std_thread_this_sleep ( step_ms / 2 );
    }
}

void std_main ( void ) {
    run_aud_test();
    std_log_info_m ( "aud_test_m COMPLETE!" );
}
