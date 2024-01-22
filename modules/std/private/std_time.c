#include <std_time.h>

#include <std_log.h>
#include <std_platform.h>

#include "std_init.h"

#include <time.h>

//==============================================================================

static std_time_state_t* std_time_state;

void std_time_init ( std_time_state_t* state ) {
#if defined(std_platform_win32_m)
    LARGE_INTEGER li;
    QueryPerformanceFrequency ( &li );
    state->tick_freq_milli_f32 = ( float )  li.QuadPart / 1000.f;
    state->tick_freq_milli_f64 = ( double ) li.QuadPart / 1000.0;
    state->tick_freq_micro_f32 = ( float )  li.QuadPart / 1000000.f;
    state->tick_freq_micro_f64 = ( double ) li.QuadPart / 1000000.0;
#elif defined(std_platform_linux_m)
    // linux time is already in nanoseconds, just scale it
    state->tick_freq_milli_f32 = 1000.f * 1000.f;
    state->tick_freq_milli_f64 = 1000.0 * 1000.0;
    state->tick_freq_micro_f32 = 1000.f;
    state->tick_freq_micro_f64 = 1000.0;
#endif
    state->program_start_tick = std_tick_now();
    state->program_start_timestamp_utc = std_timestamp_now_utc();
    state->program_start_timestamp_local = std_timestamp_now_local();
}

void std_time_attach ( std_time_state_t* state ) {
    std_time_state = state;
}

//==============================================================================

static uint64_t std_time_leap_years_count ( uint32_t year ) {
    std_assert_m ( year > 0 );
    --year;
    return ( year / 4 ) - ( year / 100 ) + ( year / 400 );
}

static bool std_time_is_leap_year ( uint32_t year ) {
    return ( year % 4 == 0 && year % 100 != 0 ) || ( year % 400 == 0 );
}

//==============================================================================

std_calendar_time_t std_calendar_time_now_utc ( void ) {
    std_calendar_time_t calendar_time;

#if defined(std_platform_win32_m)
    SYSTEMTIME system_time;
    GetSystemTime ( &system_time );
    calendar_time.year = ( uint64_t ) system_time.wYear;
    calendar_time.month = ( uint8_t ) system_time.wMonth;
    calendar_time.day = ( uint8_t ) system_time.wDay;
    calendar_time.hour = ( uint8_t ) system_time.wHour;
    calendar_time.minute = ( uint8_t ) system_time.wMinute;
    calendar_time.second = ( uint8_t ) system_time.wSecond;
    calendar_time.millisecond = ( uint16_t ) system_time.wMilliseconds;
#elif defined(std_platform_linux_m)
    struct timespec ts;
    clock_gettime ( CLOCK_REALTIME, &ts );
    struct tm tm = *gmtime ( &ts.tv_sec );
    calendar_time.year = ( uint64_t ) ( tm.tm_year + 1900 );
    calendar_time.month = ( uint8_t ) ( tm.tm_mon + 1 );
    calendar_time.day = ( uint8_t ) tm.tm_mday;
    calendar_time.hour = ( uint8_t ) tm.tm_hour;
    calendar_time.minute = ( uint8_t ) tm.tm_min;
    calendar_time.second = ( uint8_t ) tm.tm_sec;
    calendar_time.millisecond = ( uint16_t ) ( ts.tv_nsec / 1000000.0 );
#endif
    return calendar_time;
}

std_calendar_time_t std_calendar_time_now_local ( void ) {
    std_calendar_time_t calendar_time;
#if defined(std_platform_win32_m)
    SYSTEMTIME system_time;
    GetLocalTime ( &system_time );
    calendar_time.year = ( uint64_t ) system_time.wYear;
    calendar_time.month = ( uint8_t ) system_time.wMonth;
    calendar_time.day = ( uint8_t ) system_time.wDay;
    calendar_time.hour = ( uint8_t ) system_time.wHour;
    calendar_time.minute = ( uint8_t ) system_time.wMinute;
    calendar_time.second = ( uint8_t ) system_time.wSecond;
    calendar_time.millisecond = ( uint16_t ) system_time.wMilliseconds;
#elif defined(std_platform_linux_m)
    struct timespec ts;
    clock_gettime ( CLOCK_REALTIME, &ts );
    struct tm tm = *localtime ( &ts.tv_sec );
    calendar_time.year = ( uint64_t ) ( tm.tm_year + 1900 );
    calendar_time.month = ( uint8_t ) ( tm.tm_mon + 1 );
    calendar_time.day = ( uint8_t ) tm.tm_mday;
    calendar_time.hour = ( uint8_t ) tm.tm_hour;
    calendar_time.minute = ( uint8_t ) tm.tm_min;
    calendar_time.second = ( uint8_t ) tm.tm_sec;
    calendar_time.millisecond = ( uint16_t ) ( ts.tv_nsec / 1000000.0 );
#endif
    return calendar_time;
}

std_timestamp_t std_timestamp_now_utc ( void ) {
    return std_calendar_to_timestamp ( std_calendar_time_now_utc() );
}

std_timestamp_t std_timestamp_now_local ( void ) {
    return std_calendar_to_timestamp ( std_calendar_time_now_local() );
}

