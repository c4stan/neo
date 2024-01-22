#include "fs_executable.h"

#include "fs_shared.h"
#include "fs_path.h"

#include <std_log.h>
#include <std_string.h>

/*
size_t fs_executable_get_name ( char* name, size_t cap ) {
#if defined(std_platform_win32_m)
    fs_executable_get_path ( t_char_buffer, fs_path_size_m );
    return fs_path_get_name ( t_char_buffer, name, cap );
#elif defined(std_platform_linux_m)
    char buffer[fs_path_size_m];
    fs_executable_get_path ( buffer, fs_path_size_m );
    return fs_path_get_name ( buffer, name, cap );
#endif
}

size_t fs_executable_get_path ( char* path, size_t cap ) {
#if defined(std_platform_win32_m)
    DWORD get_retcode = GetModuleFileNameW ( NULL, t_path_buffer, fs_path_size_m );
    std_assert_m ( get_retcode > 0 && get_retcode < fs_path_size_m );
    char buffer[fs_path_size_m];
    fs_from_path_buffer ( buffer, fs_path_size_m );
    return fs_path_normalize ( buffer, path, cap );
#elif defined(std_platform_linux_m)
    char buffer[fs_path_size_m];
    ssize_t len = readlink ( "/proc/self/exe", buffer, fs_path_size_m );
    std_assert_m ( len != -1 );
    return fs_path_normalize ( buffer, path, cap );
#endif
}
*/
