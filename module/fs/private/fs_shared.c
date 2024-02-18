#include "fs_shared.h"

#include <std_platform.h>
#include <std_compiler.h>
#include <std_log.h>

#if defined(std_platform_win32_m)
std_thread_local_m char  t_char_buffer[fs_path_size_m];
std_thread_local_m WCHAR t_path_buffer[fs_path_size_m];
std_thread_local_m WCHAR t_path_buffer_2[fs_path_size_m];

size_t fs_path_to_str ( const WCHAR* path, char* str, size_t cap ) {
    std_assert_m ( cap <= INT_MAX );
    int size = WideCharToMultiByte ( CP_UTF8, 0, path, -1, str, ( int ) cap, NULL, NULL );

#if std_assert_enabled_m

    if ( size == 0 ) {
        DWORD error = GetLastError();
        std_log_warn_m ( "Conversion from wide string to unicode failed, possible error code: " std_fmt_int_m, error );
    }

#endif

    return ( size_t ) size;
}

size_t fs_str_to_path ( const char* str, WCHAR* path, size_t cap ) {
    std_assert_m ( cap <= INT_MAX );
    int size = MultiByteToWideChar ( CP_UTF8, 0, str, -1, path, ( int ) cap );
    return ( size_t ) size;
}

size_t fs_to_path_buffer ( const char* str ) {
    return fs_str_to_path ( str, t_path_buffer, fs_path_size_m );
    // TODO error check
}

size_t fs_from_path_buffer ( char* str, size_t cap ) {
    return fs_path_to_str ( t_path_buffer, str, cap );
    // TODO doublecheck that params are correct
    // TODO error check
}

size_t fs_to_path_buffer_2 ( const char* str ) {
    return fs_str_to_path ( str, t_path_buffer_2, fs_path_size_m );
    // TODO error check
}

size_t fs_from_path_buffer_2 ( char* str, size_t cap ) {
    return fs_path_to_str ( t_path_buffer_2, str, cap );
    // TODO doublecheck that params are correct
    // TODO error check
}

void fs_filetime_to_timestamp ( uint64_t filetime, std_timestamp_t* timestamp ) {
    FILETIME ft;
    ft.dwHighDateTime = filetime >> 32;
    ft.dwLowDateTime = filetime & 0xffffffff;
    SYSTEMTIME st;
    BOOL conversion_retcode = FileTimeToSystemTime ( &ft, &st );
    std_assert_m ( conversion_retcode == TRUE );
    std_calendar_time_t ct;
    ct.year = st.wYear;
    ct.month = ( uint8_t ) st.wMonth;
    ct.day = ( uint8_t ) st.wDay;
    ct.hour = ( uint8_t ) st.wHour;
    ct.minute = ( uint8_t ) st.wMinute;
    ct.second = ( uint8_t ) st.wSecond;
    ct.millisecond = st.wMilliseconds;
    *timestamp = std_calendar_to_timestamp ( ct );
}

#elif defined(std_platform_linux_m)

void fs_filetime_to_timestamp ( struct timespec ts, std_timestamp_t* timestamp ) {
    std_calendar_time_t ct;
    struct tm tm = *gmtime ( &ts.tv_sec );
    ct.year = ( uint64_t ) ( tm.tm_year + 1900 );
    ct.month = ( uint8_t ) ( tm.tm_mon + 1 );
    ct.day = ( uint8_t ) tm.tm_mday;
    ct.hour = ( uint8_t ) tm.tm_hour;
    ct.minute = ( uint8_t ) tm.tm_min;
    ct.second = ( uint8_t ) tm.tm_sec;
    ct.millisecond = ( uint16_t ) ( ts.tv_nsec / 1000000.0 );
    *timestamp = std_calendar_to_timestamp ( ct );
}

#endif