std_tick_t std_tick_now ( void ) {
#if defined(std_platform_win32_m)
    LARGE_INTEGER t;
    QueryPerformanceCounter ( &t );
    return ( std_tick_t ) t.QuadPart;
#elif defined(std_platform_linux_m)
    struct timespec ts;
    clock_gettime ( CLOCK_MONOTONIC_RAW, &ts );
    return ( std_tick_t ) ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

//==============================================================================

#define std_time_ms_in_second_m (1000)
#define std_time_ms_in_minute_m (60 * std_time_ms_in_second_m)
#define std_time_ms_in_hour_m (60 * std_time_ms_in_minute_m)
#define std_time_ms_in_day_m (24 * std_time_ms_in_hour_m)

// TOOD
std_calendar_time_t std_timestamp_to_calendar ( std_timestamp_t time ) {
    std_calendar_time_t calendar_time;

    uint64_t days = time.count / std_time_ms_in_day_m;

    // year
    uint32_t year = 1;

    for ( ;; ++year ) {
        uint64_t days_in_year = 365 + std_time_is_leap_year ( year );

        if ( days < days_in_year ) {
            break;
        }

        days -= days_in_year;
    }

    calendar_time.year = year;

    // month
    uint32_t month = 0;
    uint32_t days_per_month[] = {31, 28 + std_time_is_leap_year ( year ), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    for ( ; month < 12; ++month ) {
        uint64_t days_in_month = days_per_month[month];

        if ( days < days_in_month ) {
            break;
        }

        days -= days_in_month;
    }

    month += 1;
    calendar_time.month = month;

    // day
    calendar_time.day = days + 1;

    // hours, minutes, seconds, millis
    calendar_time.millisecond = ( uint16_t ) ( time.count % 1000 );
    time.count /= 1000;
    calendar_time.second = ( uint8_t ) ( time.count % 60 );
    time.count /= 60;
    calendar_time.minute = ( uint8_t ) ( time.count % 60 );
    time.count /= 60;
    calendar_time.hour = ( uint8_t ) ( time.count % 24 );
    time.count /= 24;

    return calendar_time;
}

std_timestamp_t std_calendar_to_timestamp ( std_calendar_time_t calendar_time ) {
    std_timestamp_t timestamp;
    timestamp.count = 0;

    // convert to u64 to avoid possible overlows in the code below
    uint64_t millisecond = calendar_time.millisecond;
    uint64_t second = calendar_time.second;
    uint64_t minute = calendar_time.minute;
    uint64_t hour = calendar_time.hour;
    uint64_t day = calendar_time.day;

    timestamp.count += millisecond;
    timestamp.count += second * std_time_ms_in_second_m;
    timestamp.count += minute * std_time_ms_in_minute_m;
    timestamp.count += hour * std_time_ms_in_hour_m;
    timestamp.count += ( day - 1 ) * std_time_ms_in_day_m;
    timestamp.count += ( calendar_time.year - 1 ) * 365 * std_time_ms_in_day_m;
    timestamp.count += std_time_leap_years_count ( calendar_time.year ) * std_time_ms_in_day_m;

    uint64_t days_in_current_year = 0;
    uint32_t days_per_month[] = {31, 28 + std_time_is_leap_year ( calendar_time.year ), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    for ( size_t i = 0; i < calendar_time.month - 1; ++i ) {
        days_in_current_year += days_per_month[i];
    }

    timestamp.count += days_in_current_year * std_time_ms_in_day_m;

    return timestamp;
}

size_t std_calendar_to_string ( std_calendar_time_t time, char* string, size_t cap ) {
    size_t result = std_str_format ( string, cap, std_fmt_u64_m"/"std_fmt_u8_m"/"std_fmt_u8_m" "std_fmt_u8_m":"std_fmt_u8_m":"std_fmt_u8_m"."std_fmt_u16_m,
            time.year, time.month, time.day, time.hour, time.minute, time.second, time.millisecond );
    return result;
}

size_t std_timestamp_to_string ( std_timestamp_t timestamp, char* string, size_t cap ) {
    // Just do a two-step conversion
    std_calendar_time_t calendar_time = std_timestamp_to_calendar ( timestamp );
    return std_calendar_to_string ( calendar_time, string, cap );
}

float std_tick_to_milli_f32 ( std_tick_t tick ) {
    return tick / std_time_state->tick_freq_milli_f32;
}

double std_tick_to_milli_f64 ( std_tick_t tick ) {
    return tick / std_time_state->tick_freq_milli_f64;
}

float std_tick_to_micro_f32 ( std_tick_t tick ) {
    return tick / std_time_state->tick_freq_micro_f32;
}

double std_tick_to_micro_f64 ( std_tick_t tick ) {
    return tick / std_time_state->tick_freq_micro_f64;
}

std_tick_t std_program_start_tick ( void ) {
    return std_time_state->program_start_tick;
}

std_timestamp_t std_program_start_timestamp_utc ( void ) {
    return std_time_state->program_start_timestamp_utc;
}

std_timestamp_t std_program_start_timestamp_local ( void ) {
    return std_time_state->program_start_timestamp_local;
}
