#include <std_module.h>
#include <std_platform.h>
#include <std_hash.h>
#include <std_list.h>
#include <std_mutex.h>
#include <std_string.h>
#include <std_array.h>
#include <std_file.h>

#include "std_state.h"

// ----------------------------------------------------------------------------

typedef void* ( *std_module_loader_f ) ( void* );
typedef void ( *std_module_reloader_f ) ( void*, void* );
typedef void* ( *std_module_unloader_f ) ( void );

static std_module_state_t* std_module_state;

// ----------------------------------------------------------------------------

// The hash is locally stored in the key, so that it doesn't need to be recomputed a bunch of times
//static uint64_t std_module_hash_name ( const void* _name, void* _ ) {
//    std_unused_m ( _ );
//    const std_module_name_t* name = ( const std_module_name_t* ) _name;
//    return name->hash;
//}

//static bool std_module_cmp_name ( uint64_t hash1, const void* _name1, const void* _name2, void* _ ) {
//    std_unused_m ( _ );
//    std_unused_m ( hash1 );
//    const std_module_name_t* name1 = ( const std_module_name_t* ) _name1;
//    const std_module_name_t* name2 = ( const std_module_name_t* ) _name2;
//
//    if ( name1->hash != name2->hash ) {
//        return false;
//    }
//
//    return std_str_cmp ( name1->string, name2->string ) == 0;
//}

//static uint64_t std_module_hash_api ( const void* api, void* _ ) {
//    std_unused_m ( _ );
//    uint64_t key = * ( const size_t* ) api;
//    uint64_t hash = std_hash_murmur_64 ( key );
//    return hash;
//}

//static bool std_module_cmp_api ( uint64_t hash1, const void* _api1, const void* _api2, void* _ ) {
//    std_unused_m ( _ );
//    std_unused_m ( hash1 );
//    void** api1 = ( void** ) _api1;
//    void** api2 = ( void** ) _api2;
//    return *api1 == *api2;
//}

// ----------------------------------------------------------------------------

// TODO what to do with this?
#ifdef std_module_enable_fake_loading_m
void std_module_fakeload_api ( const char* name, void* api ) {
    // Lock, pop freelist, write to buffer
    //std_rwmutex_lock ( &std_module_state->modules_mutex );
    std_module_t* module = std_list_pop_m ( &std_module_state->modules_freelist );
    module->api = api;
    module->name.hash = std_hash_string_64_m ( name );
    std_str_copy ( module->name.string, std_module_name_max_len_m, name );
#ifdef std_platform_win32_m
    module->handle = 0;
#endif
    std_hash_map_insert ( &std_module_state->modules_api_map, ( uint64_t ) api, ( uint64_t ) module );
    //std_rwmutex_unlock ( &std_module_state->modules_mutex );
}
#endif

// ----------------------------------------------------------------------------

static std_module_t* std_module_lookup ( const char* name ) {
    uint64_t name_hash = std_hash_string_64_m ( name );

    uint64_t* module_lookup = std_hash_map_lookup ( &std_module_state->modules_name_map, name_hash );

    if ( module_lookup ) {
        std_module_t* module = * ( std_module_t** ) module_lookup;
        return module;
    }

    return NULL;
}

static bool std_module_file_name ( const char* name, const char* file_prefix, const char* file_postfix, char* module_file_name ) {
    size_t i;
    i = std_str_copy ( module_file_name, std_module_name_max_len_m, file_prefix );

    if ( i > std_module_name_max_len_m ) {
        return false;
    }

    i += std_str_copy ( module_file_name + i, std_module_name_max_len_m - i, name );

    if ( i > std_module_name_max_len_m ) {
        return false;
    }

    i += std_str_copy ( module_file_name + i, std_module_name_max_len_m - i, file_postfix );

    if ( i > std_module_name_max_len_m ) {
        return false;
    }

    return true;
}

static bool std_module_entrypoint_name ( const char* name, const char* entrypoint_postfix, char* module_entrypoint_name ) {
    size_t i;
    i = std_str_copy ( module_entrypoint_name, std_module_name_max_len_m, name );

    if ( i > std_module_name_max_len_m ) {
        return false;
    }

    i += std_str_copy ( module_entrypoint_name + i, std_module_name_max_len_m - i, entrypoint_postfix );

    if ( i > std_module_name_max_len_m ) {
        return false;
    }

    return true;
}

