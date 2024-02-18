#pragma once

#include <std_platform.h>
#include <std_compiler.h>

typedef uint64_t std_thread_h;
#define std_thread_null_handle_m UINT64_MAX

typedef void ( std_thread_routine_f ) ( void* );


/*
    Things a thread could care about:
    - wake proprity
    - core pairing
    - core placement
    - core sharing
*/

/*
   A thread can declare how much 'active' they are.
   An example of a low core usage thread could be a 'background' thread that sleeps for long periods and wakes up to perform some action only when requested to.
   An example of a high core usage thread could be a 'worker' thread that normally has always work available to do and the more it runs, the better.
   High core usage is used as default.
*/
/*
typedef enum {
    std_thread_core_usage_low_m,
    std_thread_core_usage_high_m,
    std_thread_core_usage_default_m = std_thread_core_usage_high_m
} std_thread_core_usage_e;

typedef enum {
    std_thread_core_pairing_any_m,                        // The thread is ok with being paired (e.g. hyperthreaded) with any other thread
    std_thread_core_pairing_only_low_core_usage_m,        // The thread only allows to be paired with threads that declare low core usage
    std_thread_core_pairing_prefer_high_core_usage_m,     // The thread prefers to be paired with threads that declare high core usage
    std_thread_core_pairing_default_m = std_thread_core_pairing_any_m
} std_thread_core_pairing_e;

typedef enum {
    std_thread_core_sharing_any_m,                        // The thread is ok with time-sharing their cores with any other thread
    std_thread_core_sharing_none_m,                       // The thread doesn't allow any other thread to run on their cores
    std_thread_core_sharing_only_low_core_usage_m,        // The thread only allows threads that declare low core usage to run on their cores
    std_thread_core_sharing_prefer_high_core_usage_m,     // The thread prefers threads that declare high core usage to run on their cores
    std_thread_core_sharing_default_m = std_thread_core_sharing_any_m
} std_thread_core_sharing_e;

typedef enum {
    std_thread_wake_priority_background_m,
    std_thread_wake_priority_low_m,
    std_thread_wake_priority_medium_m,
    std_thread_wake_priority_high_m,
    std_thread_wake_priority_realtime_m,
    std_thread_wake_priority_default_m = std_thread_wake_priority_medium_m
} std_thread_wake_priority_e;

typedef struct {
    std_thread_core_usage_e     core_usage;
    std_thread_core_pairing_e   pairing;
    std_thread_core_sharing_e   sharing;
    std_thread_wake_priority_e  priority;
    uint64_t                    core_mask;
} std_thread_scheduling_requirements_t;

#define std_thread_core_mask_any_m UINT64_MAX

#define std_thread_scheduling_requirements_default_m          \
    (std_thread_scheduling_requirements_t) {                \
        .core_usage = std_thread_core_usage_default_m,        \
        .pairing    = std_thread_core_pairing_default_m,      \
        .sharing    = std_thread_core_sharing_default_m,      \
        .priority   = std_thread_wake_priority_default_m,     \
        .cores_mask = std_thread_core_mask_any_m              \
    }

std_thread_h    std_thread              ( std_thread_routine_f* routine, void* arg, const char* name, const std_thread_scheduling_requirements_t* scheduling );
*/

// TODO make affinity masks support std_thread_max_threads_m threads! currently defined as 128, but the mask only fits for 64 threads
#define std_thread_core_mask_any_m UINT64_MAX

typedef struct {
    size_t stack_size;
    uint64_t core_mask;
    char name[std_thread_name_max_len_m];
} std_thread_info_t;

//==============================================================================

std_thread_h    std_thread              ( std_thread_routine_f* routine, void* arg, const char* name, uint64_t core_mask );
bool            std_thread_join         ( std_thread_h thread );
#if 0
    std_thread_h    std_thread_register     ( std_thread_routine_f* routine, void* arg, const char* name, uint64_t core_mask, uint64_t os_handle ); // TODO remove this?
#endif

// TODO take a thread group index too (see SetThreadGroupAffinity)
void            std_thread_set_core_mask ( std_thread_h thread, uint64_t core_mask );

bool            std_thread_alive        ( std_thread_h thread );
uint32_t        std_thread_uid          ( std_thread_h thread );
uint32_t        std_thread_index        ( std_thread_h thread );
const char*     std_thread_name         ( std_thread_h thread );

std_thread_h    std_thread_this         ( void );
void            std_thread_this_yield   ( void );
std_no_return_m std_thread_this_exit    ( void );
void            std_thread_this_sleep   ( size_t milliseconds );

bool            std_thread_info         ( std_thread_info_t* info, std_thread_h thread );
