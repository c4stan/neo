/*
-------------------------------------------------------------------------------

    The typical boilerplate required to define a new module is:

  // ----

  xx.h
    #define xx_module_name_m xx
    std_module_export_m void* xx_load ( void* );
    std_module_export_m void xx_unload ( void );
    std_module_export_m void xx_reload ( void*, void* );

    typedef struct {
        // function pointers
        ...
    } xx_i;

  xx.c
    static void xx_api_init ( xx_i* xx ) {
        // fill xx
        ...
    }

    void* xx_load ( void* std_runtime ) {
        std_runtime_bind ( std_runtime );

        xx_state_t* state = xg_state_alloc();

        xx_submodule_load ( &state->submodule_state );
        ...

        xx_api_init ( &state->api );
        return &state->api;
    }

    void xx_unload ( void ) {
        xx_submodule_unload ();
        ...
    }

    void xx_reload ( void* std_runtime, void* api ) {
        std_runtime_bind ( std_runtime );

        // Assumes that the api is stored at the beginning of the module state
        std_auto_m state = ( xx_state_t* ) api;

        xx_submodule_reload ( &state->submodule_state );
        ...

        xx_api_init ( &state->api );
    }

  private/xx_state.h
    typedef struct {
        xx_i api;
        xx_submodule_state_t submodule_state;
        ...
    } xx_state_t;

    xx_state_t* xx_state_alloc ( void );
    void xx_state_free ( void );

  // ----

    TODO add a neo cmd to automate some of the boilerplate?

    The module name is used by the system to find the actual .dll in the
    file system.
    On the first call to std_module_get_m ( xx_module_name ) the system will
    try to find xx.dll, load it, and get a pointer to function xx_load. If
    successful xx_load is called by passing std_runtime_state_t as parameter
    and expecting a pointer to a static xx_i struct to be returned.
    That pointer gets cached and will be returned on every other
    std_module_get call until all references are released.

-------------------------------------------------------------------------------
*/

#pragma once

#include <std_compiler.h>
#include <std_log.h>
#include <std_allocator.h>

// Returns a pointer to the specified module api. Modules are ref counted.
// TODO give a way to preload all modules at boot time and never unload
void* std_module_get ( const char* module_name );
#define std_module_get_m( name ) ( std_pp_eval_concat_m( name, _i* ) ) std_module_get ( std_pp_eval_string_m( name ) )

// Decreases the refcount of the module that owns the specified api. When refcount reaches 0 the module DLL gets unloaded.
//void std_module_release ( void* api );

void* std_module_load ( const char* name );
void std_module_unload ( const char* name );
//void std_module_unload ( const char* name );
#define std_module_load_m( name ) ( std_pp_eval_concat_m ( name, _i* ) ) std_module_load ( std_pp_eval_string_m ( name ) )
#define std_module_unload_m( name ) std_module_unload ( std_pp_eval_string_m( name ) )

// Unloads a module DLL, recompiles the module code by running neo on it, and loads the new DLL.
// The new api is loaded in place of the old one (same memory), so all pointers to the module api remain valid.
// The caller must ensure that calls to the module api functions don't happen until the reload is done
// https://handmade.network/forums/t/6984-hotloaded_application_dll_drawbacks
void std_module_reload ( const char* build_target );
#define std_module_reload_m() std_module_reload ( std_pp_eval_string_m ( std_module_name_m ) )

size_t std_module_build ( const char* build_target, void* output, size_t output_size );

// Implemented in std_state.c
void std_runtime_bind ( void* std_runtime );
size_t std_runtime_size ( void );

// utility
#define std_module_declare_state_m( NAME ) \
NAME##_state_t* NAME##_state_alloc ( void ); \
void NAME##_state_free ( void ); \
void NAME##_state_bind ( NAME##_state_t* state );

#define std_module_implement_state_m( NAME ) \
    static NAME##_state_t* NAME##_state; \
\
NAME##_state_t* NAME##_state_alloc ( void ) { \
    std_assert_m ( ! NAME##_state ); \
    NAME##_state = std_virtual_heap_alloc_m ( NAME##_state_t ); \
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
