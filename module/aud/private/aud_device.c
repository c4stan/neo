#include "aud_device.h"

#include <std_platform.h>
#include <std_list.h>
#include <std_mutex.h>
#include <std_atomic.h>
#include <std_string.h>
#include <std_time.h>
#include <std_allocator.h>

#if defined(std_platform_win32_m)
    #include <Mmsystem.h>
    #include <Mmreg.h>
#elif defined(std_platform_linux_m)
    #include <alsa/asoundlib.h>
#endif

typedef struct {
    bool is_submitted;
    aud_device_h device;
    std_alloc_t data_alloc;
    std_buffer_t data_buffer; // TODO remove
#if defined(std_platform_win32_m)
    WAVEHDR win32_header;
#endif
} aud_device_submit_context_t;

typedef enum {
    aud_devuce_existing_m = 1 << 0,
    aud_device_active_m   = 1 << 1,
} aud_device_f;

typedef struct {
    uint64_t os_id;
#if defined(std_platform_linux_m)
    uint64_t os_card_id;
#endif
    uint64_t os_handle;
    uint64_t guid;
    aud_device_f flags;
    aud_device_params_t params;

    aud_device_submit_context_t* submit_contexts;
    std_ring_t submit_ring;

    /*std_memory_h submit_blocks_handle;
    std_ring_t submit_blocks_ring;
    byte_t** submit_blocks;
    uint64_t submit_block_size;
    aud_device_submit_context_t* submit_contexts;*/
} aud_device_t;

typedef struct {
    aud_device_t* devices_array;
    aud_device_t* devices_freelist;
    std_mutex_t devices_mutex;
    size_t hardware_device_count;

    uint64_t guid;

#if defined(std_platform_linux_m)
    char** os_device_names;
#endif
} aud_device_state_t;

static aud_device_state_t aud_device_state;

void aud_device_init ( void ) {
    static aud_device_t devices_array[aud_device_max_devices_m];

    aud_device_state.devices_array = devices_array;
    aud_device_state.devices_freelist = std_static_freelist_m ( devices_array );
    std_mutex_init ( &aud_device_state.devices_mutex );
    aud_device_state.guid = 0;

    // TODO allow for dynamic updating of devices instead of just caching them all at the start
#if defined(std_platform_win32_m)
    size_t device_count = waveOutGetNumDevs();

    for ( size_t i = 0; i < device_count; ++i ) {
        aud_device_t* device = std_list_pop_m ( &aud_device_state.devices_freelist );
        device->os_id = i;
        device->guid = std_atomic_increment_u64 ( &aud_device_state.guid );
        device->flags = aud_devuce_existing_m;
    }

    aud_device_state.hardware_device_count = device_count;

#elif defined(std_platform_linux_m)

    uint32_t device_count = 0;

    //snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;
    //snd_pcm_stream_name ( stream );

    int card_id = -1;

    while ( snd_card_next ( &card_id ) >= 0 && card_id >= 0 ) {
        char card_name[32];
        std_str_format ( card_name, 32, "hw:" std_fmt_int_m, card_id );
        snd_ctl_t* card_handle;

        if ( snd_ctl_open ( &card_handle, card_name, 0 ) >= 0 ) {
            snd_ctl_card_info_t* card_info;
            snd_ctl_card_info_alloca ( &card_info );

            snd_ctl_card_info ( card_handle, card_info );

            int device_id = -1;

            while ( snd_ctl_pcm_next_device ( card_handle, &device_id ) >= 0 && device_id >= 0 ) {
#if 0
                snd_pcm_info_t* pcminfo;
                snd_pcm_info_alloca ( &pcminfo );

                snd_pcm_info_set_device ( pcminfo, device );
                snd_pcm_info_set_subdevice ( pcminfo, 0 );
                snd_pcm_info_set_stream ( pcminfo, stream );
                snd_ctl_pcm_info ( handle, pcminfo );
#endif
                const char* type = snd_ctl_card_info_get_id ( card_info );

                if ( std_str_cmp ( type, "PCH" ) == 0 ) {
                    aud_device_t* device = std_list_pop_m ( &aud_device_state.devices_freelist );
                    device->os_id = ( uint64_t ) device_id;
                    device->os_card_id = ( uint64_t ) card_id;
                    device->guid = std_atomic_increment_u64 ( &aud_device_state.guid );
                    device->flags = aud_devuce_existing_m;
                    ++device_count;
                }
            }

            snd_ctl_close ( card_handle );
        }
    }

    aud_device_state.hardware_device_count = device_count;
#endif
}

