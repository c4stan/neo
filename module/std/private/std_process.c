#include <std_platform.h>
#include <std_process.h>
#include <std_mutex.h>
#include <std_thread.h>

#include "std_state.h"

#include <errno.h>

//==============================================================================

static std_process_state_t* std_process_state;
static std_process_t* std_process_self;

//==============================================================================

#if 0
static uint64_t std_process_pipe_hash_name ( const void* _name, void* _ ) {
    std_unused_m ( _ );
    const std_process_pipe_name_t* name = ( const std_process_pipe_name_t* ) _name;
    return name->hash;
}

static bool std_process_pipe_cmp_name ( uint64_t hash1, const void* _name1, const void* _name2, void* _ ) {
    std_unused_m ( _ );
    std_unused_m ( hash1 );
    const std_process_pipe_name_t* name1 = ( const std_process_pipe_name_t* ) _name1;
    const std_process_pipe_name_t* name2 = ( const std_process_pipe_name_t* ) _name2;

    if ( name1->hash != name2->hash ) {
        return false;
    }

    return std_str_cmp ( name1->name, name2->name ) == 0;
}
#endif

//==============================================================================

// Assumes that the thread running this is the main thread
// NOTE: this function uses the param state instead of the static one, and it cannot
// call to outside functions that expect a functioning std_xxx_state !
static void std_process_register_self ( std_process_state_t* state, char** args, size_t args_count ) {
    std_mutex_lock ( &state->mutex );
    std_process_t* process = std_list_pop_m ( &state->processes_freelist );
    ++state->processes_pop;

#if defined(std_platform_win32_m)
    uint64_t os_handle = ( uint64_t ) ( GetCurrentProcess() );
    uint64_t os_id = ( uint64_t ) ( GetCurrentProcessId() );
#elif defined(std_platform_linux_m)
    uint64_t os_handle = ( uint64_t ) getpid();
    uint64_t os_id = ( uint64_t ) getpid();
#endif
    //uint64_t os_thread_handle = ( uint64_t ) syscall ( SYS_gettid );
    process->os_handle = os_handle;
    process->os_id = os_id;
    //process->os_main_thread_handle = os_thread_handle;

    // executable path
#if defined(std_platform_win32_m)
    // TODO normalize the path?
    {
        WCHAR path_buffer[std_process_path_max_len_m];
        DWORD get_retcode = GetModuleFileNameW ( NULL, path_buffer, std_process_path_max_len_m );
        std_assert_m ( get_retcode > 0 && get_retcode < std_process_path_max_len_m );
        int result = WideCharToMultiByte ( CP_UTF8, 0, path_buffer, -1, process->executable_path, std_process_path_max_len_m, NULL, NULL );
        std_assert_m ( result > 0 );
    }
    //std_str_copy ( process->executable, std_process_path_max_len_m, args[0] );
#elif defined(std_platform_linux_m)
    readlink ( "/proc/self/exe", process->executable_path, std_process_path_max_len_m );
#endif

    // process name (first arg)
    std_str_copy ( process->name, std_process_name_max_len_m, args[0] );

    // remaining invocation args
    {
        std_array_t array = std_array ( std_process_args_max_len_m + std_process_max_args_m );

        for ( size_t i = 1; i  < args_count; ++i ) {
            process->args[i - 1] = process->args_buffer;
            std_str_append ( process->args_buffer, &array, args[i] );
            std_array_push ( &array, 1 );
        }
    }
    process->args_count = args_count - 1;

    // stdin/out/err
#if defined(std_platform_win32_m)
    process->stdin_handle = ( uint64_t ) ( GetStdHandle ( STD_INPUT_HANDLE ) );
    process->stdout_handle = ( uint64_t ) ( GetStdHandle ( STD_OUTPUT_HANDLE ) );
    process->stderr_handle = ( uint64_t ) ( GetStdHandle ( STD_ERROR_HANDLE ) );
#elif defined(std_platform_linux_m)
    process->stdin_handle = ( uint64_t ) STDIN_FILENO;
    process->stdout_handle = ( uint64_t ) STDOUT_FILENO;
    process->stderr_handle = ( uint64_t ) STDERR_FILENO;
#endif

    std_process_self = process;

    std_mutex_unlock ( &state->mutex );
}

