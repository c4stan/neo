#pragma once

#include <std_platform.h>

#if 1
#if defined(__GNUC__)
    #define std_compiler_gcc_m
#elif defined(__clang__)
    #define std_compiler_clang_m
#elif defined(_MSC_VER)
    #define std_compiler_ms_m
#else
    #error Unknown compiler
#endif
#else
    #define std_compiler_clang_m
#endif

#if !defined(NULL)
    #define NULL ( ( void * ) 0 )
#endif

/*
    TODO:
        need a way to output compiler warnings in a portable way

    Clang is the only compiler currently supported. It is likely that it will remain that way,
    don't see a reason to switch to GCC or MSVC (*especially* not MSVC).
    Regardless, all compiler specific keywords should be wrapped in macros. Making sure that
    the wrapping is also good for generalizing to other popular compilers would be nice
    but not a priority for now.
*/

#if defined(std_compiler_clang_m)

    #define std_warnings_save_state_m()                           _Pragma ( "clang diagnostic push" )
    #define std_warnings_restore_state_m()                        _Pragma ( "clang diagnostic pop" )
    #define std_warnings_ignore_m( x )                            _Pragma ( std_pp_string_m ( clang diagnostic ignored x ) )
    //#define std_ignore_warning_begin_m( x )                       _Pragma ( "clang diagnostic push" ) _Pragma ( std_pp_string_m ( clang diagnostic ignored x ) )
    //#define std_ignore_warning_end_m()                            _Pragma ( "clang diagnostic pop" )
    // TODO remove this
    #define std_ignore_warning_m( exp, warning )                  std_warnings_save_state_m() std_warnings_ignore_m(warning) exp; std_warnings_restore_state_m()
    #define std_disable_optimization_begin_m()                    _Pragma ( "clang optimize off")
    #define std_disable_optimization_end_m()                      _Pragma ( "clang optimize on")
    #define std_set_packing_m( n )                                _Pragma ( "pack(n)" )
    #define std_reset_packing_m                                   _Pragma ( "pack()" )
    #define std_push_packing_m( n )                               _Pragma ( "pack(push, n)" )
    #define std_pop_packing_m( n )                                _Pragma ( "pack(pop, n)" )
    #define std_static_align_m( n )                               __attribute__ (( aligned ( n ) ))
    #define std_typeof_m( x )                                     __typeof__ ( x )
    #define std_alignof_m( x )                                    __alignof__ ( x )
    #define std_inline_m                                          __attribute__ (( always_inline ))
    #define std_noinline_m                                        __attribute__ (( noinline ))
    #define std_noop_m                                            __asm__ ( "nop" )
    //( ( void ) 0 )

    // TODO does this work on local scope variables?
    #define std_unused_static_m()                               __attribute__ (( used ))

    #if defined(std_platform_win32_m)
        #define std_module_export_m                               __attribute__ (( dllexport ))
    #elif defined(std_platform_linux_m)
        #define std_module_export_m                               __attribute__ (( visibility("default") ))
    #endif

    #define std_asm_m                                             __asm__
    #define std_thread_local_m                                    __thread
    #define std_likely_m( x )                                     __builtin_expect ( (x), 1 )
    #define std_unlikely_m( x )                                   __builtin_expect ( (x), 0 )
    #define std_unused_m( x )                                     ( void ) x
    #define std_on_m                                              1
    #define std_off_m                                             0
    #define std_pp_string_m( x )                                  #x
    #define std_pp_eval_string_m( x )                             std_pp_string_m ( x )
    #define std_pp_concat_m(A, B)                                 A ## B
    #define std_pp_eval_concat_m(A, B)                            std_pp_concat_m(A, B)
    #define std_pp_eval_m( x )                                    x
    // use std_file_name_m instead
    //#define std_file_path_m                                       __FILE__
    #define std_func_name_m                                       __func__
    #define std_line_num_m                                        __LINE__
    #define std_field_size_m( type, field )                       sizeof( ( ( type * ) 0 )->field )
    #define std_field_offset_m( type, field )                     offsetof( type, field )
    #define std_static_array_stride_m( a )                        sizeof ( a[0] )
    // TODO rename to std_static_array_count?
    #define std_static_array_capacity_m( a )                      sizeof ( a ) / sizeof ( a[0] )
    #define std_static_array_size_m( a )                          sizeof ( a )
    #define std_array_typecast_m( a )                             ( std_typeof_m ( &( (a)[0] ) ) )
    #define std_no_return_m                                       __attribute__((noreturn)) void
    #define std_static_assert_m( exp )                            _Static_assert ( exp, "FAILED STATIC ASSERT: " #exp )
    #define std_pad_m( size )                                     std_pp_eval_concat_m ( char __pad, std_line_num_m ) [size]
    #define std_auto_m                                            __auto_type

    #if defined(std_platform_win32_m)
        #define std_debug_break_m()                             __debugbreak()
    #else
        #define std_debug_break_m()                             __builtin_debugtrap()
    #endif

    #define std_div_ceil_m( dividend, divisor )                 ( ( (dividend) + (divisor) - 1 ) / (divisor) )

