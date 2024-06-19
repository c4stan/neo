#pragma once

#include <std_platform.h>
#include <std_log.h>
#include <std_list.h>
#include <std_hash.h>
#include <std_mutex.h>
#include <std_thread.h>
#include <std_process.h>
#include <std_allocator.h>
#include <std_time.h>

//==============================================================================

#define std_module_map_slots_m std_module_max_modules_m * 4

typedef struct {
    uint64_t hash;
    char string[std_module_name_max_len_m];
} std_module_name_t;

typedef struct {
    void* api;
    uint64_t handle;
    std_module_name_t name;
    // Info read from the PE header when the module gets first load.
    // Data is initialized and contributes directly to disk size.
    // Bss is uninitialized and its contribute to disk is almost zero.
    // Both have direct contribute on runtime memory consumption.
    // TODO
    size_t data_size;
    size_t bss_size;
} std_module_t;

typedef struct {
    std_module_t        modules_array[std_module_max_modules_m];
    std_module_t*       modules_freelist;

    // std_module_name_t* -> std_module_t*
    #if 0
    std_module_name_t   modules_name_map_keys[std_module_map_slots_m];
    std_module_t**      modules_name_map_payloads[std_module_map_slots_m];
    std_map_t           modules_name_map;
    #else
    //uint64_t* modules_name_map_keys;
    //uint64_t* modules_name_map_values;
    uint64_t modules_name_map_keys[std_module_map_slots_m]; // name hash
    uint64_t modules_name_map_payloads[std_module_map_slots_m]; // std_module_t*
    std_hash_map_t modules_name_map;
    #endif

    // void* api -> std_module_t*
    #if 0
    void*               modules_api_map_keys[std_module_map_slots_m];
    std_module_t**      module_api_map_payloads[std_module_map_slots_m];
    std_map_t           modules_api_map;
    #else
    uint64_t modules_api_map_keys[std_module_map_slots_m]; // void*
    uint64_t module_api_map_payloads[std_module_map_slots_m]; // std_module_t*
    std_hash_map_t modules_api_map;
    #endif

    //std_mutex_t         modules_mutex;
    std_rwmutex_t       modules_mutex;
} std_module_state_t;

//==============================================================================

typedef struct {
    std_virtual_stack_t nodes_stack;
    //std_virtual_buffer_t nodes_buffer;
    //std_alloc_t nodes_alloc;
    //size_t      nodes_mapped_size;
    size_t      nodes_count;
    size_t      total_size;
    std_mutex_t mutex;
    // TODO add binning? Have a freelist for each bin, store an additional
    // pointer for that in the heap free segments. Can bin from virtual page
    // size (2KiB) down to 32(?) bytes, and step by power of 2. Bin back full
    // pages individually and decompose the rest into the smaller bins.
    // Also speeding up coalescing with something like a 16bytes header for every
    // used/unused segment instead of having to do a lin search would be good.
} std_allocator_virtual_heap_t;

// TODO remove?
typedef struct {
    //std_alloc_t bins_alloc;
    //std_virtual_buffer_t bins_buffer;
    std_virtual_stack_t bins_stack;
    size_t bin_size;
    size_t bin_capacity;
    void* bins_freelist;
    std_mutex_t mutex;
    std_hash_map_t tag_bin_map; // uint64_t tag -> <uint64_t count, std_memory_h[] allocations>* bin
    uint64_t tag_bin_map_keys[std_tagged_allocator_max_bins_m * 2];
    uint64_t tag_bin_map_payloads[std_tagged_allocator_max_bins_m * 2];
} std_allocator_tagged_heap_t;

#define std_allocator_tlsf_min_x_level_m 7      // min allocation size of 2^7 - header_size = 128 - 8 = 120 bytes
#define std_allocator_tlsf_max_x_level_m 36     // max allocation size of 2^36 - 1 - header_size = ~68 GB
#define std_allocator_tlsf_x_size_m (std_allocator_tlsf_max_x_level_m - std_allocator_tlsf_min_x_level_m)
#define std_allocator_tlsf_y_size_m 16          // 16 subdivision buckets per X level
#define std_allocator_tlsf_log2_y_size_m 4

