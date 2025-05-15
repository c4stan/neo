#pragma once

#include <std_platform.h>
#include <std_compiler.h>
#include <std_thread.h>

typedef uint64_t std_process_h;
typedef uint64_t std_pipe_h;
#define std_process_null_handle_m UINT64_MAX

typedef enum {
    std_process_type_default_m,
    std_process_type_console_m,
    std_process_type_background_m
} std_process_type_e;

typedef enum {
    std_process_io_default_m,
    std_process_io_capture_m      // Need this for std_process_get_io() to return non NULL handles.
} std_process_io_e;

typedef struct {
    uint64_t stdin_handle;
    uint64_t stdout_handle;
    uint64_t stderr_handle;
} std_process_io_t;

typedef enum {
    std_process_exit_code_completed_m,
    std_process_exit_code_error_m,
} std_process_exit_code_e;

typedef enum {
    std_process_pipe_flags_read_m = 1 << 0,
    std_process_pipe_flags_write_m = 1 << 1,
    std_process_pipe_flags_blocking_m = 1 << 2,
} std_process_pipe_flags_b;

typedef struct {
    char name[std_process_pipe_name_max_len_m];
    std_process_pipe_flags_b flags;
    size_t write_capacity;
    size_t read_capacity;
} std_process_pipe_params_t;

typedef struct {
    std_process_io_t io;
    const char* executable_path;
    const char* working_path;
    const char* name;
    const char** args;
    size_t args_count;
} std_process_info_t;

std_process_h                   std_process ( const char* executable, const char* process_name, const char** args, size_t args_count, std_process_type_e type, std_process_io_e io );
bool                            std_process_info ( std_process_info_t* info, std_process_h process );
std_process_io_t                std_process_get_io ( std_process_h process );
bool                            std_process_wait_for ( std_process_h process );
bool                            std_process_kill ( std_process_h process );

std_process_h                   std_process_this ( void );
#if 0
    const char**                    std_process_this_args ( void ); // Typical arg[0] set to process name is not part of these, TODO expose std_process_t::name
    size_t                          std_process_this_args_count ( void );
    const char*                     std_process_this_file_path ( void );
    const char*                     std_process_this_working_path ( void );
#endif
void                            std_process_this_exit ( std_process_exit_code_e exit_code );

bool                            std_process_io_read ( void* dest, size_t* read_size, size_t cap, uint64_t handle );
bool                            std_process_io_write ( uint64_t handle, size_t* write_size, const void* source, size_t size );

// TODO standardize pipes better across win32/linux
//      avoid read-write access (on linux can't block, self-consumes)
//      avoid wait_for_connection (on linux doesn't exist)
//      auto connect inside create (only supported behavior on linux)
std_pipe_h                      std_process_pipe_create ( const std_process_pipe_params_t* params );
bool                            std_process_pipe_wait_for_connection ( std_pipe_h pipe );
std_pipe_h                      std_process_pipe_connect ( const char* name, std_process_pipe_flags_b flags );
bool                            std_process_pipe_write ( size_t* out_write_size, std_pipe_h pipe, const void* data, size_t size );
bool                            std_process_pipe_read ( size_t* out_read_size, void* dest, size_t cap, std_pipe_h pipe );
void                            std_process_pipe_destroy ( std_pipe_h pipe ); // TOOD use better name, this is supposed to be called after both a create and a connect
