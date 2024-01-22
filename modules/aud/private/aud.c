#include <aud.h>

#include "aud_device.h"
#include "aud_source.h"

static aud_i s_api;

// https://ourmachinery.com/post/writing-a-low-level-sound-system/

static aud_i aud_api ( void ) {
    aud_i aud = {0};

    aud.get_devices_count = aud_device_get_count;
    aud.get_devices = aud_device_get_list;
    aud.get_device_info = aud_device_get_info;

    aud.get_device_ring = aud_device_get_ring;
    aud.update_device_ring = aud_device_update_ring;

    aud.activate_device = aud_device_activate;

    aud.create_source = aud_source_create;
    aud.feed_source = aud_source_feed;
    aud.play_source = aud_source_play;
    aud.pause_source = aud_source_pause;
    aud.reset_source = aud_source_reset;
    aud.destroy_source = aud_source_destroy;
    aud.get_source_info = aud_source_get_info;
    aud.set_source_volume = aud_source_set_volume_scale;

    aud.play = aud_device_play;
    aud.output_to_device = aud_source_output_to_device;

    return aud;
}

void* aud_load ( void* std_runtime ) {
    std_attach ( std_runtime );

    aud_device_init();
    aud_source_init();

    s_api = aud_api();

    return &s_api;
}

void aud_unload ( void ) {
    std_noop_m;
}
