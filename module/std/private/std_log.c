#include <std_log.h>

#include <std_compiler.h>
#include <std_thread.h>

#include <stdio.h>

#include "std_state.h"

#if defined(std_platform_linux_m)
    #include <sys/ptrace.h>
    #include <ctype.h>
#endif

//==============================================================================

static std_log_state_t* std_log_state;

//==============================================================================

#if std_enabled_m(std_log_colored_console_output_m)
    #define std_terminal_color_reset_m    "\x1B[0m"
    //#define std_terminal_color_bright_m   "\x1B[1m"
    #define std_terminal_color_red_m      "\x1B[1m\x1B[31m"
    //#define std_terminal_color_green_m    "\x1B[1m\x1B[32m"
    #define std_terminal_color_yellow_m   "\x1B[1m\x1B[33m"
    //#define std_terminal_color_blue_m     "\x1B[1m\x1B[34m"
    //#define std_terminal_color_magenta_m  "\x1B[1m\x1B[35m"
    //#define std_terminal_color_cyan_m     "\x1B[1m\x1B[36m"
    //#define std_terminal_color_white_m    "\x1B[1m\x1B[37m"
#endif

static void std_log_default_callback ( const std_log_msg_t* msg ) {
    const char* type_prefix = "";

    if ( msg->flags & std_log_level_crash_m ) {
        type_prefix = "[CRASH] ";
    } else if ( msg->flags & std_log_level_error_m ) {
        type_prefix = "[ERROR] ";
    } else if ( msg->flags & std_log_level_warn_m ) {
        type_prefix = "[WARN] ";
    } else if ( msg->flags & std_log_level_info_m ) {
        type_prefix = "[INFO] ";
    }

    const char* color_prefix = "";
    const char* color_postfix = "";
#if std_enabled_m(std_log_colored_console_output_m)

    if ( msg->flags & ( std_log_level_error_m | std_log_level_crash_m ) ) {
        color_prefix = std_terminal_color_red_m;
    } else if ( msg->flags & std_log_level_warn_m ) {
        color_prefix = std_terminal_color_yellow_m;
    }

    color_postfix = std_terminal_color_reset_m;
#endif

    std_tick_t program_start = std_program_start_tick();
    std_tick_t now = std_tick_now();
    float delta_millis = std_tick_to_milli_f32 ( now - program_start );

    if ( msg->flags == 0 ) {
        printf ( msg->payload );
    } else {
        printf ( std_fmt_str_m
            "[" std_fmt_f32_dec_m(3) "] "
            "[" std_fmt_u32_m "/" std_fmt_str_m "] "
            "[" std_fmt_str_m ":" std_fmt_size_m "] "
            std_fmt_str_m
            std_fmt_str_m
            std_fmt_str_m,
            color_prefix,
            delta_millis / 1000.f,
            std_thread_uid ( std_thread_this() ),
            std_thread_name ( std_thread_this() ),
            msg->file, msg->line,
            type_prefix,
            msg->payload,
            std_terminal_color_reset_m );
    }

    if ( msg->flags & ( std_log_level_warn_m | std_log_level_error_m | std_log_level_crash_m ) ) {
        fflush ( stdout );
        if ( std_log_state->is_debugger_attached ) {
            std_debug_break_m();
        }
    }

    if ( msg->flags & std_log_level_crash_m ) {
        std_process_this_exit ( std_process_exit_code_error_m );
    }
}

#if defined(std_platform_linux_m)
// https://stackoverflow.com/questions/3596781/how-to-detect-if-the-current-process-is-being-run-by-gdb
static bool std_log_detect_gdb ( void ) {
#if 0
    int pid = fork();
    int status;
    int res;

    std_assert_m ( pid != -1 );

    if ( pid == 0 ) {
        int ppid = getppid();

        /* Child */
        if ( ptrace ( PTRACE_ATTACH, ppid, NULL, NULL ) == 0 ) {
            /* Wait for the parent to stop and continue it */
            waitpid ( ppid, NULL, 0 );
            ptrace ( PTRACE_CONT, NULL, NULL );

            /* Detach */
            ptrace ( PTRACE_DETACH, getppid(), NULL, NULL );

            /* We were the tracers, so gdb is not present */
            res = 0;
        } else {
            /* Trace failed so GDB is present */
            res = 1;
        }

        exit ( res );
    } else {
        waitpid ( pid, &status, 0 );
        res = WEXITSTATUS ( status );
    }

    return res;
#else
    char buf[4096];

    const int status_fd = open ( "/proc/self/status", O_RDONLY );

    if ( status_fd == -1 ) {
        return false;
    }

    const ssize_t num_read = read ( status_fd, buf, sizeof ( buf ) - 1 );
    close ( status_fd );

    if ( num_read <= 0 ) {
        return false;
    }

    buf[num_read] = '\0';
    char tracerPidString[] = "TracerPid:";
    const char* tracer_pid_ptr = strstr ( buf, tracerPidString );

    if ( !tracer_pid_ptr ) {
        return false;
    }

    for ( const char* characterPtr = tracer_pid_ptr + sizeof ( tracerPidString ) - 1; characterPtr <= buf + num_read; ++characterPtr ) {
        if ( isspace ( *characterPtr ) ) {
            continue;
        } else {
            return isdigit ( *characterPtr ) != 0 && *characterPtr != '0';
        }
    }

    return false;
#endif
}
#endif

static bool std_log_detect_debugger ( void ) {
#if defined(std_platform_linux_m)
    return std_log_detect_gdb();
#else
    return IsDebuggerPresent();
#endif
}

void std_log_init ( std_log_state_t* state ) {
    state->log_callback = std_log_default_callback;
    state->is_debugger_attached = std_log_detect_debugger();
}

void std_log_attach ( std_log_state_t* state ) {
    std_log_state = state;
}

// Default callback used in case something needs to be logged at boot time.
// The fflush call makes sure that the program flushes its output buffer before
// a possible crash.
// TODO only flush when things are really bad? (e.g. check for std_log_level_error_m)
static void std_log_boot_callback ( const std_log_msg_t* msg ) {
    printf ( msg->payload );
    fflush ( stdout );
    //std_debug_break_m ();
}

void std_log_boot ( void ) {
    static std_log_state_t state;
    std_log_attach ( &state );
    std_log_set_callback ( std_log_boot_callback );
}

//==============================================================================

void std_log_set_callback ( std_log_callback_f* callback ) {
    std_log_state->log_callback = callback;
}

//==============================================================================

void std_log_print ( std_log_msg_t msg, ... ) {
    if ( !std_log_state->log_callback ) {
        return;
    }

    char buffer[2048];

    va_list args;
    va_start ( args, msg );
    int len = vsprintf ( buffer, msg.payload, args );
    va_end ( args );
    buffer[len] = '\n';
    buffer[len + 1] = '\0';

    std_log_msg_t formatted_msg = msg;
    formatted_msg.payload = buffer;

    std_log_state->log_callback ( &formatted_msg );
}

//==============================================================================

bool std_log_debugger_attached ( void ) {
    // TODO detect every time?
    return std_log_state->is_debugger_attached;
}
