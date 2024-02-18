#pragma once

/*
-------------------------------------------------------------------------------

    Log callbacks are bound in App code
    All modules simply log to the callback (if any)
    So callbacks are simply a pointer set by app code
    and read from anywhere.

-------------------------------------------------------------------------------
*/

#include <std_byte.h>
#include <std_string.h>
#include <std_compiler.h>
#include <std_process.h>

// Type encodings for format, same as stdio
#define std_fmt_size_m    "%zu"
#define std_fmt_int_m     "%d"
#define std_fmt_uint_m    "%u"
#define std_fmt_h32_m     "0x%X"
#define std_fmt_h64_m     "0x%llX"
#define std_fmt_u8_m      "%u"
#define std_fmt_byte_m    std_fmt_u8_m
#define std_fmt_bool_m    std_fmt_u8_m
#define std_fmt_u16_m     "%u"
#define std_fmt_u32_m     "%"PRIu32
#define std_fmt_i32_m     "%"PRId32
#define std_fmt_u64_m     "%"PRIu64
#define std_fmt_i64_m     "%"PRId64
#define std_fmt_f32_m     "%f"
#define std_fmt_f64_m     "%f"
#define std_fmt_str_m     "%s"
#define std_fmt_ptr_m     "%p"
#define std_fmt_tick_m    std_fmt_u64_m
#define std_fmt_newline_m "\n"
#define std_fmt_tab_m     "\t"
// Causes the print to overwrite the last printed line. Can be stacked to overwrite multiple lines.
#define std_fmt_prevline_m "\033[F"
#define std_fmt_f32_dec_m(X) "%." std_pp_string_m(X) "f"
#define std_fmt_int_pad_m(X) "%0" std_pp_string_m(X) "d"
#define std_fmt_i64_pad_m(X) "%0" std_pp_string_m(X) PRId64
#define std_fmt_u32_pad_m(X) "%0" std_pp_string_m(X) PRIu32
#define std_fmt_u64_pad_m(X) "%0" std_pp_string_m(X) PRIu64

typedef uint32_t std_log_level_f;
// App code can define and use more flags.
// Log flags are supposed to be directly handled by callback code.
#define std_log_level_info_m  (1 << 0)
#define std_log_level_warn_m  (1 << 1)
#define std_log_level_debug_m (1 << 2)
#define std_log_level_error_m (1 << 3)
#define std_log_level_crash_m (1 << 4)
#define std_log_level_all_m   (-1)

// Boolean values that can be tested for code that should only be executed
// depending on which logging is enabled.
#define std_log_info_enabled_m        ( std_log_enabled_levels_m & std_log_level_info_m )
#define std_log_warn_enabled_m        ( std_log_enabled_levels_m & std_log_level_warn_m )
#define std_log_debug_enabled_m       ( std_log_enabled_levels_m & std_log_level_debug_m )
#define std_log_error_enabled_m       ( std_log_enabled_levels_m & std_log_level_error_m )
#define std_log_crash_enabled_m       ( std_log_enabled_levels_m & std_log_level_crash_m )
#define std_assert_enabled_m          std_log_error_enabled_m

// Log callbacks are given as param a struct containing a bunch of useful log
// data and they're free to use it in whatever way.
typedef struct {
    const char* module;
    const char* file;
    const char* function;
    size_t line;
    const char* payload;
    std_log_level_f flags;
} std_log_msg_t;

typedef void ( std_log_callback_f ) ( const std_log_msg_t* msg );

void    std_log_set_callback       ( std_log_callback_f* callback );    // Callback binder. TODO add flags listened as param?
void    std_log_print              ( std_log_msg_t msg, ... );          // Callback caller.
#define std_log_msg_m( flags, fmt )  ( std_log_msg_t ) { std_pp_eval_string_m(std_module_name_m), std_pp_eval_string_m(std_file_name_m), std_func_name_m, std_line_num_m, (fmt), (flags) }
#define std_log_m( flags, fmt, ... ) std_log_print ( std_log_msg_m ( (flags), (fmt) ), ##__VA_ARGS__ )

// Callback caller wrappers with static log level check.
#if std_log_enabled_levels_m & std_log_level_info_m
    #define std_log_info_m(fmt, ...)     std_log_m(std_log_level_info_m, (fmt), ##__VA_ARGS__)
#else
    #define std_log_info_m (fmt, ...)
#endif

#if std_log_enabled_levels_m & std_log_level_warn_m
    #define std_log_warn_m(fmt, ...)     std_log_m(std_log_level_warn_m, (fmt), ##__VA_ARGS__)
#else
    #define std_log_warn_m(fmt, ...)
#endif

#if std_log_enabled_levels_m & std_log_level_debug_m
    #define std_log_debug_m(fmt, ...)     std_log_m(std_log_level_debug_m, (fmt), ##__VA_ARGS__)
#else
    #define std_log_debug_m(fmt, ...)
#endif

#if std_log_enabled_levels_m & std_log_level_error_m
    #define std_log_error_m(fmt, ...)     std_log_m(std_log_level_error_m, (fmt), ##__VA_ARGS__)
#else
    #define std_log_error_m(fmt, ...)
#endif

#if std_log_enabled_levels_m & std_log_level_crash_m
    #define std_log_crash_m(fmt, ...)     std_log_m(std_log_level_crash_m, (fmt), ##__VA_ARGS__)

#else
    #define std_log_crash_m(fmt, ...)
#endif

// Note: when asserts are turned off the condition code is executed anyway!
// Manually guard against execution by testing std_assert_enabled_m if that's not desired
#if std_assert_enabled_m
    #define std_assert_info_m(cond, ...)  if (!(cond)) { std_log_info_m(__VA_ARGS__);  }
    #define std_assert_warn_m(cond, ...)  if (!(cond)) { std_log_warn_m(__VA_ARGS__);  }
    #define std_assert_error_m(cond, ...) if (!(cond)) { std_log_error_m(__VA_ARGS__); }
    #define std_assert_crash_m(cond, ...) if (!(cond)) { std_log_crash_m(__VA_ARGS__); }
#else
    #define std_assert_info_m(cond, ...)  (cond)
    #define std_assert_warn_m(cond, ...)  (cond)
    #define std_assert_error_m(cond, ...) (cond)
    #define std_assert_crash_m(cond, ...) (cond)
#endif

// Convenie generic assert macro that takes an optional message and defaults to ERROR on fail
#define std_assert_msg_m(exp, ...)    std_assert_error_m( (exp), __VA_ARGS__ )
#define std_assert_nomsg_m(exp)       std_assert_error_m( (exp), "FAILED ASSERT: " #exp )
#define std_assert_overload_m(_1, _2, _NAME, ...) _NAME
// TODO This breaks when passing a string message and additional args to format!
#define std_assert_m(...)             std_assert_overload_m(__VA_ARGS__, std_assert_msg_m, std_assert_nomsg_m) (__VA_ARGS__)

// Default fail for not implemented code paths
#define std_not_implemented_m()       std_log_error_m("Code path not implemented yet.")

// TODO create std_debug and move this there?
bool std_log_debugger_attached ( void );