//==============================================================================

void std_process_init ( std_process_state_t* state, char** args, size_t args_count ) {
    std_mem_zero_m ( state->processes_array );
    state->processes_freelist = std_static_freelist_m ( state->processes_array );
    state->processes_pop = 0;
    std_mem_zero_m ( state->pipes_array );
    state->pipes_freelist = std_static_freelist_m ( state->pipes_array );
    state->pipes_count = 0;
#if 0
    state->pipes_map = std_static_map_m ( state->pipes_map_keys, state->pipes_map_values,
                                          std_process_pipe_hash_name, NULL, std_process_pipe_cmp_name, NULL );
#endif
    std_mutex_init ( &state->mutex );
    std_process_register_self ( state, args, args_count );

    // read working path once. it is assumed that it will never change.
#if defined(std_platform_win32_m)
    {
        WCHAR path_buffer[std_process_path_max_len_m];
        DWORD get_retcode = GetCurrentDirectoryW ( std_process_path_max_len_m, path_buffer );
        std_assert_m ( get_retcode > 0 && get_retcode < std_process_path_max_len_m );
        int result = WideCharToMultiByte ( CP_UTF8, 0, path_buffer, -1, state->working_path, std_process_path_max_len_m, NULL, NULL );
        std_assert_m ( result > 0 );

        // convert \ to /. TODO fix std_str_replace?
        for ( int i = 0; i < result; ++i ) {
            if ( state->working_path[i] == '\\' ) {
                state->working_path[i] = '/';
            }
        }

        state->working_path[result - 1] = '/';
        state->working_path[result] = '\0';
    }
#else
    getcwd ( state->working_path, std_process_path_max_len_m );
    // TODO missing convert? missing wchar -> char?
#endif
}

void std_process_attach ( std_process_state_t* state ) {
    std_process_state = state;
}

void std_process_shutdown ( void ) {
    std_mutex_deinit ( &std_process_state->mutex );
}

//==============================================================================

