#pragma once

#include <std_platform.h>
#include <std_compiler.h>
#include <std_time.h>

#if defined(std_platform_win32_m)

extern std_thread_local_m char  t_char_buffer[fs_path_size_m];
extern std_thread_local_m WCHAR t_path_buffer[fs_path_size_m];
extern std_thread_local_m WCHAR t_path_buffer_2[fs_path_size_m];

size_t  fs_path_to_str ( const WCHAR* path, char* string, size_t cap );
size_t  fs_str_to_path ( const char* string, WCHAR* path, size_t cap );

// Fills a path buffer, converting from UTF8 to the preferred OS encording (UTF16 fow Win32). The buffer is to be assumed to be big enough to contain any valid path.
// Returns the lenght of the converted string.
size_t  fs_to_path_buffer ( const char* str );
size_t  fs_to_path_buffer_2 ( const char* str );

// Gets back a path from the path buffer, converting it from the preferred OS encoding to UTF8.
// The size returned is the one required to contain the converted path into the given buffer.
size_t  fs_from_path_buffer ( char* str, size_t cap );
size_t  fs_from_path_buffer_2 ( char* str, size_t cap );

void fs_filetime_to_timestamp ( uint64_t filetime, std_timestamp_t* timestamp );

#elif defined(std_platform_linux_m)

#include <sys/stat.h>
#include <dirent.h>
#include <ftw.h>
#include <limits.h>
#include <errno.h> // TODO move to std_platform?

void fs_filetime_to_timestamp ( struct timespec ts, std_timestamp_t* timestamp );

#endif
