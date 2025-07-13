#include <aud.h>

#include <std_queue.h>
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
    void* data;
    size_t size;
    //std_alloc_t data_alloc;
#if defined(std_platform_win32_m)
    WAVEHDR win32_header;
#endif
} aud_device_submit_context_t;

typedef struct {
    aud_device_submit_context_t submit_contexts[aud_device_max_submit_contexts_m];
} aud_device_context_t;

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

    aud_device_context_t* context;
    std_ring_t submit_ring;

    /*std_memory_h submit_blocks_handle;
    std_ring_t submit_blocks_ring;
    char** submit_blocks;
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

    aud_device_context_t* device_contexts_array;
} aud_device_state_t;

void        aud_device_load ( aud_device_state_t* state );
void        aud_device_reload ( aud_device_state_t* state );
void        aud_device_unload ( void );

size_t      aud_device_get_count ( void );
size_t      aud_device_get_list ( aud_device_h* devices, size_t cap );
bool        aud_device_get_info ( aud_device_info_t* info, aud_device_h device );

bool        aud_device_activate ( aud_device_h device, const aud_device_params_t* params );
bool        aud_device_deactivate ( aud_device_h device );

// TODO delete this?
void        aud_device_play ( aud_device_h device, void* data, size_t size );

char*       aud_device_get_buffer ( aud_device_h device );
void        aud_device_push_buffer ( aud_device_h device, uint64_t buffer_size );

const std_ring_t*   aud_device_get_ring ( aud_device_h device );
void                aud_device_update_ring ( aud_device_h device );