std_process_h std_process ( const char* executable, const char* process_name, const char** args, size_t args_count, std_process_type_e type, std_process_io_e io ) {
    std_assert_m ( std_str_len ( executable ) <= std_process_path_max_len_m );
    std_assert_m ( args_count <= std_process_max_args_m );

#if defined(std_platform_win32_m)
    // Create process
    HANDLE stdin_read = NULL;
    HANDLE stdin_write = NULL;
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;

    STARTUPINFO si;
    std_mem_zero_m ( &si );

    if ( io == std_process_io_capture_m ) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof ( sa );
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;

        BOOL retval = FALSE;
        retval = CreatePipe ( &stdin_read, &stdin_write, &sa, 0 );
        std_assert_m ( retval == TRUE );
        retval = SetHandleInformation ( stdin_write, HANDLE_FLAG_INHERIT, 0 );
        std_assert_m ( retval == TRUE );
        retval = CreatePipe ( &stdout_read, &stdout_write, &sa, 0 );
        std_assert_m ( retval == TRUE );
        retval = SetHandleInformation ( stdout_read, HANDLE_FLAG_INHERIT, 0 );
        std_assert_m ( retval == TRUE );

        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    si.cb = sizeof ( si );
    si.hStdError = stdout_write;
    si.hStdOutput = stdout_write;
    si.hStdInput = stdin_read;

    DWORD flags = 0;

    switch ( type ) {
        case std_process_type_default_m:
            break;

        case std_process_type_background_m:
            flags |= CREATE_NO_WINDOW;
            break;

        case std_process_type_console_m:
            flags |= CREATE_NEW_CONSOLE;
            break;
    }

    if ( type == std_process_type_console_m && io == std_process_io_capture_m ) {
        std_log_warn_m ( "Creating a new console process while capturing its input/output. The console window will not show any i/o because it's been captured." );
    }

    uint64_t os_handle;
    uint64_t os_id;
    //uint64_t os_main_thread_handle;

    {
        PROCESS_INFORMATION pi;
        std_mem_zero_m ( &pi );

        char cmdline[std_process_cmdline_max_len_m];
        {
            std_array_t array = std_array ( std_process_cmdline_max_len_m );

            std_str_append ( cmdline, &array, executable );

            for ( size_t i = 0; i < args_count; ++i ) {
                std_str_append ( cmdline, &array, " " );
                std_str_append ( cmdline, &array, args[i] );
            }
        }

        //BOOL retval = CreateProcess ( executable, cmdline, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi );
        BOOL retval = CreateProcess ( NULL, cmdline, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi );
        std_assert_m ( retval == TRUE );

        os_handle = ( uint64_t ) pi.hProcess;
        os_id = ( uint64_t ) pi.dwProcessId;
        //os_main_thread_handle = ( uint64_t ) pi.hThread;
    }

    {
        BOOL retval;

        if ( stdin_read != NULL ) {
            retval = CloseHandle ( stdin_read );
            std_assert_m ( retval == TRUE );
        }

        if ( stdout_write != NULL ) {
            retval = CloseHandle ( stdout_write );
            std_assert_m ( retval == TRUE );
        }
    }

#elif defined(std_platform_linux_m)

    // 0 is read, 1 is write
    int stdin[2]  = { -1, -1 };
    int stdout[2] = { -1, -1 };

    if ( io == std_process_io_capture_m ) {
        int retval = pipe ( stdin );
        std_assert_m ( retval != -1 );

        retval = pipe ( stdout );
        std_assert_m ( retval != -1 );
    }

    pid_t pid = fork();

    if ( pid == 0 ) {
        // child
        if ( io == std_process_io_capture_m ) {
            int retval = dup2 ( stdin[0], STDIN_FILENO );
            std_assert_m ( retval != -1 );

            retval = dup2 ( stdout[1], STDOUT_FILENO );
            std_assert_m ( retval != -1 );
        }

        char* process_args[std_process_max_args_m + 4];
        {
            size_t arg_idx = 0;

            if ( type == std_process_type_console_m ) {
                process_args[arg_idx++] = "xterm";
                process_args[arg_idx++] = "-e";
                process_args[arg_idx++] = ( char* ) executable;

            } else {
                process_args[arg_idx++] = ( char* ) process_name;
            }

            for ( size_t i = 0; i < args_count; ++i ) {
                process_args[arg_idx++] = ( char* ) args[i];
            }

            process_args[arg_idx++] = NULL;
        }

        if ( type == std_process_type_console_m ) {
            // Note: trying to do this using gnome-terminal won't work, a new process gets spawned that the parent is unable to wait on
            execv ( "/usr/bin/xterm", process_args );
        } else {
            // why does execv take a pointer to a non-const char? can the arg be modified?
            execv ( executable, process_args );
        }

        // TODO some error occured, look at errno
        //std_log_error_m ( "Process exited with errno code " std_fmt_int_m, errno );
    } else {
        std_assert_m ( pid > 0 );

        // Close child process stdin read and stdout write, not needed by the parent
        if ( io == std_process_io_capture_m ) {
            close ( stdin[0] );
            close ( stdout[1] );
        }
    }

    uint64_t stdin_write = ( uint64_t ) stdin[1];
    uint64_t stdout_read = ( uint64_t ) stdout[0];
    uint64_t os_handle = pid;
    uint64_t os_id = pid;

#endif

    // expects: stdin/out_read/write, os_handle, os_id

    // Lock
    // Pop
    // Init process
    // Increase processes pop
    std_mutex_lock ( &std_process_state->mutex );

    std_process_t* process = std_list_pop_m ( &std_process_state->processes_freelist );
    std_assert_m ( process );

    process->os_handle = os_handle;
    process->os_id = os_id;
    //process->os_main_thread_handle = os_main_thread_handle;
    process->stdin_handle = ( uint64_t ) stdin_write;
    process->stdout_handle = ( uint64_t ) stdout_read;
    process->stderr_handle = ( uint64_t ) stdout_read;
    // TODO should make sure that this is an absolute path?
    std_str_copy ( process->executable_path, std_process_path_max_len_m, executable );
    std_str_copy ( process->name, std_process_name_max_len_m, process_name );

    {
        std_array_t array = std_array ( std_process_args_max_len_m + std_process_max_args_m );

        for ( size_t i = 0; i  < args_count; ++i ) {
            process->args[i] = process->args_buffer;
            std_str_append ( process->args_buffer, &array, args[i] );
            std_array_push ( &array, 1 );
        }
    }

    process->args_count = args_count;
    ++std_process_state->processes_pop;
    // Unlock
    // Return handle
    std_mutex_unlock ( &std_process_state->mutex );
    std_process_h handle = ( std_process_h ) ( process - std_process_state->processes_array );
    return handle;
}

