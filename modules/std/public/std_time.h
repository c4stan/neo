#pragma once

#include <std_platform.h>

typedef struct {
    uint64_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
} std_calendar_time_t;

typedef struct {
    uint64_t count; // millisecond count from time 0
} std_timestamp_t;

#define std_timestamp_zero_m ( std_timestamp_t ) { .count = 0 }

// Hardware tick counter.
// Doesn't match CPU clock count but also doesn't have to match any real time unit.
// Only guarantee is that it's accurate enough to measure microseconds.
typedef uint64_t std_tick_t;

std_timestamp_t std_timestamp_now_utc ( void );
std_timestamp_t std_timestamp_now_local ( void );

std_calendar_time_t std_calendar_time_now_utc ( void );
std_calendar_time_t std_calendar_time_now_local ( void );

std_tick_t std_tick_now ( void );

std_timestamp_t std_calendar_to_timestamp ( std_calendar_time_t calendar_time );
std_calendar_time_t std_timestamp_to_calendar ( std_timestamp_t timestamp );

size_t std_calendar_to_string  ( std_calendar_time_t calendar_time, char* string, size_t cap );
size_t std_timestamp_to_string ( std_timestamp_t timestamp, char* string, size_t cap );

float   std_tick_to_milli_f32 ( std_tick_t tick );
double  std_tick_to_milli_f64 ( std_tick_t tick );
float   std_tick_to_micro_f32 ( std_tick_t tick );
double  std_tick_to_micro_f64 ( std_tick_t tick );

std_tick_t std_program_start_tick ( void );
std_timestamp_t std_program_start_timestamp_utc ( void );
std_timestamp_t std_program_start_timestamp_local ( void );