void aud_device_shutdown ( void ) {
    std_mutex_deinit ( &aud_device_state.devices_mutex );
}

size_t aud_device_get_count ( void ) {
    return aud_device_state.hardware_device_count;
}

size_t aud_device_get_list ( aud_device_h* devices, size_t cap ) {
    size_t count = 0;

    for ( size_t i = 0; i < aud_device_max_devices_m && count < cap; ++i ) {
        if ( aud_device_state.devices_array[i].flags & aud_devuce_existing_m ) {
            devices[count++] = i;
        }
    }

    return count;
}

bool aud_device_get_info ( aud_device_info_t* info, aud_device_h device_handle ) {
    aud_device_t* device = &aud_device_state.devices_array[device_handle];

    if ( std_unlikely_m ( ( device->flags & aud_devuce_existing_m ) == 0 ) ) {
        return false;
    }

    bool is_active = device->flags & aud_device_active_m;
    info->is_active = is_active;

    if ( is_active ) {
        info->channels = device->params.channels;
        info->sample_frequency = device->params.sample_frequency;
        info->bits_per_sample = device->params.bits_per_sample;
    } else {
        info->channels = 0;
        info->sample_frequency = 0;
        info->bits_per_sample = 0;
    }

#if defined(std_platform_win32_m)
    WAVEOUTCAPS caps;
    waveOutGetDevCaps ( device->os_id, &caps, sizeof ( caps ) );
    std_str_copy ( info->name, aud_device_name_size_m, caps.szPname );
#elif defined(std_platform_linux_m)
    char card_name[32];
    std_str_format ( card_name, 32, "hw:" std_fmt_int_m, device->os_card_id );
    snd_ctl_t* card_handle;
    snd_ctl_open ( &card_handle, card_name, 0 );

    snd_pcm_info_t* pcminfo;
    snd_pcm_info_alloca ( &pcminfo );
    snd_pcm_info_set_device ( pcminfo, device->os_id );
    snd_pcm_info_set_subdevice ( pcminfo, 0 );
    snd_pcm_info_set_stream ( pcminfo, SND_PCM_STREAM_PLAYBACK );
    snd_ctl_pcm_info ( card_handle, pcminfo );

    const char* name = snd_pcm_info_get_name ( pcminfo );
    std_str_copy ( info->name, aud_device_name_size_m, name );

    snd_ctl_close ( card_handle );
#endif

    return true;
}