std_process_io_t std_process_get_io ( std_process_h process_handle ) {
    std_process_t* process = &std_process_state->processes_array[process_handle];
    std_process_io_t io;

    std_mutex_lock ( &std_process_state->mutex );
    io.stdin_handle = process->stdin_handle;
    io.stdout_handle = process->stdout_handle;
    io.stderr_handle = process->stderr_handle;
    std_mutex_unlock ( &std_process_state->mutex );

    return io;
}

bool std_process_wait_for ( std_process_h process_handle ) {
    std_process_t* process = &std_process_state->processes_array[process_handle];

    bool dead;
#if defined(std_platform_win32_m)
    DWORD wait_retval = WaitForSingleObject ( ( HANDLE ) process->os_handle, INFINITE );
    dead = wait_retval != WAIT_FAILED;
#elif defined(std_platform_linux_m)
    int wait_retval = waitpid ( process->os_handle, NULL, 0 );
    dead = wait_retval == process->os_handle;
#endif

    if ( dead ) {
        std_list_push ( &std_process_state->processes_freelist, process );
    }

    return dead;
}

bool std_process_kill ( std_process_h process_handle ) {
    std_process_t* process = &std_process_state->processes_array[process_handle];

    bool dead;
#if defined(std_platform_win32_m)
    BOOL kill_retval = TerminateProcess ( ( HANDLE ) process->os_handle, 0 );
    dead = kill_retval == TRUE;
#elif defined(std_platform_linux_m)
    int kill_retval = kill ( process->os_handle, SIGKILL );
    dead = kill_retval == 0;
#endif

    if ( dead ) {
        std_list_push ( &std_process_state->processes_freelist, process );
    }

    return dead;
}

std_process_h std_process_this ( void ) {
#if defined(std_platform_win32_m)
    uint64_t os_handle = ( uint64_t ) ( GetCurrentProcess() );
#elif defined(std_platform_linux_m)
    uint64_t os_handle = ( uint64_t ) getpid();
#endif

    std_mutex_lock ( &std_process_state->mutex );

    for ( size_t i = 0; i < std_process_state->processes_pop; ++i ) {
        if ( std_process_state->processes_array[i].os_handle == os_handle ) {
            std_mutex_unlock ( &std_process_state->mutex );
            return ( std_process_h ) i;
        }
    }

    std_mutex_unlock ( &std_process_state->mutex );
    return std_process_null_handle_m;
}

