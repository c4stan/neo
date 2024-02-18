#pragma once

#include <std_module.h>
#include <std_buffer.h>
#include <std_queue.h>

#define aud_module_name_m aud
std_module_export_m void* aud_load ( void* );
std_module_export_m void aud_unload ( void );

typedef uint64_t aud_device_h;
typedef uint64_t aud_source_h;

typedef struct {
    size_t channels;
    size_t sample_frequency;
    size_t bits_per_sample;
} aud_device_params_t;

typedef struct {
    uint64_t capacity_ms;
    uint64_t sample_frequency;
    uint64_t bits_per_sample;
} aud_source_params_t;

typedef struct {
    // TODO find a way to query for supported activation params
    bool is_active;
    // zero if not active
    size_t channels;
    size_t sample_frequency;
    size_t bits_per_sample;

    char name[aud_device_name_size_m];
} aud_device_info_t;

typedef struct {
    uint64_t sample_frequency;
    uint64_t bits_per_sample;
    double time_played;
} aud_source_info_t;

typedef struct {
    size_t ( *get_devices_count ) ( void );
    size_t ( *get_devices ) ( aud_device_h* devices, size_t cap );
    bool ( *get_device_info ) ( aud_device_info_t* info, aud_device_h device );

    const std_ring_t* ( *get_device_ring ) ( aud_device_h device );
    void ( *update_device_ring ) ( aud_device_h device );

    bool ( *activate_device ) ( aud_device_h device, const aud_device_params_t* params );
    bool  ( *deactivate_device ) ( aud_device_h deivce );

    void ( *play ) ( aud_device_h device, std_buffer_t buffer );

    aud_source_h ( *create_source ) ( const aud_source_params_t* params );
    void ( *feed_source ) ( aud_source_h source, const void* data, uint64_t size );
    void ( *play_source ) ( aud_source_h source );
    void ( *pause_source ) ( aud_source_h source );
    void ( *reset_source ) ( aud_source_h source );
    void ( *destroy_source ) ( aud_source_h source );
    bool ( *get_source_info ) ( aud_source_info_t* info, aud_source_h source );
    void ( *set_source_volume ) ( aud_source_h source, float volume_scale );

    // samples, mixes and submits audio data to the device for playback
    void ( *output_to_device ) ( aud_device_h device, uint64_t milliseconds );
} aud_i;