static std_module_t* std_module_load_internal ( const char* name ) {
    //std_log_info_m ( "Requested module " std_fmt_str_m " is not currently loaded. Looking up the modules library...", name );
    // Build file and entrypoint name
    char module_file_name[std_module_name_max_len_m];
    char module_entrypoint_name[std_module_name_max_len_m];

    const char* file_prefix = std_submodules_path_m;
    const char* file_postfix = ".dll";
    const char* entrypoint_postfix = "_load";

    if ( !std_module_file_name ( name, file_prefix, file_postfix, module_file_name ) ) {
        std_log_error_m ( "Module name max length (" std_fmt_int_m ") was exceeded for module " std_fmt_str_m ".", std_module_name_max_len_m, name );
        return NULL;
    }

    if ( !std_module_entrypoint_name ( name, entrypoint_postfix, module_entrypoint_name ) ) {
        std_log_error_m ( "Module entrypoint name max length (" std_fmt_int_m ") was exceeded for module " std_fmt_str_m ".", std_module_name_max_len_m, name );
        return NULL;
    }

    //
    /*
    const char current_dir[128];
    GetCurrentDirectory ( 128, current_dir );
    printf ( "CD: " std_log_str_m std_log_newline_m, current_dir );
    printf ( "MP: " std_log_str_m std_log_newline_m, module_file_name );
    HANDLE file = CreateFile ( module_file_name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    HANDLE map_handle = CreateFileMapping ( ( HANDLE ) file, NULL, PAGE_READONLY, 0, 0, NULL );
    void* map = MapViewOfFileEx ( map_handle, FILE_MAP_READ, 0, 0, 0, NULL );
    PIMAGE_NT_HEADERS headers = ImageNtHeader ( map );
    DWORD error = GetLastError();
    printf ( std_log_str_m" "std_log_u32_m std_log_newline_m, module_file_name, error );
    uint32_t bss_size = headers->OptionalHeader.SizeOfInitializedData;
    printf ( std_log_str_m ": " std_log_u32_m std_log_newline_m, module_name.name, bss_size );
    CloseHandle ( map_handle );
    UnmapViewOfFile ( map );
    */
    //

    std_file_info_t file_info;
    std_file_path_info ( &file_info, module_file_name );
    std_unused_m ( file_info );

    // Load
#if defined(std_platform_win32_m)
    HMODULE handle = LoadLibrary ( module_file_name );

    if ( handle == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to load - LoadLibrary failed on path " std_fmt_str_m ".", name, module_file_name );
        return NULL;
    }

    std_module_loader_f loader = ( std_module_loader_f ) GetProcAddress ( handle, module_entrypoint_name );

    if ( loader == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to load - GetProcAddress failed to load function " std_fmt_str_m ".", name, module_entrypoint_name );
        return NULL;
    }

    /*PIMAGE_NT_HEADERS headers = ImageNtHeader ( handle );

    if ( headers == NULL ) {
        return NULL;
    }*/

    //uint32_t bss_size = headers->OptionalHeader.SizeOfInitializedData;
    //printf ( std_log_str_m ": " std_log_u32_m std_log_newline_m, module_name.name, bss_size );
#elif defined(std_platform_linux_m)
    void* handle = dlopen ( module_file_name, RTLD_LAZY );

    if ( handle == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to load - dlopen failed on path " std_fmt_str_m ".", name, module_file_name );
        return NULL;
    }

    std_module_loader_f loader = ( std_module_loader_f ) dlsym ( handle, module_entrypoint_name );

    if ( loader == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to load - dlsym failed to load function " std_fmt_str_m ".", name, module_entrypoint_name );
        return NULL;
    }

#endif

    std_log_info_m ( "Loading module " std_fmt_str_m " at entrypoint " std_fmt_str_m, name, module_entrypoint_name );
    void* api = loader ( std_runtime_state() );

    if ( api == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to load - load function " std_fmt_str_m " returned NULL.", name, module_entrypoint_name );
        return NULL;
    } else {
        std_log_info_m ( "Load of module " std_fmt_str_m " complete.", name );
    }

    uint64_t hash = std_hash_string_64_m ( name );

    std_module_t* module = std_list_pop_m ( &std_module_state->modules_freelist );
    module->api = api;
    module->name.hash = hash;
    std_str_copy ( module->name.string, std_module_name_max_len_m, name );
    module->handle = ( uint64_t ) handle;
    std_hash_map_insert ( &std_module_state->modules_api_map, ( uint64_t ) module->api, ( uint64_t ) module );
    std_hash_map_insert ( &std_module_state->modules_name_map, hash, ( uint64_t ) module );
    
    return module;
}