#define std_allocator_tlsf_header_size_m 8 // freelist pointers excluded
#define std_allocator_tlsf_footer_size_m 8
#define std_allocator_tlsf_min_segment_size_m ( 1 << std_allocator_tlsf_min_x_level_m )
#define std_allocator_tlsf_max_segment_size_m ( ( 1ull << std_allocator_tlsf_max_x_level_m ) - 1 )
#define std_allocator_tlsf_free_segment_bit_m (1 << 0)
#define std_allocator_tlsf_free_prev_segment_bit_m (1 << 1)
#define std_allocator_tlsf_alignment_bit_m (1 << 2)
#define std_allocator_tlsf_size_mask_m ( ( ~0ull ) << 3 )

typedef struct {
    std_mutex_t mutex;
    std_virtual_stack_t stack;
    void* freelists[std_allocator_tlsf_x_size_m][std_allocator_tlsf_y_size_m];
    uint16_t available_freelists[std_allocator_tlsf_x_size_m];
    uint64_t available_rows;
    size_t allocated_size;
} std_allocator_tlsf_heap_t;

typedef struct {
    size_t                          virtual_page_size;
    //uint32_t                        virtual_page_size_bit_idx; // idx of the top bit in the page size value
    // TODO remove
    //std_allocator_virtual_heap_t    virtual_heap;
    // TODO remove? rework?
    std_allocator_tagged_heap_t     tagged_heap;
    size_t                          tagged_page_size;
    std_allocator_tlsf_heap_t       tlsf_heap;
} std_allocator_state_t;

//==============================================================================

typedef struct {
    uint64_t                            logical_cores_mask;
    std_platform_cache_info_t           caches_info[64];
    size_t                              caches_count;
    std_platform_physical_core_info_t   physical_cores_info[64];
    size_t                              physical_cores_count;
    std_platform_logical_core_info_t    logical_cores_info[64];
    size_t                              logical_cores_count;
    std_platform_memory_info_t          memory_info;
} std_platform_state_t;

//==============================================================================

typedef struct {
    float  tick_freq_milli_f32;
    double tick_freq_milli_f64;
    float  tick_freq_micro_f32;
    double tick_freq_micro_f64;
    std_tick_t program_start_tick;
    std_timestamp_t program_start_timestamp_utc;
    std_timestamp_t program_start_timestamp_local;
} std_time_state_t;

//==============================================================================

//typedef struct {
//    std_virtual_buffer_t static_string_table_buffer;
//} std_string_state_t;

//==============================================================================

// TODO remove?
typedef void ( std_log_callback_f ) ( const std_log_msg_t* msg );

typedef struct {
    std_log_callback_f* log_callback;
    bool is_debugger_attached;
} std_log_state_t;

//==============================================================================

typedef struct {
    uint64_t                os_handle;
    uint64_t                os_id;
    std_thread_routine_f*   routine;
    void*                   arg;
    size_t                  idx;    // TODO store std_thread_h instead?
    uint32_t                uid;
    uint64_t                core_mask;
    char                    name[std_thread_name_max_len_m];
    size_t                  stack_size;
#if defined ( std_platform_linux_m )
    // because pthread doesn't provide a way to check whether a thread is alive or not, we just flag it as dead after a successful join...
    bool                    is_alive;
#endif
} std_thread_t;

typedef struct {
    std_thread_t            threads_array[std_thread_max_threads_m];
    std_thread_t*           threads_freelist;
    //std_list_t              threads_freelist;
    size_t                  threads_pop;
    uint32_t                uid;
    std_mutex_t             mutex;
    uint64_t                tls_alloc;
} std_thread_state_t;

//==============================================================================

