#include <xg.h>

#include "vulkan/xg_vk.h"

typedef struct {
    uint32_t idx;
} xg_vk_query_t;

typedef struct {
    VkQueryPool vk_handle;
    xg_query_pool_params_t params;
} xg_vk_query_pool_t;

xg_query_pool_h xg_vk_query_pool_create ( const xg_query_pool_params_t* params );
void xg_vk_query_pool_destroy ( xg_query_pool_h pool );
void xg_vk_query_pool_read ( std_buffer_t dest, xg_query_pool_h pool );

xg_vk_query_pool_t* xg_vk_query_pool_get ( xg_query_pool_h pool );

typedef struct {
    xg_vk_query_pool_t* pools_array;
    xg_vk_query_pool_t* pools_freelist;
    uint64_t* pools_bitset;
    std_mutex_t pools_mutex;
} xg_vk_query_state_t;

void xg_vk_query_load ( xg_vk_query_state_t* state );
void xg_vk_query_reload ( xg_vk_query_state_t* state );
void xg_vk_query_unload ( void );