void* std_module_load ( const char* name ) {
    //std_rwmutex_lock_write ( &std_module_state->modules_mutex );

    std_module_t* module = std_module_lookup ( name );
    if ( module ) {
        std_log_error_m ( "Trying to load already loaded module " std_fmt_str_m, name );
        //std_rwmutex_unlock_write ( &std_module_state->modules_mutex );
        return module->api;
    }

    module = std_module_load_internal ( name );

    //std_rwmutex_unlock_write ( &std_module_state->modules_mutex );
    return module->api;
}

void* std_module_get ( const char* name ) {
    //std_rwmutex_lock_read ( &std_module_state->modules_mutex );
    std_module_t* module = std_module_lookup ( name );
    //std_rwmutex_unlock_read ( &std_module_state->modules_mutex );

    if ( !module ) {
        std_log_error_m ( "Trying to get non loaded module " std_fmt_str_m, name );
        return NULL;
    }

    return module->api;
}

static void std_module_unload_internal ( std_module_t* module ) {
    std_log_info_m ( "Unloading module " std_fmt_str_m "...", module->name.string );

    std_assert_m ( module->handle != 0 );
    char module_entrypoint_name[std_module_name_max_len_m];
    const char* entrypoint_postfix = "_unload";

    if ( !std_module_entrypoint_name ( module->name.string, entrypoint_postfix, module_entrypoint_name ) ) {
        std_log_error_m ( "Module entrypoint name max length (" std_fmt_int_m ") was exceeded for module " std_fmt_str_m ".", std_module_name_max_len_m, module->name.string );
        return;
    }

#ifdef std_platform_win32_m
    std_module_unloader_f unloader = ( std_module_unloader_f ) GetProcAddress ( ( HMODULE ) module->handle, module_entrypoint_name );

    if ( unloader == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to unload - GetProcAddress failed to load function " std_fmt_str_m ".", module->name.string, module_entrypoint_name );
        return;
    }

#else
    std_module_unloader_f unloader = ( std_module_unloader_f ) dlsym ( ( void* ) module->handle, module_entrypoint_name );

    if ( unloader == NULL ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to unload - dlsym failed to load function " std_fmt_str_m ".", module->name.string, module_entrypoint_name );
        return;
    }

#endif

    unloader();

#if defined(std_platform_win32_m)
    BOOL unload_result = FreeLibrary ( ( HANDLE ) module->handle );

    if ( !unload_result ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to release its handle during an unload - FreeLibrary failed" );
    }
#elif defined(std_platform_linux_m)
    int unload_result = dlclose ( ( void* ) module->handle );

    if ( unload_result != 0 ) {
        std_log_error_m ( "Module " std_fmt_str_m " failed to release its handle during an unload - dlclose failed" );
    }
#endif

    std_list_push ( &std_module_state->modules_freelist, module );

    std_verify_m ( std_hash_map_remove_hash ( &std_module_state->modules_name_map, module->name.hash ) );

    std_log_info_m ( "Unload of module " std_fmt_str_m " complete.", module->name.string );
}

void std_module_unload ( const char* name ) {
    //std_rwmutex_lock_write ( &std_module_state->modules_mutex );

    std_module_t* module = std_module_lookup ( name );
    if ( !module ) {
        std_log_error_m ( "Trying to unload module " std_fmt_str_m " that is not currently loaded", name );
        //std_rwmutex_unlock_write ( &std_module_state->modules_mutex );
        return;
    }
    std_module_unload_internal ( module );

    //std_rwmutex_unlock_write ( &std_module_state->modules_mutex );
}

