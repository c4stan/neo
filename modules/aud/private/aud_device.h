#include <aud.h>

#include <std_queue.h>
#include <std_platform.h>

size_t      aud_device_get_count ( void );
size_t      aud_device_get_list ( aud_device_h* devices, size_t cap );
bool        aud_device_get_info ( aud_device_info_t* info, aud_device_h device );

bool        aud_device_activate ( aud_device_h device, const aud_device_params_t* params );
bool        aud_device_deactivate ( aud_device_h device );

void        aud_device_play ( aud_device_h device, std_buffer_t buffer );

void        aud_device_init ( void );
void        aud_device_shutdown ( void );

byte_t*     aud_device_get_buffer ( aud_device_h device );
void        aud_device_push_buffer ( aud_device_h device, uint64_t buffer_size );

const std_ring_t*   aud_device_get_ring ( aud_device_h device );
void                aud_device_update_ring ( aud_device_h device );
