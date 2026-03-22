#pragma once

#include <std_platform.h>
#include <std_compiler.h>
#include <std_handle.h>

std_define_handle_m ( std_thread_h );

typedef void ( std_thread_routine_f ) ( void* );

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

// TODO take a thread group index too (see SetThreadGroupAffinity)
void            std_thread_set_core_mask ( std_thread_h thread, uint64_t core_mask );

bool            std_thread_alive        ( std_thread_h thread );
uint32_t        std_thread_uid          ( std_thread_h thread );
uint32_t        std_thread_index        ( std_thread_h thread );
const char*     std_thread_name         ( std_thread_h thread );

std_thread_h    std_thread_this         ( void );
void            std_thread_this_yield   ( void );
void            std_thread_this_sleep   ( size_t milliseconds );
std_no_return_m std_thread_this_exit    ( void );

bool            std_thread_info         ( std_thread_info_t* info, std_thread_h thread );