size_t std_module_build ( const char* solution_name, void* output, size_t output_size ) {
    // Prepare the builder process args
    const char* argv[std_process_max_args_m];
    size_t argc = 0;
    char args_buffer[std_process_args_max_len_m];
    {
        std_stack_t stack = std_static_stack_m ( args_buffer );
        
        argv[argc++] = stack.top;
        std_stack_string_copy ( &stack, std_builder_path_m );

        argv[argc++] = stack.top;
        std_stack_string_copy ( &stack, "build" );

        argv[argc++] = stack.top;
        std_stack_string_copy ( &stack, solution_name );

        argv[argc++] = stack.top;
        std_stack_string_copy ( &stack, "-r" ); // reload flag
    }

    // The builder will send back the build results to this process through this pipe
    std_process_pipe_params_t pipe_params = {
        .name = "std_module_update_pipe",
        .flags = std_process_pipe_flags_read_m | std_process_pipe_flags_blocking_m,
        .write_capacity = output_size,
        .read_capacity = output_size,
    };
    std_pipe_h pipe = std_process_pipe_create ( &pipe_params );
    std_assert_m ( pipe != std_process_null_handle_m );

    // Invoke the builder process and wait for it to finish
    std_log_info_m ( "Running build command for workspace " std_fmt_str_m "...\n", solution_name );
#if defined ( std_platform_win32_m )
    char* python_path = "python";
#elif defined ( std_platform_linux_m )
    char* python_path = "python3";
#endif
    std_process_h compiler = std_process ( python_path, "python", argv, 4, std_process_type_default_m, std_process_io_default_m );
    std_process_pipe_wait_for_connection ( pipe );

    size_t read_size = 0;
    bool result = std_process_pipe_read ( &read_size, output, output_size, pipe );
    std_assert_m ( result );

    result = std_process_wait_for ( compiler );
    std_assert_m ( result );

    std_process_pipe_destroy ( pipe );

    return read_size;
}

std_warnings_ignore_m ( "-Wunreachable-code" )
std_warnings_ignore_m ( "-Wunused-macros" )

void* std_module_reboot ( const char* solution_name ) {
    std_module_unload ( solution_name );

    char buffer[1024];
    std_module_build ( solution_name, buffer, std_static_array_size_m ( buffer ) );

    int32_t updated_modules_count = std_str_to_i32 ( buffer );

    if ( updated_modules_count > 0 ) {
        const char* p = buffer;

        for ( int32_t i = 0; i < updated_modules_count; ++i ) {

            while ( *p != '\0' ) {
                ++p;
            }

            ++p;
            const char* name = p;
            std_log_info_m ( "Rebuild of module " std_fmt_str_m " complete.", name );
        }
    } else if ( updated_modules_count == 0 ) {
        std_log_info_m ( "Module build skipped, nothing changed." );
    } else if ( updated_modules_count == -1 ) {
        std_log_info_m ( "Module build failed." );
    }

    return std_module_load ( solution_name );
}