bool aud_device_activate ( aud_device_h device_handle, const aud_device_params_t* params ) {
    // TODO avoid static, alloc new mem on activate?
    static aud_device_submit_context_t submit_contexts_array [aud_device_max_devices_m] [aud_device_max_submit_contexts_m];

    aud_device_t* device = &aud_device_state.devices_array[device_handle];

    if ( std_unlikely_m ( ( device->flags & aud_devuce_existing_m ) == 0 ) ) {
        return false;
    }

    bool result;
    uint64_t os_handle;

#if defined(std_platform_win32_m)
    WORD channel_count = ( WORD ) params->channels;
    WORD frequency = ( WORD ) params->sample_frequency;
    WORD bits_per_sample = ( WORD ) params->bits_per_sample;
    WORD channel_mask = 0;

    // TODO
    if ( channel_count == 1 ) {
        channel_mask = SPEAKER_FRONT_CENTER;
    } else if ( channel_count == 2 ) {
        channel_mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    } else {
        std_not_implemented_m();
    }

    HWAVEOUT waveout_handle;

    WAVEFORMATEX format = {
        .wFormatTag = WAVE_FORMAT_EXTENSIBLE,
        .nChannels = channel_count,
        .nSamplesPerSec = frequency,
        .nAvgBytesPerSec = frequency * channel_count * bits_per_sample / 8,
        .nBlockAlign = channel_count * bits_per_sample / 8,
        .wBitsPerSample = bits_per_sample,
        .cbSize = sizeof ( WAVEFORMATEXTENSIBLE ) - sizeof ( WAVEFORMATEX ),
    };
    WAVEFORMATEXTENSIBLE format_ex = {
        .Format = format,
        .Samples.wValidBitsPerSample = bits_per_sample,
        .dwChannelMask = channel_mask,
        .SubFormat = KSDATAFORMAT_SUBTYPE_PCM,
    };

    MMRESULT res = waveOutOpen ( &waveout_handle, ( UINT ) device->os_id, &format_ex.Format, ( DWORD_PTR ) NULL, 0, CALLBACK_NULL );

    result = res == MMSYSERR_NOERROR;
    os_handle = ( uint64_t ) waveout_handle;
#elif defined(std_platform_linux_m)
    std_not_implemented_m();



    result = false;
    os_handle = 0;
#endif

    if ( result ) {
        device->os_handle = os_handle;
        device->flags |= aud_device_active_m;
        device->params = *params;

        /*
        size_t device_idx = ( size_t ) device_handle;
        std_buffer_t submit_contexts_buffer = std_static_buffer_m ( submit_contexts_array );
        size_t submit_contexts_slice_size = sizeof ( aud_device_submit_context_t ) * aud_device_max_submit_contexts_m;
        std_buffer_t pool_buffer = std_buffer_slice ( submit_contexts_buffer, device_idx * submit_contexts_slice_size, submit_contexts_slice_size );
        std_mem_zero ( pool_buffer.base, pool_buffer.size );
        device->submit_contexts = std_circular_pool ( pool_buffer, sizeof ( aud_device_submit_context_t ) );
        */

        size_t device_idx = ( size_t ) device_handle;
        device->submit_contexts = submit_contexts_array[device_idx];

        uint64_t submit_block_size = aud_device_submit_block_max_ms_m * params->sample_frequency * params->bits_per_sample / 8;
        std_assert_m ( submit_block_size % 1000 == 0 );
        submit_block_size = submit_block_size / 1000;

        for ( uint64_t i = 0; i < aud_device_max_submit_contexts_m; ++i ) {
            aud_device_submit_context_t* context = &device->submit_contexts[i];
            std_mem_zero_m ( context );
            context->data_alloc = std_virtual_heap_alloc ( submit_block_size, 16 );
        }

        device->submit_ring = std_ring ( aud_device_max_submit_contexts_m );
    }

    return result;
}

bool aud_device_deactivate ( aud_device_h device ) {
    std_unused_m ( device );
    std_not_implemented_m();
    return false;
}

static void aud_device_recycle_submission_contexts ( aud_device_t* device, bool block_if_full ) {
    uint64_t count = std_ring_count ( &device->submit_ring );
    bool stall_begin = false;

    while ( count > 0 ) {
        aud_device_submit_context_t* submit_context = &device->submit_contexts[std_ring_bot_idx ( &device->submit_ring )];

        if ( !submit_context->is_submitted ) {
            break;
        }

#if defined(std_platform_win32_m)

        if ( ( submit_context->win32_header.dwFlags & WHDR_DONE ) == 0 ) {
            if ( count == aud_device_max_submit_contexts_m && block_if_full ) {
                if ( !stall_begin ) {
                    stall_begin = true;
                    std_log_warn_m ( "aud submission buffer is full! the thread will busy wait for a slot" );
                }

                continue;
            }

            break;
        }

        // context is done playing, can be disposed

        MMRESULT result = waveOutUnprepareHeader ( ( HWAVEOUT ) device->os_handle, &submit_context->win32_header, sizeof ( WAVEHDR ) );
        std_assert_m ( result == MMSYSERR_NOERROR );

        submit_context->is_submitted = false;
        std_ring_pop ( &device->submit_ring, 1 );

        count = std_ring_count ( &device->submit_ring );
#elif defined(std_platform_linux_m)
        std_not_implemented_m();
        std_unused_m ( stall_begin );
#endif
    }
}

