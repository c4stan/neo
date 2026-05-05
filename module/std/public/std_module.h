#pragma once

#include <std_compiler.h>
#include <std_log.h>
#include <std_allocator.h>

// TODO give a way to preload all modules at boot time and never unload
// TODO automatically unload all loaded modules in the right order inside std_shutdown
void* std_module_get ( const char* module_name );
void* std_module_load ( const char* module_name );
void std_module_unload ( const char* module_name );

// Unloads a module DLL, recompiles the module code by running neo on it, and loads the new DLL.
// The new api is loaded in place of the old one (same memory), so all pointers to the module api remain valid.
// The caller must ensure that calls to the module api functions don't happen until the reload is done
// https://handmade.network/forums/t/6984-hotloaded_application_dll_drawbacks
void std_module_reload ( const char* build_target );

#define std_module_get_m( name ) ( std_pp_eval_concat_m( name, _i* ) ) std_module_get ( std_pp_eval_string_m( name ) )
#define std_module_load_m( name ) ( std_pp_eval_concat_m ( name, _i* ) ) std_module_load ( std_pp_eval_string_m ( name ) )
#define std_module_unload_m( name ) std_module_unload ( std_pp_eval_string_m( name ) )
#define std_module_reload_m() std_module_reload ( std_pp_eval_string_m ( std_module_name_m ) )

void* std_module_reboot ( const char* solution_name );

// Implemented in std_state.c
void std_runtime_bind ( void* std_runtime );
size_t std_runtime_size ( void );

// utility
#define std_module_declare_state_m( NAME ) \
NAME##_state_t* NAME##_state_alloc ( void ); \
void NAME##_state_free ( void ); \
void NAME##_state_bind ( NAME##_state_t* state );

#define std_module_implement_state_m( NAME ) \
    static NAME##_state_t* NAME##_state = NULL; \
\
NAME##_state_t* NAME##_state_alloc ( void ) { \
    std_assert_m ( ! NAME##_state ); \
    NAME##_state = std_virtual_heap_alloc_struct_m ( NAME##_state_t ); \
    return NAME##_state; \
} \
\
void NAME##_state_free ( void ) { \
    std_assert_m ( NAME##_state ); \
    std_virtual_heap_free ( NAME##_state ); \
    NAME##_state = NULL; \
} \
\
void NAME##_state_bind ( NAME##_state_t* state ) { \
    NAME##_state = state; \
}

const char* std_module_name_from_id ( uint32_t id );
