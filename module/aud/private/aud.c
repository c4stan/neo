#include <aud.h>

#include "aud_device.h"
#include "aud_source.h"

typedef struct {
    aud_i api;
    aud_device_state_t device;
    aud_source_state_t source;
} aud_state_t;

std_module_implement_state_m ( aud );

// https://ourmachinery.com/post/writing-a-low-level-sound-system/

static void aud_api_init ( aud_i* aud ) {
    aud->get_devices_count = aud_device_get_count;
    aud->get_devices = aud_device_get_list;
    aud->get_device_info = aud_device_get_info;

    aud->get_device_ring = aud_device_get_ring;
    aud->update_device_ring = aud_device_update_ring;

    aud->activate_device = aud_device_activate;

    aud->create_source = aud_source_create;
    aud->feed_source = aud_source_feed;
    aud->play_source = aud_source_play;
    aud->pause_source = aud_source_pause;
    aud->reset_source = aud_source_reset;
    aud->destroy_source = aud_source_destroy;
    aud->get_source_info = aud_source_get_info;
    aud->set_source_volume = aud_source_set_volume_scale;

    aud->output_to_device = aud_source_output_to_device;
}

void* aud_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    aud_state_t* state = aud_state_alloc();

    aud_device_load ( &state->device );
    aud_source_load ( &state->source );

    aud_api_init ( &state->api );
    aud_state_bind ( state );
    return &state->api;
}

void aud_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );
    aud_state_t* state = ( aud_state_t* ) api;
    aud_device_reload ( &state->device );
    aud_source_reload ( &state->source );
    aud_api_init ( &state->api );
    aud_state_bind ( state );
}

void aud_unload ( void ) {
    aud_source_unload();
    aud_device_unload();
    aud_state_free();
}