std_no_return_m std_process_this_exit ( std_process_exit_code_e exit_code ) {
#if defined(std_platform_win32_m)
    uint32_t exit_code_value = exit_code == std_process_exit_code_completed_m ? 0 : 1;
    ExitProcess ( exit_code_value );
#elif defined(std_platform_linux_m)
    int32_t exit_code_value = exit_code == std_process_exit_code_completed_m ? 0 : 1;
    exit ( exit_code_value );
#endif
}

bool std_process_info ( std_process_info_t* info, std_process_h process_handle ) {
    std_process_t* process = &std_process_state->processes_array[process_handle];

    info->io.stdin_handle = process->stdin_handle;
    info->io.stdout_handle = process->stdout_handle;
    info->io.stderr_handle = process->stderr_handle;
    info->executable_path = process->executable_path;
    info->working_path = std_process_state->working_path;
    info->name = process->name;
    info->args = ( const char** ) process->args;
    info->args_count = process->args_count;

    return true;
}

bool std_process_io_read ( void* dest, size_t* out_read_size, size_t size, uint64_t pipe ) {
#if defined(std_platform_win32_m)
    DWORD read_size;
    std_assert_m ( size < UINT32_MAX );
    BOOL read_retcode = ReadFile ( ( HANDLE ) pipe, dest, ( DWORD ) size, &read_size, NULL );

    if ( read_retcode == FALSE ) {
        DWORD error = GetLastError();
        std_assert_m ( error == ERROR_BROKEN_PIPE );

        if ( out_read_size ) {
            *out_read_size = 0;
        }

        return false;
    }

    if ( out_read_size ) {
        *out_read_size = ( size_t ) read_size;
    }

    return true;
#elif defined(std_platform_linux_m)
    ssize_t read_retcode = read ( ( int ) pipe, dest, size );

    if ( out_read_size ) {
        *out_read_size = ( size_t ) read_retcode;
    }

    return read_retcode != 0;
#endif
}

bool std_process_io_write ( uint64_t pipe, size_t* out_write_size, const void* source, size_t size ) {
#if defined(std_platform_win32_m)
    DWORD write_size;
    std_assert_m ( size < UINT32_MAX );
    BOOL write_retcode = WriteFile ( ( HANDLE ) pipe, source, ( DWORD ) size, &write_size, NULL );

    if ( write_retcode == FALSE ) {
        return false;
    }

    if ( out_write_size ) {
        *out_write_size = ( size_t ) write_size;
    }

    return true;

#elif defined(std_platform_linux_m)
    ssize_t write_retcode = write ( ( int ) pipe, source, size );

    if ( write_retcode == -1 ) {
        *out_write_size = 0;
        return false;
    }

    if ( out_write_size ) {
        *out_write_size = ( size_t ) write_retcode;
    }

    return true;
#endif
}

// -----------------------