void std_module_reload ( const char* solution_name ) {
    char buffer[1024];
    std_module_build ( solution_name, buffer, std_static_array_size_m ( buffer ) );

    int32_t updated_modules_count = std_str_to_i32 ( buffer );

    if ( updated_modules_count > 0 ) {
        const char* p = buffer;

        // Lock
        //std_rwmutex_lock_write ( &std_module_state->modules_mutex );

        for ( int32_t i = 0; i < updated_modules_count; ++i ) {

            while ( *p != '\0' ) {
                ++p;
            }

            ++p;
            const char* name = p;

            std_module_t* module = std_module_lookup ( name );

            if ( !module ) {
                std_log_error_m ( "Trying to reload module " std_fmt_str_m " when it is not loaded in the first place." );
                return;
            }

#if defined(std_platform_win32_m)
            BOOL unload_result = FreeLibrary ( ( HANDLE ) module->handle );

            if ( !unload_result ) {
                std_log_error_m ( "Module " std_fmt_str_m " failed to release its old handle during a reload - FreeLibrary failed" );
            }
#elif defined(std_platform_linux_m)
            int unload_result = dlclose ( ( void* ) module->handle );

            if ( unload_result != 0 ) {
                std_log_error_m ( "Module " std_fmt_str_m " failed to release its old handle during a reload - dlclose failed" );
            }
#endif


            // Build file and entrypoint name
            char module_file_name[std_module_name_max_len_m];
            char module_entrypoint_name[std_module_name_max_len_m];

            const char* file_prefix = std_submodules_path_m;
            const char* file_postfix = ".dll";
            const char* entrypoint_postfix = "_reload";

            if ( !std_module_file_name ( name, file_prefix, file_postfix, module_file_name ) ) {
                std_log_error_m ( "Module name max length (" std_fmt_int_m ") was exceeded for module " std_fmt_str_m ".", std_module_name_max_len_m, name );
                return;
            }

            if ( !std_module_entrypoint_name ( name, entrypoint_postfix, module_entrypoint_name ) ) {
                std_log_error_m ( "Module entrypoint name max length (" std_fmt_int_m ") was exceeded for module " std_fmt_str_m ".", std_module_name_max_len_m, name );
                return;
            }

#if defined(std_platform_win32_m)
            HMODULE handle = LoadLibrary ( module_file_name );

            if ( handle == NULL ) {
                std_log_error_m ( "Module " std_fmt_str_m " failed to reload - LoadLibrary failed on path " std_fmt_str_m ".", name, module_file_name );
                return;
            }

            std_module_reloader_f reloader = ( std_module_reloader_f ) GetProcAddress ( handle, module_entrypoint_name );

            if ( reloader == NULL ) {
                std_log_error_m ( "Module " std_fmt_str_m " failed to reload - GetProcAddress failed to load function " std_fmt_str_m ".", name, module_entrypoint_name );
                return;
            }

#elif defined(std_platform_linux_m)
            void* handle = dlopen ( module_file_name, RTLD_LAZY );

            if ( handle == NULL ) {
                std_log_error_m ( "Module " std_fmt_str_m " failed to reload - dlopen failed on path " std_fmt_str_m ".", name, module_file_name );
                return;
            }

            std_module_reloader_f reloader = ( std_module_reloader_f ) dlsym ( handle, module_entrypoint_name );

            if ( reloader == NULL ) {
                std_log_error_m ( "Module " std_fmt_str_m " failed to reload - dlsym failed to load function " std_fmt_str_m ".", name, module_entrypoint_name );
                return;
            }

#endif

            std_log_info_m ( "Reloading module " std_fmt_str_m " at entrypoint " std_fmt_str_m, name, module_entrypoint_name );
            reloader ( std_runtime_state(), module->api );

            // Update
            module->handle = ( uint64_t ) handle;

            std_log_info_m ( "Reload of module " std_fmt_str_m " complete.", name );
        }

        // Unlock, return
        //std_rwmutex_unlock_write ( &std_module_state->modules_mutex );

    } else if ( updated_modules_count == 0 ) {
        std_log_info_m ( "Module reload skipped, nothing changed." );
    } else if ( updated_modules_count == -1 ) {
        std_log_info_m ( "Module reload aborted, build failed." );
    }

    return;
}

// ----------------------------------------------------------------------------

void std_module_init ( std_module_state_t* state ) {
    //state->modules_buffer = std_buffer ( state->modules_array, std_module_max_modules_m * sizeof ( std_module_t ) );
    //state->modules_freelist = std_freelist ( state->modules_buffer, sizeof ( std_module_t ) );
    state->modules_freelist = std_static_freelist_m ( state->modules_array );
    #if 0
    state->modules_name_map =
    std_map (
        std_buffer ( state->modules_name_map_keys, std_module_map_slots_m * sizeof ( std_module_name_t ) ),
        std_buffer ( state->modules_name_map_payloads, std_module_map_slots_m * sizeof ( std_module_t* ) ),
        sizeof ( std_module_name_t ),
        sizeof ( std_module_t* ),
        std_module_hash_name, NULL,
        std_module_cmp_name, NULL
        );
    state->modules_api_map =
    std_map (
        std_buffer ( state->modules_api_map_keys, std_module_map_slots_m * sizeof ( void* ) ),
        std_buffer ( state->module_api_map_payloads, std_module_map_slots_m * sizeof ( std_module_t* ) ),
        sizeof ( void* ),
        sizeof ( std_module_t* ),
        std_module_hash_api, NULL,
        std_module_cmp_api, NULL
        );
    #else
    state->modules_name_map = std_hash_map ( state->modules_name_map_keys, state->modules_name_map_payloads, std_module_map_slots_m );
    state->modules_api_map = std_hash_map ( state->modules_api_map_keys, state->module_api_map_payloads, std_module_map_slots_m );
    #endif
    std_rwmutex_init ( &state->modules_mutex );
}

void std_module_attach ( std_module_state_t* state ) {
    std_module_state = state;
}

void std_module_shutdown ( void ) {
    std_rwmutex_deinit ( &std_module_state->modules_mutex );
}
