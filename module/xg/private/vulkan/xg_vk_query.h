#include <xg.h>

#include "vulkan/xg_vk.h"

#if 0
/*

*/

typedef uint64_t xg_query_pool_h;
typedef uint64_t xg_query_h;

typedef enum {
    xg_query_pool_type_timestamp_m,
} xg_query_pool_type_e;

typedef struct {
    xg_query_pool_type_e type;
    uint32_t capacity;
    const char* debug_name;
} xg_query_pool_params_t;

typedef struct {
    uint32_t idx;
} xg_vk_query_t;

typedef struct {
    xg_device_h device;
    VkQueryPool vk_pool;
    uint32_t count;

    std_memory_h queries_handle;
    xg_vk_query_t* queries;
    uint32_t queries_count;

    std_memory_h readback_handle;
    uint64_t* readback;

    xg_query_pool_params_t params;
} xg_vk_query_pool_t;

xg_query_pool_h xg_vk_query_pool_create ( const xg_query_pool_params_t* params );
void xg_vk_query_pool_destroy ( xg_query_pool_h pool );

xg_query_h xg_vk_query_reserve ( xg_query_pool_h pool );
uint32_t xg_vk_query_alloc ( xg_query_pool_h pool, xg_query_h query );
void xg_vk_query_readback ( xg_query_pool_h pool, xg_query_h* queries, uint32_t queries_count, std_buffer_t reacback_buffer );

typedef struct {
    xg_vk_query_pool_t* pools_array;
    xg_vk_query_pool_t* pools_freelist;
    std_mutex_t pools_mutex;
} xg_vk_query_state_t;

void xg_vk_query_load ( xg_vk_query_pool_state_t* state );
void xg_vk_query_reload ( xg_vk_query_pool_state_t* state );
void xg_vk_query_unload ( void );
#endif