typedef struct {
    uint64_t                os_handle;
    uint64_t                os_id;
    //uint64_t                os_main_thread_handle; // provided by win32, not sure how to get it on linux.
    uint64_t                stdin_handle;
    uint64_t                stdout_handle;
    uint64_t                stderr_handle;
    char                    executable_path[std_process_path_max_len_m];
    char                    name[std_process_name_max_len_m];
    char                    args_buffer[std_process_args_max_len_m + std_process_max_args_m];
    char*                   args[std_process_max_args_m];
    size_t                  args_count;
} std_process_t;

typedef struct {
    uint64_t os_handle;
    bool is_owner;
    char name[std_process_pipe_name_max_len_m];
    std_process_pipe_params_t params;
} std_process_pipe_t;

typedef struct {
    uint64_t hash;
    char name[std_process_pipe_name_max_len_m];
} std_process_pipe_name_t;

typedef struct {
    std_process_t           processes_array[std_process_max_processes_m];
    std_process_t*          processes_freelist;
    size_t                  processes_pop;

    std_process_pipe_t      pipes_array[std_process_max_pipes_m];
    std_process_pipe_t*     pipes_freelist;
    size_t                  pipes_count;

    // std_process_pipe_name_t -> std_pipe_t*
#if 0
    std_process_pipe_name_t pipes_map_keys[std_process_max_pipes_m * 2];
    std_pipe_t*             pipes_map_values[std_process_max_pipes_m * 2];
    std_map_t               pipes_map;
#endif

    std_mutex_t             mutex;
    char                    working_path[std_process_path_max_len_m];
} std_process_state_t;

//void std_process_set_args ( const char** args, size_t count );

//==============================================================================

typedef struct {
    void* entry_point;
    // TODO unify platforms using uint64_t?
#ifdef std_platform_win32_m
    HMODULE handle;
#elif defined(std_platform_linux_m)
    void* handle;
#endif
    // Info read from the PE header when the module gets first load.
    // Data is initialized and contributes directly to disk size.
    // Bss is uninitialized and its contribute to disk is almost zero.
    // Both have direct contribute on runtime memory consumption.
    size_t data_size;
    size_t bss_size;
} std_app_module_t;

typedef struct {
    // TODO
    //std_app_module_t app_module;
    //std_app_state_e client_state;
} std_app_state_t;

//==============================================================================

typedef struct {
    std_module_state_t      module_state;
    std_allocator_state_t   allocator_state;
    std_platform_state_t    platform_state;
    std_log_state_t         log_state;
    std_time_state_t        time_state;
    std_thread_state_t      thread_state;
    std_process_state_t     process_state;
    //std_app_state_t         app_state;
} std_runtime_state_t;

//==============================================================================

void std_log_boot ( void );
void std_allocator_boot ( void );

void std_module_init        ( std_module_state_t* );
void std_allocator_init     ( std_allocator_state_t* );
void std_platform_init      ( std_platform_state_t* );
//void std_string_init        ( std_string_state_t* );
void std_log_init           ( std_log_state_t* );
void std_time_init          ( std_time_state_t* );
void std_thread_init        ( std_thread_state_t* );
void std_process_init       ( std_process_state_t*, char** args, size_t args_count );
void std_app_init           ( std_app_state_t* );

//==============================================================================

void std_module_attach      ( std_module_state_t* );
void std_allocator_attach   ( std_allocator_state_t* );
void std_platform_attach    ( std_platform_state_t* );
//void std_string_attach      ( std_string_state_t* );
void std_log_attach         ( std_log_state_t* );
void std_time_attach        ( std_time_state_t* );
void std_thread_attach      ( std_thread_state_t* );
void std_process_attach     ( std_process_state_t* );
void std_app_attach         ( std_app_state_t* );

//==============================================================================

void std_process_shutdown ( void );
void std_thread_shutdown ( void );
void std_module_shutdown ( void );
void std_app_shutdown ( void );

//==============================================================================

void* std_runtime_state ( void );

//==============================================================================