std_pipe_h std_process_pipe_create ( const std_process_pipe_params_t* params ) {
#if defined(std_platform_win32_m)
    DWORD open_mode = FILE_FLAG_FIRST_PIPE_INSTANCE;

    if ( ( params->flags & std_process_pipe_flags_read_m ) && ( params->flags & std_process_pipe_flags_write_m ) ) {
        open_mode |= PIPE_ACCESS_DUPLEX;
    } else if ( params->flags & std_process_pipe_flags_read_m ) {
        open_mode |= PIPE_ACCESS_INBOUND;
    } else if ( params->flags & std_process_pipe_flags_write_m ) {
        open_mode |= PIPE_ACCESS_OUTBOUND;
    }

    std_assert_m ( open_mode != 0 );

    DWORD pipe_mode = PIPE_TYPE_BYTE;

    if ( params->flags & std_process_pipe_flags_read_m ) {
        pipe_mode |= PIPE_READMODE_BYTE;
    }

    if ( params->flags & std_process_pipe_flags_blocking_m ) {
        pipe_mode |= PIPE_WAIT;
    } else {
        pipe_mode |= PIPE_NOWAIT;
    }

    DWORD max_instances = 1; // TODO handle multiple instances?
    DWORD out_buffer_size = ( DWORD ) params->write_capacity;
    DWORD in_buffer_size = ( DWORD ) params->read_capacity;

    char pipe_name[256];
    {
        const char* win32_pipe_prefix = "\\\\.\\pipe\\";
        std_array_t array = std_static_array_m ( pipe_name );
        std_str_append ( pipe_name, &array, win32_pipe_prefix );
        std_str_append ( pipe_name, &array, params->name );
    }

    HANDLE handle = CreateNamedPipe ( pipe_name, open_mode, pipe_mode, max_instances, out_buffer_size, in_buffer_size, 0, NULL );

    if ( handle == INVALID_HANDLE_VALUE ) {
        DWORD error = GetLastError();
        std_log_error_m ( "Pipe creation failed with error code " std_fmt_int_m, error );
        return std_process_null_handle_m;
    }

    // TODO lock/unlock a mutex
    std_process_pipe_t* pipe = std_list_pop_m ( &std_process_state->pipes_freelist );
    std_assert_m ( pipe );
    pipe->os_handle = ( uint64_t ) handle;
    pipe->is_owner = true;
    pipe->params = *params;
    pipe->params.name = NULL;
    std_str_copy ( pipe->name, std_process_pipe_name_max_len_m, params->name );

    std_pipe_h pipe_handle = ( uint64_t ) ( pipe - std_process_state->pipes_array );
    return pipe_handle;

#if 0
    std_process_pipe_name_t pipe_name;
    pipe_name.hash = std_hash_djb2_64 ( params->name, std_str_len ( params->name ) );
    std_str_copy ( pipe_name.name, std_process_pipe_name_max_len_m, params->name );
    std_map_insert ( &state->pipes_map, &pipe_name, &pipe );
#endif
#else
    mkfifo ( params->name, 0666 );

    int flags = 0;

    if ( ( params->flags & std_process_pipe_flags_read_m ) && ( params->flags & std_process_pipe_flags_write_m ) ) {
        flags |= O_RDWR;
    } else if ( params->flags & std_process_pipe_flags_read_m ) {
        flags |= O_RDONLY;
    } else if ( params->flags & std_process_pipe_flags_write_m ) {
        flags |= O_WRONLY;
    }

    if ( params->flags & std_process_pipe_flags_blocking_m == 0 ) {
        flags |= O_NONBLOCK;
    }

    int fd = open ( params->name, flags );

    // TODO lock/unlock a mutex
    std_process_pipe_t* pipe = std_list_pop_m ( &std_process_state->pipes_freelist );
    std_assert_m ( pipe );
    pipe->os_handle = ( uint64_t ) fd;
    pipe->is_owner = true;
    pipe->params = *params;
    pipe->params.name = NULL;
    std_str_copy ( pipe->name, std_process_pipe_name_max_len_m, params->name );

    std_pipe_h pipe_handle = ( uint64_t ) ( pipe - std_process_state->pipes_array );
    return pipe_handle;
#endif
}

bool std_process_pipe_wait_for_connection ( std_pipe_h pipe_handle ) {
#if defined(std_platform_win32_m)
    std_process_pipe_t* pipe = &std_process_state->pipes_array[pipe_handle];

    BOOL connect_result = ConnectNamedPipe ( ( HANDLE ) pipe->os_handle, NULL );

    return connect_result == TRUE;
#elif defined(std_platform_linux_m)
    // TODO linux
    return false;
#endif
}

