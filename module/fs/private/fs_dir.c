#include "fs_dir.h"

#include "fs_shared.h"

#include <std_string.h>
#include <std_log.h>

#if defined std_platform_win32_m
static bool fs_dir_create_recursive_from_path_buffer ( void ) {
    WCHAR* p = t_path_buffer;

    while ( *p ) {
        if ( *p == '\\' || *p == '/' ) {
            *p = '\0';
            BOOL create_retcode = CreateDirectoryW ( t_path_buffer, NULL );

            if ( create_retcode == FALSE ) {
                if ( GetLastError() != ERROR_ALREADY_EXISTS ) {
                    std_log_os_error_m();
                    return false;   
                }
            }

            *p = '/';
        }

        ++p;
    }

    BOOL create_retcode = CreateDirectoryW ( t_path_buffer, NULL );

    if ( create_retcode == FALSE ) {
        if ( GetLastError() != ERROR_ALREADY_EXISTS ) {
            std_log_os_error_m();
            return false;   
        }
    }

    return true;
}
#endif

bool fs_dir_create ( const char* path ) {
    std_assert_m ( path != NULL );

#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < fs_path_size_m );

    BOOL create_retcode = CreateDirectoryW ( t_path_buffer, NULL );

    if ( create_retcode == FALSE ) {
        if ( GetLastError() == ERROR_PATH_NOT_FOUND ) {
            // CreateDirectory fails if any intermediate dir level in the path is missing.
            // In that case we yry to recursively create the entire path.
            if ( !fs_dir_create_recursive_from_path_buffer() ) {
                return false;
            }
        } else {
            if ( GetLastError() != ERROR_ALREADY_EXISTS ) {
                std_log_os_error_m();                
            }
            return false;   
        }
    }

    return true;
#elif defined(std_platform_linux_m)
    int result = mkdir ( path, 0700 );
    return result == 0;
#endif
}

#if defined(std_platform_linux_m)
static int fs_nftw_remove_callback ( const char* path, const struct stat* stat, int typeflag, struct FTW* ftw ) {
    int result = remove ( path );
    std_assert_m ( result == 0 );
    return result;
}
#endif

bool fs_dir_delete ( const char* path ) {
    std_verify_m ( path != NULL );
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < fs_path_size_m );
    BOOL remove_retcode = RemoveDirectoryW ( t_path_buffer );

    if ( remove_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    // rmdir() can only delete an empty folder
    return nftw ( path, fs_nftw_remove_callback, 64, FTW_DEPTH | FTW_PHYS );
#endif
}

// TODO check that dir_copy and dir_move work on win32
bool fs_dir_copy ( const char* path, const char* dest, fs_already_existing_e already_existing ) {
#if 0
    std_assert_m ( path != NULL );
    std_assert_m ( dest != NULL );
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    len = fs_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    BOOL copy_retcode = CopyFileW ( t_path_buffer, t_path_buffer_2, already_existing != fs_already_existing_overwrite_m );

    if ( copy_retcode == FALSE ) {
        return false;
    }

    return true;
#endif
    std_unused_m ( path );
    std_unused_m ( dest );
    std_unused_m ( already_existing );
    std_not_implemented_m();
    return false;
}

bool fs_dir_move ( const char* path, const char* dest, fs_already_existing_e already_existing ) {
#if 0
    std_assert_m ( path != NULL );
    std_assert_m ( dest != NULL );
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    len = fs_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    DWORD flags = 0;

    if ( already_existing == fs_already_existing_overwrite_m ) {
        flags |= MOVEFILE_WRITE_THROUGH;
    }

    BOOL move_retcode = MoveFileExW ( t_path_buffer, t_path_buffer_2, flags );

    if ( move_retcode == FALSE ) {
        return false;
    }

    return true;
#endif
    std_unused_m ( path );
    std_unused_m ( dest );
    std_unused_m ( already_existing );
    std_not_implemented_m();
    return false;
}

size_t fs_dir_iterate ( const char* path, fs_iterator_callback_f callback, void* arg ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    size_t len = fs_to_path_buffer ( path );
    t_path_buffer[len++ - 1] = '*';
    t_path_buffer[len++ - 1] = '\0';
    std_assert_m ( len > 0 && len < fs_path_size_m );
    WIN32_FIND_DATAW fd;
    HANDLE find_handle = FindFirstFileW ( t_path_buffer, &fd );
    std_assert_m ( find_handle != INVALID_HANDLE_VALUE );
    size_t count = 0;

    do {
        if ( fd.cFileName[0] == '.' ) {
            continue;
        }

        std_ignore_warning_m ( fs_path_flags_t flags = 0, "-Wassign-enum" )

        if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
            flags |= fs_path_is_dir_m;
        } else {
            flags |= fs_path_is_file_m;
        }

        len = fs_path_to_str ( fd.cFileName, t_char_buffer, fs_path_size_m );
        std_assert_m ( len > 0 && len < fs_path_size_m );
        callback ( t_char_buffer, flags, arg );
        ++count;
    } while ( FindNextFileW ( find_handle, &fd ) );

    FindClose ( find_handle );
    return count;

#elif defined(std_platform_linux_m)

    DIR* dir = opendir ( path );
    std_assert_m ( dir );
    size_t count = 0;

    struct dirent* item = readdir ( dir );

    while ( item ) {
        std_ignore_warning_m ( fs_path_flags_t flags = 0, " -Wassign-enum" )

        if ( item->d_type == DT_DIR ) {
            flags |= fs_path_is_dir_m;
        } else if ( item->d_type == DT_REG ) {
            flags |= fs_path_is_file_m;
        }

        callback ( item->d_name, flags, arg );
        ++count;
        item = readdir ( dir );
    }

    closedir ( dir );

    return count;
