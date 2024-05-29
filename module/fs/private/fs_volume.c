#include "fs_volume.h"

#include "fs_shared.h"

#include <std_string.h>
#include <std_log.h>

fs_list_h fs_volume_get_first ( fs_volume_h* volume ) {
#if defined(std_platform_win32_m)
    std_assert_m ( volume != NULL );
    HANDLE find_handle = FindFirstVolumeW ( t_path_buffer, fs_path_size_m );
    std_assert_m ( find_handle != INVALID_HANDLE_VALUE );
    size_t len = fs_from_path_buffer ( volume->guid, fs_volume_guid_size_m );
    std_verify_m ( len > 0 && len < fs_path_size_m );
    return ( fs_list_h ) find_handle;
#elif defined(std_platform_linux_m)
    return 0;
#endif
}

bool fs_volume_get_next ( fs_list_h* list, fs_volume_h* volume ) {
#if defined(std_platform_win32_m)
    std_assert_m ( list != NULL );
    BOOL find_retcode = FindNextVolumeW ( ( HANDLE ) * list, t_path_buffer, fs_path_size_m );

    if ( find_retcode == FALSE ) {
        return false;
    }

    std_assert_m ( volume != NULL );
    size_t len = fs_from_path_buffer ( volume->guid, fs_volume_guid_size_m );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    return true;
#else
    return false;
#endif
}

void fs_volume_close_list ( fs_list_h list ) {
#if defined(std_platform_win32_m)
    BOOL close_retval = FindVolumeClose ( ( HANDLE ) list );
    std_assert_m ( close_retval == TRUE );
#elif defined(std_platform_linux_m)
#endif
}

size_t fs_volume_get_count ( void ) {
#if defined(std_platform_win32_m)
    HANDLE find_handle = FindFirstVolumeW ( t_path_buffer, fs_path_size_m );
    std_assert_m ( find_handle != INVALID_HANDLE_VALUE );
    size_t count = 1;

    while ( FindNextVolumeW ( find_handle, t_path_buffer, fs_path_size_m ) == TRUE ) {
        ++count;
    }

    FindVolumeClose ( find_handle );
    return count;
#elif defined(std_platform_linux_m)
    return 0;
#endif
}

size_t fs_volume_get ( fs_volume_h* volumes, size_t cap ) {
#if defined(std_platform_win32_m)

    if ( cap == 0 ) {
        return 0;
    }

    fs_list_h list = fs_volume_get_first ( &volumes[0] );
    size_t count = 1;

    while ( count < cap && fs_volume_get_next ( &list, &volumes[count] ) ) {
        ++count;
    }

    fs_volume_close_list ( list );
    return count;
#elif defined(std_platform_linux_m)
    return 0;
#endif
}

size_t fs_volume_get_path ( char* path, size_t cap, const fs_volume_h* volume ) {
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( volume->guid );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    DWORD total_len;
    BOOL get_retcode = GetVolumePathNamesForVolumeNameW ( t_path_buffer, t_path_buffer_2, fs_path_size_m, &total_len );
    // GetVolumePathNamesForVolumeNameW returns the list of all mounted folders to the given volume, e.g.
    // [C:/\0C:/mount\0D:/mount\0\0]
    // At the moment the code assumes that the first dir in the list is the root, which (probably?) means, it assumes no mounted folders.
    // The entire list is printed to the path buffer, the chance of an overflow is ignored as the content of the list is ignored except for the first element.
    // As the elements are separated by \0, we can just print the list to the path buffer, the user will ignore what's after the terminator anyway.
    std_assert_m ( get_retcode == TRUE );
    len = fs_from_path_buffer_2 ( path, cap );
    std_assert_m ( len > 0 );
    return len;
#elif defined(std_platform_linux_m)
    return 0;
#endif
}

bool fs_volume_get_info ( fs_volume_info_t* info, const fs_volume_h* volume ) {
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( volume->guid );
    std_assert_m ( len > 0 && len < fs_path_size_m );

    WCHAR name[64] = { 0 };
    DWORD serial_number = 0;
    DWORD max_path_piece_len = 0;
    DWORD flags = 0;
    WCHAR fs[64] = { 0 };

    {
        BOOL result = GetVolumeInformationW ( t_path_buffer, name, 64, &serial_number, &max_path_piece_len, &flags, fs, 64 );

        if ( result == FALSE ) {
            return false;
        }
    }

    uint64_t total_size;
    uint64_t free_size;
    uint64_t available_free_size;

    {
        BOOL result = GetDiskFreeSpaceExW ( t_path_buffer, ( PULARGE_INTEGER ) &available_free_size, ( PULARGE_INTEGER ) &total_size, ( PULARGE_INTEGER ) &free_size );

        if ( result == FALSE ) {
            return false;
        }
    }


    info->total_size = total_size;
    info->free_size = free_size;
    info->available_free_size = available_free_size;
    info->serial_number = serial_number;

    len = fs_path_to_str ( name, info->name, fs_volume_name_size_m );
    std_assert_m ( len > 0 && len < fs_volume_name_size_m );

    len = fs_path_to_str ( fs, info->file_system, fs_volume_filesystem_name_size_m );
    std_assert_m ( len > 0 && len < fs_volume_filesystem_name_size_m );

    info->flags = 0;

    if ( flags & FILE_FILE_COMPRESSION ) {
        info->flags |= fs_volume_supports_compression_m;
    }

    if ( flags & FILE_READ_ONLY_VOLUME ) {
        info->flags |= fs_volume_read_only_m;
    }

    if ( flags & FILE_SUPPORTS_ENCRYPTION ) {
        info->flags |= fs_volume_supports_encryption_m;
    }

    if ( flags & FILE_VOLUME_IS_COMPRESSED ) {
        info->flags |= fs_volume_is_compressed_m;
    }

    return true;
#elif defined(std_platform_linux_m)
    return false;
#endif
}