std_pipe_h std_process_pipe_connect ( const char* name, std_process_pipe_flags_b flags ) {
#if defined(std_platform_win32_m)
    DWORD access = 0;

    if ( flags & std_process_pipe_flags_read_m ) {
        access |= GENERIC_READ;
    }

    if ( flags & std_process_pipe_flags_write_m ) {
        access |= GENERIC_WRITE;
    }

    if ( flags & std_process_pipe_flags_blocking_m ) {
        WaitNamedPipe ( name, NMPWAIT_WAIT_FOREVER );
    }

    char pipe_name[256];
    {
        const char* win32_pipe_prefix = "\\\\.\\pipe\\";
        std_array_t array = std_static_array_m ( pipe_name );
        std_str_append ( pipe_name, &array, win32_pipe_prefix );
        std_str_append ( pipe_name, &array, name );
    }

    HANDLE handle = CreateFile ( pipe_name, access, 0, NULL, OPEN_EXISTING, 0, NULL );

    if ( handle != INVALID_HANDLE_VALUE ) {
        std_process_pipe_t* pipe = std_list_pop_m ( &std_process_state->pipes_freelist );
        std_assert_m ( pipe );
        pipe->os_handle = ( uint64_t ) handle;
        pipe->is_owner = false;
        std_mem_zero_m ( &pipe->params );
        std_str_copy ( pipe->name, std_process_pipe_name_max_len_m, name );

        DWORD mode = PIPE_READMODE_BYTE;

        if ( flags & std_process_pipe_flags_blocking_m ) {
            mode |= PIPE_WAIT;
        } else {
            mode |= PIPE_NOWAIT;
        }

        BOOL set_state_result = SetNamedPipeHandleState ( handle, &mode, NULL, NULL );

        if ( set_state_result == FALSE ) {
            std_log_error_m ( "Connection to pipe " std_fmt_str_m " failed to set pipe state.", name );
        }

        std_pipe_h pipe_handle = ( uint64_t ) ( pipe - std_process_state->pipes_array );
        return pipe_handle;
    } else {
        return std_process_null_handle_m;
    }

#elif defined(std_platform_linux_m)
    // TODO linux
    return std_process_null_handle_m;
#endif
}

bool std_process_pipe_write ( size_t* out_write_size, std_pipe_h pipe_handle, const void* data, size_t size ) {
#if defined(std_platform_win32_m)
    std_process_pipe_t* pipe = &std_process_state->pipes_array[pipe_handle];

    DWORD write_size = 0;
    DWORD data_size = ( DWORD ) size;
    BOOL write_result = WriteFile ( ( HANDLE ) pipe->os_handle, data, data_size, &write_size, NULL );

    if ( write_result == FALSE ) {
        DWORD error = GetLastError();
        std_log_error_m ( "Pipe write failed with error code " std_fmt_int_m, error );
        return false;
    }

    if ( out_write_size ) {
        *out_write_size = ( size_t ) write_size;
    }

    return true;
#elif defined(std_platform_linux_m)
    // TODO linux
    return false;
#endif
}

bool std_process_pipe_read ( size_t* out_read_size, void* dest, size_t capacity, std_pipe_h pipe_handle ) {
#if defined(std_platform_win32_m)
    std_process_pipe_t* pipe = &std_process_state->pipes_array[pipe_handle];

    DWORD read_size = 0;
    DWORD dest_capacity = ( DWORD ) capacity;
    BOOL read_result = ReadFile ( ( HANDLE ) pipe->os_handle, dest, dest_capacity, &read_size, NULL );

    if ( read_result == FALSE ) {
        return false;
    }

    if ( out_read_size ) {
        *out_read_size = ( size_t ) read_size;
    }

    return true;
#elif defined(std_platform_linux_m)
    // TODO linux
    return false;
#endif
}

void std_process_pipe_destroy ( std_pipe_h pipe_handle ) {
#if defined(std_platform_win32_m)
    std_process_pipe_t* pipe = &std_process_state->pipes_array[pipe_handle];

    CloseHandle ( ( HANDLE ) pipe->os_handle );

    std_list_push ( &std_process_state->pipes_freelist, pipe );
#elif defined(std_platform_linux_m)
    // TODO linux
#endif
}