#endif
}

size_t fs_dir_get_files ( char** files, size_t files_cap, size_t file_cap, const char* path ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    std_assert_m ( files != NULL );
    size_t len = fs_to_path_buffer ( path );
    t_path_buffer[len++ - 1] = '*';
    t_path_buffer[len++ - 1] = '\0';
    std_assert_m ( len > 0 && len < fs_path_size_m );
    WIN32_FIND_DATAW item;
    HANDLE find_handle = FindFirstFileW ( t_path_buffer, &item );
    std_assert_m ( find_handle != INVALID_HANDLE_VALUE );
    size_t i = 0;

    for ( ;; ) {
        BOOL next_retcode;

        if ( item.cFileName[0] == '.' ) {
            goto next;
        }

        if ( item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
            goto next;
        }

        if ( i < files_cap ) {
            fs_path_to_str ( item.cFileName, files[i], file_cap );
            ++i;
        }

next:
        next_retcode = FindNextFileW ( find_handle, &item );

        if ( next_retcode == FALSE ) {
            break;
        }
    }

    FindClose ( find_handle );
    return i;
#elif defined(std_platform_linux_m)

    DIR* dir = opendir ( path );
    std_assert_m ( dir );
    size_t i = 0;

    struct dirent* item = readdir ( dir );

    while ( item ) {
        if ( item->d_type == DT_REG && i < files_cap ) {
            std_str_copy ( files[i++], file_cap, item->d_name );
        }

        item = readdir ( dir );
    }

    closedir ( dir );

    return i;

#endif
}

size_t fs_dir_get_subdirs ( char** subdirs, size_t subdirs_cap, size_t subdir_cap, const char* path ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    std_assert_m ( subdirs != NULL );
    size_t len = fs_to_path_buffer ( path );
    t_path_buffer[len++ - 1] = '*';
    t_path_buffer[len++ - 1] = '\0';
    std_assert_m ( len > 0 && len < fs_path_size_m );
    WIN32_FIND_DATAW item;
    HANDLE find_handle = FindFirstFileW ( t_path_buffer, &item );
    std_assert_m ( find_handle != INVALID_HANDLE_VALUE );
    size_t i = 0;

    for ( ;; ) {
        BOOL next_retcode;

        if ( wcscmp ( item.cFileName, L"." ) == 0 || wcscmp ( item.cFileName, L".." ) == 0 ) {
            goto next;
        }

        if ( ! ( item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
            goto next;
        }

        if ( i < subdirs_cap ) {
            fs_path_to_str ( item.cFileName, subdirs[i], subdir_cap );
            ++i;
        }

next:
        next_retcode = FindNextFileW ( find_handle, &item );

        if ( next_retcode == FALSE ) {
            break;
        }
    }

    FindClose ( find_handle );
    return i;
#elif defined(std_platform_linux_m)
    DIR* dir = opendir ( path );
    std_assert_m ( dir );
    size_t i = 0;

    struct dirent* item = readdir ( dir );

    while ( item ) {
        if ( std_str_cmp ( item->d_name, "." ) == 0 || std_str_cmp ( item->d_name, ".." ) == 0 ) {
            goto next;
        }

        if ( item->d_type == DT_DIR && i < subdirs_cap ) {
            std_str_copy ( subdirs[i++], subdir_cap, item->d_name );
        }

next:
        item = readdir ( dir );
    }

    closedir ( dir );
    return i;
#endif
}

bool fs_dir_get_info ( fs_dir_info_t* info, const char* path ) {
    std_assert_m ( path != NULL );
    std_assert_m ( info != NULL );
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL get_retcode = GetFileAttributesExW ( t_path_buffer, GetFileExInfoStandard, &data );

    if ( get_retcode == FALSE ) {
        return false;
    }

    uint64_t creation_time = ( uint64_t ) data.ftCreationTime.dwHighDateTime << 32 | data.ftCreationTime.dwLowDateTime;
    uint64_t last_access_time = ( uint64_t ) data.ftLastAccessTime.dwHighDateTime << 32 | data.ftLastAccessTime.dwLowDateTime;
    uint64_t last_write_time = ( uint64_t ) data.ftLastWriteTime.dwHighDateTime << 32 | data.ftLastWriteTime.dwLowDateTime;
    fs_filetime_to_timestamp ( creation_time, &info->creation_time );
    fs_filetime_to_timestamp ( last_access_time, &info->last_access_time );
    fs_filetime_to_timestamp ( last_write_time, &info->last_write_time );

    std_ignore_warning_m ( info->flags = 0, "-Wassign-enum" )

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ) {
        info->flags |= fs_dir_compressed_m;
    }

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ) {
        info->flags |= fs_dir_encrypted_m;
    }

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ) {
        info->flags |= fs_dir_hidden_m;
    }

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ) {
        info->flags |= fs_dir_os_use_m;
    }

    return true;
#elif defined(std_platform_linux_m)
    struct stat data;
    int result = stat ( path, &data );
    std_assert_m ( result == 0 );

    info->creation_time.count = 0;
    fs_filetime_to_timestamp ( data.st_atim, &info->last_access_time );
    fs_filetime_to_timestamp ( data.st_mtim, &info->last_write_time );

    std_ignore_warning_m ( info->flags = 0, "-Wassign-enum" )

    return true;
#endif
}