void aud_device_play ( aud_device_h device_handle, std_buffer_t buffer ) {
    aud_device_t* device = &aud_device_state.devices_array[device_handle];

    if ( std_unlikely_m ( ( device->flags & aud_devuce_existing_m ) == 0 ) ) {
        return;
    }

    aud_device_recycle_submission_contexts ( device, true );

    aud_device_submit_context_t* submit_context = &device->submit_contexts[std_ring_top_idx ( &device->submit_ring )];
    std_ring_push ( &device->submit_ring, 1 );

#if defined(std_platform_win32_m)
    submit_context->device = device_handle;
    submit_context->data_buffer = buffer;
    submit_context->win32_header.lpData = ( char* ) buffer.base;
    submit_context->win32_header.dwBufferLength = ( DWORD ) buffer.size;
    submit_context->is_submitted = true;

    MMRESULT result = waveOutPrepareHeader ( ( HWAVEOUT ) device->os_handle, &submit_context->win32_header, sizeof ( WAVEHDR ) );
    std_assert_m ( result == MMSYSERR_NOERROR );

    result = waveOutWrite ( ( HWAVEOUT ) device->os_handle, &submit_context->win32_header, sizeof ( WAVEHDR ) );
    std_assert_m ( result == MMSYSERR_NOERROR );
#elif defined(std_platform_linux_m)
    std_not_implemented_m();
    std_unused_m ( submit_context );
#endif
}

byte_t* aud_device_get_buffer ( aud_device_h device_handle ) {
    aud_device_t* device = &aud_device_state.devices_array[device_handle];
    std_assert_m ( ( device->flags & aud_devuce_existing_m ) );

    aud_device_submit_context_t* submit_context = &device->submit_contexts[std_ring_top_idx ( &device->submit_ring )];

    return submit_context->data_alloc.buffer.base;
}

void aud_device_push_buffer ( aud_device_h device_handle, uint64_t buffer_size ) {
    aud_device_t* device = &aud_device_state.devices_array[device_handle];
    std_assert_m ( ( device->flags & aud_devuce_existing_m ) );

    aud_device_recycle_submission_contexts ( device, true );

    aud_device_submit_context_t* submit_context = &device->submit_contexts[std_ring_top_idx ( &device->submit_ring )];
    std_assert_m ( buffer_size <= submit_context->data_alloc.buffer.size );
    std_ring_push ( &device->submit_ring, 1 );

#if defined(std_platform_win32_m)
    submit_context->device = device_handle;
    submit_context->win32_header.lpData = ( char* ) submit_context->data_alloc.buffer.base;
    submit_context->win32_header.dwBufferLength = ( DWORD ) buffer_size;
    submit_context->is_submitted = true;

    MMRESULT result = waveOutPrepareHeader ( ( HWAVEOUT ) device->os_handle, &submit_context->win32_header, sizeof ( WAVEHDR ) );
    std_assert_m ( result == MMSYSERR_NOERROR );

    result = waveOutWrite ( ( HWAVEOUT ) device->os_handle, &submit_context->win32_header, sizeof ( WAVEHDR ) );
    std_assert_m ( result == MMSYSERR_NOERROR );
#elif defined(std_platform_linux_m)
    std_not_implemented_m();
#endif
}

const std_ring_t* aud_device_get_ring ( aud_device_h device_handle ) {
    aud_device_t* device = &aud_device_state.devices_array[device_handle];
    std_assert_m ( ( device->flags & aud_devuce_existing_m ) );
    return &device->submit_ring;
}

void aud_device_update_ring ( aud_device_h device_handle ) {
    aud_device_t* device = &aud_device_state.devices_array[device_handle];
    std_assert_m ( ( device->flags & aud_devuce_existing_m ) );

    aud_device_recycle_submission_contexts ( device, false );
}