#elif defined(std_compiler_gcc_m)

    #define std_warnings_save_state_m()                           _Pragma ( "GCC diagnostic push" )
    #define std_warnings_restore_state_m()                        _Pragma ( "GCC diagnostic pop" )
    #define std_warnings_ignore_m( x )                            _Pragma ( std_pp_string_m ( GCC diagnostic ignored x ) )
    //#define std_warnings_ignore_m( x )                            _Pragma ( "GCC diagnostic ignored " x )
    //#define std_warnings_ignore_m( x )                            _Pragma ( std_pp_eval_concat_m ( std_pp_eval_concat_m ( std_pp_string_m ( GCC diagnostic ignored ), " " ), x ) )
    //#define std_ignore_warning_begin_m( x )                       _Pragma ( "clang diagnostic push" ) _Pragma ( std_pp_string_m ( clang diagnostic ignored x ) )
    //#define std_ignore_warning_end_m()                            _Pragma ( "clang diagnostic pop" )
    // TODO remove this
    #define std_ignore_warning_m( exp, warning )                  std_warnings_save_state_m() std_warnings_ignore_m ( warning ) exp; std_warnings_restore_state_m()
    #define std_disable_optimization_begin_m()                    _Pragma ( "GCC optimize off")
    #define std_disable_optimization_end_m()                      _Pragma ( "GCC optimize on")
    #define std_set_packing_m( n )                                _Pragma ( "pack(n)" )
    #define std_reset_packing_m                                   _Pragma ( "pack()" )
    #define std_push_packing_m( n )                               _Pragma ( "pack(push, n)" )
    #define std_pop_packing_m( n )                                _Pragma ( "pack(pop, n)" )
    #define std_static_align_m( n )                               __attribute__ (( aligned ( n ) ))
    #define std_typeof_m( x )                                     __typeof__ ( x )
    #define std_alignof_m( x )                                    __alignof__ ( x )
    #define std_inline_m                                          __attribute__ (( always_inline ))
    #define std_noinline_m                                        __attribute__ (( noinline ))
    #define std_noop_m                                            __asm__("nop")
    //( ( void ) 0 )

    // TODO does this work on local scope variables?
    #define std_unused_static_m()                               __attribute__ (( used ))

    #if defined(std_platform_win32_m)
        #define std_module_export_m                               __attribute__ (( dllexport ))
    #elif defined(std_platform_linux_m)
        #define std_module_export_m                               __attribute__ (( visibility("default") ))
    #endif

    #define std_asm_m                                             __asm__
    #define std_thread_local_m                                    __thread
    #define std_likely_m( x )                                     __builtin_expect ( (x), 1 )
    #define std_unlikely_m( x )                                   __builtin_expect ( (x), 0 )
    #define std_unused_m( x )                                     ( void ) x
    #define std_on_m                                              1
    #define std_off_m                                             0
    #define std_pp_string_m( x )                                  #x
    #define std_pp_eval_string_m( x )                             std_pp_string_m ( x )
    #define std_pp_concat_m(A, B)                                 A ## B
    #define std_pp_eval_concat_m(A, B)                            std_pp_concat_m(A, B)
    #define std_pp_eval_m( x )                                    x
    // use std_file_name_m instead
    //#define std_file_path_m                                       __FILE__
    #define std_func_name_m                                       __func__
    #define std_line_num_m                                        __LINE__
    #define std_field_size_m( type, field )                       sizeof( ( ( type * ) 0 )->field )
    #define std_field_offset_m( type, field )                     offsetof( type, field )
    #define std_static_array_stride_m( a )                        sizeof ( a[0] )
    #define std_static_array_capacity_m( a )                      sizeof ( a ) / sizeof ( a[0] )
    #define std_static_array_size_m( a )                          sizeof ( a )
    #define std_array_typecast_m( a )                             ( std_typeof_m ( &( (a)[0] ) ) )
    #define std_no_return_m                                       __attribute__((noreturn)) void
    #define std_static_assert_m( exp )                            _Static_assert ( exp, "FAILED STATIC ASSERT: " #exp )
    #define std_pad_m( size )                                     std_pp_eval_concat_m ( char __pad, std_line_num_m ) [size]
    #define std_auto_m                                            __auto_type

    #if defined(std_platform_win32_m)
        #define std_debug_break_m()                                 __debugbreak()
    #else
        #define std_debug_break_m()                                 __builtin_trap()
    #endif

    #define std_div_ceil_m( dividend, divisor )                     ( ( (dividend) + (divisor) - 1 ) / (divisor) )
#else

    #error The only currently supported compiler is Clang.

#endif
