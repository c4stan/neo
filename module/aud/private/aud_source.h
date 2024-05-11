#include <aud.h>

#include <std_allocator.h>

typedef struct {
    aud_source_params_t params;
    std_stack_t stack;
    //std_virtual_buffer_t buffer;
    uint64_t active_idx;
    double time_played; // seconds
    float volume;
} aud_source_t;

void aud_source_init ( void );

aud_source_h aud_source_create ( const aud_source_params_t* params );
void aud_source_feed ( aud_source_h source, const void* data, uint64_t size );
void aud_source_play ( aud_source_h source );
void aud_source_pause ( aud_source_h source );
void aud_source_reset ( aud_source_h source );
void aud_source_destroy ( aud_source_h source );
void aud_source_set_volume_scale ( aud_source_h source, float scale );

bool aud_source_get_info ( aud_source_info_t* info, aud_source_h source );

void aud_source_output_to_device ( aud_device_h device, uint64_t ms );


