#include <std_file.h>

#include <std_log.h>

// ======================================================================================= //
//                                       S H A R E D
// ======================================================================================= //

#if defined(std_platform_win32_m)

static std_thread_local_m char  t_char_buffer[std_path_size_m];
static std_thread_local_m WCHAR t_path_buffer[std_path_size_m];
static std_thread_local_m WCHAR t_path_buffer_2[std_path_size_m];

size_t std_path_to_str ( const WCHAR* path, char* string, size_t cap );
size_t std_str_to_path ( const char* string, WCHAR* path, size_t cap );

// Fills a path buffer, converting from UTF8 to the preferred OS encording (UTF16 fow Win32). The buffer is to be assumed to be big enough to contain any valid path.
// Returns the lenght of the converted string.
size_t std_to_path_buffer ( const char* str );
size_t std_to_path_buffer_2 ( const char* str );

// Gets back a path from the path buffer, converting it from the preferred OS encoding to UTF8.
// The size returned is the one required to contain the converted path into the given buffer.
size_t std_from_path_buffer ( char* str, size_t cap );
size_t std_from_path_buffer_2 ( char* str, size_t cap );

void std_filetime_to_timestamp ( uint64_t filetime, std_timestamp_t* timestamp );

#elif defined(std_platform_linux_m)

#include <sys/stat.h>
#include <dirent.h>
#include <ftw.h>
#include <limits.h>

void std_filetime_to_timestamp ( struct timespec ts, std_timestamp_t* timestamp );

#endif

// impl

#if defined(std_platform_win32_m)

size_t std_path_to_str ( const WCHAR* path, char* str, size_t cap ) {
    std_assert_m ( cap <= INT_MAX );
    int size = WideCharToMultiByte ( CP_UTF8, 0, path, -1, str, ( int ) cap, NULL, NULL );

#if std_log_assert_enabled_m

    if ( size == 0 ) {
        DWORD error = GetLastError();
        std_log_warn_m ( "Conversion from wide string to unicode failed, possible error code: " std_fmt_int_m, error );
    }

#endif

    return ( size_t ) size;
}

size_t std_str_to_path ( const char* str, WCHAR* path, size_t cap ) {
    std_assert_m ( cap <= INT_MAX );
    int size = MultiByteToWideChar ( CP_UTF8, 0, str, -1, path, ( int ) cap );
    return ( size_t ) size;
}

size_t std_to_path_buffer ( const char* str ) {
    return std_str_to_path ( str, t_path_buffer, std_path_size_m );
    // TODO error check
}

size_t std_from_path_buffer ( char* str, size_t cap ) {
    return std_path_to_str ( t_path_buffer, str, cap );
    // TODO doublecheck that params are correct
    // TODO error check
}

size_t std_to_path_buffer_2 ( const char* str ) {
    return std_str_to_path ( str, t_path_buffer_2, std_path_size_m );
    // TODO error check
}

size_t std_from_path_buffer_2 ( char* str, size_t cap ) {
    return std_path_to_str ( t_path_buffer_2, str, cap );
    // TODO doublecheck that params are correct
    // TODO error check
}

void std_filetime_to_timestamp ( uint64_t filetime, std_timestamp_t* timestamp ) {
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

void std_filetime_to_timestamp ( struct timespec ts, std_timestamp_t* timestamp ) {
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

// ======================================================================================= //
//                                         P A T H
// ======================================================================================= //

static const char* std_path_next_token ( const char* path, const char* cursor ) {
    std_assert_m ( path != NULL );
    std_assert_m ( cursor != NULL );

    if ( *cursor == '\0' ) {
        return NULL;
    }

    while ( *cursor != '/' && *cursor != '\\' && *cursor != '\0' ) {
        ++cursor;
    }

    if ( *cursor == '\0' ) {
        return NULL;
    }

    if ( * ( ++cursor ) == '\0' ) {
        return NULL;
    }

    return cursor;
}

static size_t std_path_token_len ( const char* path, const char* cursor ) {
    std_assert_m ( path != NULL );
    std_assert_m ( cursor != NULL );

    if ( cursor == NULL ) {
        return 0;
    }

    while ( cursor > path && *cursor != '/' && *cursor != '\\' ) {
        --cursor;
    }

    if ( *cursor == '/' || *cursor == '\\' ) {
        ++cursor;
    }

    const char* start = cursor;

    while ( *cursor != '/' && *cursor != '\\' && *cursor != '\0' ) {
        ++cursor;
    }

    return ( size_t ) ( cursor - start );
}

// appends to path first n characters from append, adds a / if necessary
static size_t std_path_append_n ( char* path, size_t cap, const char* append, size_t n ) {
    std_assert_m ( path != NULL );
    std_assert_m ( append != NULL );
    size_t path_len = std_str_len ( path );
    size_t extra = 0;

    if ( path_len > 0 && path[path_len - 1] != '/' && append[0] != '/' && path_len + 1 < cap ) {
        path[path_len++] = '/';
        path[path_len] = '\0';
        extra = 1;
    }

    size_t append_len = n;
    size_t new_path_len = path_len + append_len + extra;
    std_assert_m ( new_path_len < cap );
    std_mem_copy ( path + path_len, append, append_len );
    path[new_path_len] = '\0';
    return new_path_len;
}

size_t std_path_append ( char* path, size_t cap, const char* append ) {
    // compute append length
    // call append_n
    std_assert_m ( path != NULL );
    std_assert_m ( append != NULL );
    size_t append_len = std_str_len ( append );
    return std_path_append_n ( path, cap, append, append_len );
}

size_t std_path_pop ( char* path ) {
    // a path component is at least 1 char, so if there only one (or less) chars in the string return empty
    // iterate back starting from skipping the last utf8, as it's either a filename's last character component or a path's '/' end character.
    // insert a string terminator at the first '/', or after the first ':', and return
    // return empty if no '/' or ':' was found
    std_assert_m ( path != NULL );
    size_t len = std_str_len ( path );

    if ( len <= 1 ) {
        path[0] = '\0';
        return 0;
    }

    size_t i = len - 1;
    size_t char_size = std_utf8_char_size_reverse ( path + i, i );
    i -= char_size;

    for ( ;; ) {
        if ( path[i] == ':' ) {
            path[i + 1] = '/';
            path[i + 2] = '\0';
            return i + 2;
        }

        if ( path[i] == '/' ) {
            path[i + 1] = '\0';
            return i + 1;
        }

        char_size = std_utf8_char_size_reverse ( path + i, i );

        if ( char_size >= i ) {
            break;
        }

        i -= char_size;
    }

    path[0] = '\0';
    return 0;
}

size_t std_path_normalize ( char* dest, size_t cap, const char* path ) {
    if ( cap == 0 ) {
        return 0;
    }

    size_t len = 0;
    dest[0] = 0;
    const char* token = path;

    // On linux preserve initial '/'
    // TODO does win32 require a branch to ignore it?
#if defined(std_platform_linux_m)

    if ( *token == '/' ) {
        len = std_path_append_n ( dest, cap, "/", 1 );
        ++token;
    }

#endif

    do {
        if ( std_mem_cmp ( token, "./", 2 ) ) {
            // Skip
        } else if ( std_mem_cmp ( token, "../", 3 ) ) {
            std_path_pop ( dest );
        } else {
            size_t token_len = std_path_token_len ( path, token );
            len = std_path_append_n ( dest, cap, token, token_len );

            if ( * ( token + token_len ) != '\0' ) {
                len = std_path_append ( dest, cap, "/" );
            }
        }

        token = std_path_next_token ( path, token );
    } while ( token != NULL );

    return len;
}

bool std_path_is_drive ( const char* path ) {
#if defined(std_platform_win32_m)
    return std_str_len ( path ) == 3 && path[0] >= 65 && path[0] <= 90 && path[1] == ':' && path[2] == '/';
#elif defined(std_platform_linux_m)
    // compare path device id with parent device id, if differs then path is mount point
    char parent[std_path_size_m];
    std_str_copy ( parent, std_path_size_m, path );
    size_t parent_len = std_path_pop ( parent );

    if ( parent_len == 0 ) {
        return true;
    }

    struct stat path_stat;
    int path_stat_result = stat ( path, &path_stat );
    struct stat parent_stat;
    int parent_stat_result = stat ( path, &parent_stat );
    std_assert_m ( path_stat_result == 0 && parent_stat_result == 0 );

    if ( path_stat.st_dev != parent_stat.st_dev ) {
        return true;
    }

    return false;
#endif
}

size_t std_path_name ( char* name, size_t cap, const char* path ) {
    std_assert_m ( path != NULL );
    size_t len = std_str_len ( path );
    std_assert_m ( len > 0 );
    size_t i = std_str_find_reverse ( path, len, "/" );

    if ( i == std_str_find_null_m ) {
        return 0;
    }

    size_t name_len = std_str_len ( path + i ) - 1;

    if ( name_len < cap ) {
        std_mem_copy ( name, path + i + 1, name_len );
        name[name_len] = '\0';
    }

    return name_len;
}

const char* std_path_name_ptr ( const char* path ) {
    std_assert_m ( path != NULL );
    size_t len = std_str_len ( path );
    std_assert_m ( len > 0 );
    size_t i = std_str_find_reverse ( path, len, "/" );

    if ( i == std_str_find_null_m ) {
        return path;
    }

    return path + i + 1;
}

bool std_path_info ( std_path_info_t* info, const char* path ) {
#if defined(std_platform_win32_m)
    size_t len = std_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < std_path_size_m );
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL get_retcode = GetFileAttributesExW ( t_path_buffer, GetFileExInfoStandard, &data );

    if ( get_retcode == FALSE ) {
        return false;
    }

    if ( info ) {
        uint64_t creation_time = ( uint64_t ) data.ftCreationTime.dwHighDateTime << 32 | data.ftCreationTime.dwLowDateTime;
        std_filetime_to_timestamp ( creation_time, &info->creation_time );
        info->flags = 0;

        if ( data.dwFileAttributes & INVALID_FILE_ATTRIBUTES ) {
            info->flags |= std_path_non_existent_m;
        } else if ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
            info->flags |= std_path_is_directory_m;
        } else {
            info->flags |= std_path_is_file_m;
        }
    }

    return true;
#elif defined(std_platform_linux_m)
    struct stat stat_info;
    int stat_result = stat ( path, &stat_info );

    if ( stat_result == -1 ) {
        return false;
    }

    if ( info ) {
        info->creation_time.count = 0;
        info->flags = 0;

        if ( stat_result == -1 && errno == ENOENT ) {
            info->flags |= std_path_non_existent_m;
        } else if ( S_ISREG ( stat_info.st_mode ) ) {
            info->flags |= std_path_is_file_m;
        } else if ( S_ISDIR ( stat_info.st_mode ) ) {
            info->flags |= std_path_is_directory_m;
        }
    }

    return true;
#endif
}

size_t std_path_absolute ( char* dest, size_t dest_cap, const char* path ) {
#if defined(std_platform_win32_m)
    size_t len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    GetFullPathNameW ( t_path_buffer, std_path_size_m, t_path_buffer_2, NULL );
    char buffer[std_path_size_m];
    size_t full_len = std_from_path_buffer_2 ( buffer, std_path_size_m );
    std_assert_m ( full_len > 0 && full_len < std_path_size_m );
    return std_path_normalize ( dest, dest_cap, buffer );
#elif defined(std_platform_linux_m)
    char buffer[PATH_MAX];
    char* result = realpath ( path, buffer );

    if ( result == NULL ) {
        return 0;
    }

    return std_path_normalize ( dest, dest_cap, buffer );
#endif
}

// ======================================================================================= //
//                                    D I R E C T O R Y
// ======================================================================================= //

#if defined std_platform_win32_m
static bool std_dir_create_recursive_from_path_buffer ( void ) {
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

std_unused_static_m()
static bool std_dir_create_recursive ( const char* path ) {
#if defined std_platform_win32_m
    size_t len = std_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < std_path_size_m );
    return std_dir_create_recursive_from_path_buffer();
#elif defined std_platform_linux_m
    char buffer[PATH_MAX];
    std_str_copy ( buffer, PATH_MAX, path );

    char* p = buffer;
    while ( *p ) {
        if ( *p == '\\' || *p == '/' ) {
            *p = '\0';
            int create_retcode = mkdir ( buffer, 0700 );
            if ( create_retcode != 0 ) {
                if ( errno != EEXIST ) {
                    std_log_os_error_m();
                    return false;
                }
            }
            *p = '/';
        }
        ++p;
    }

    int create_retcode = mkdir ( buffer, 0700 );
    if ( create_retcode != 0 ) {
        if ( errno != EEXIST ) {
            std_log_os_error_m();
            return false;
        }
    }

    return true;
#endif
}


bool std_directory_create ( const char* path ) {
    std_assert_m ( path != NULL );

#if defined(std_platform_win32_m)
    size_t len = std_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < std_path_size_m );

    BOOL create_retcode = CreateDirectoryW ( t_path_buffer, NULL );

    if ( create_retcode == FALSE ) {
        if ( GetLastError() == ERROR_PATH_NOT_FOUND ) {
            // CreateDirectory fails if any intermediate dir level in the path is missing.
            // In that case try to recursively create the entire path.
            if ( !std_dir_create_recursive_from_path_buffer() ) {
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
    if ( result != 0 ) {
        if ( errno == ENOENT ) {
            if ( !std_dir_create_recursive ( path ) ) {
                return false;
            }
        } else {
            if ( errno != EEXIST ) {
                std_log_os_error_m();
            }
            return false;
        }
    }
    return result == 0;
#endif
}

#if defined(std_platform_linux_m)
static int std_nftw_remove_callback ( const char* path, const struct stat* stat, int typeflag, struct FTW* ftw ) {
    int result = remove ( path );
    std_assert_m ( result == 0 );
    return result;
}
#endif

bool std_directory_destroy ( const char* path ) {
    std_verify_m ( path != NULL );
#if defined(std_platform_win32_m)
    size_t len = std_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < std_path_size_m );
    BOOL remove_retcode = RemoveDirectoryW ( t_path_buffer );

    if ( remove_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    // rmdir() can only delete an empty folder
    return nftw ( path, std_nftw_remove_callback, 64, FTW_DEPTH | FTW_PHYS );
#endif
}

// TODO check that dir_copy and dir_move work on win32
bool std_directory_copy ( const char* path, const char* dest, std_path_already_existing_e already_existing ) {
#if 0
    std_assert_m ( path != NULL );
    std_assert_m ( dest != NULL );
    size_t len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    len = std_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < std_path_size_m );
    BOOL copy_retcode = CopyFileW ( t_path_buffer, t_path_buffer_2, already_existing != std_path_already_existing_overwrite_m );

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

bool std_directory_move ( const char* path, const char* dest, std_path_already_existing_e already_existing ) {
#if 0
    std_assert_m ( path != NULL );
    std_assert_m ( dest != NULL );
    size_t len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    len = std_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < std_path_size_m );
    DWORD flags = 0;

    if ( already_existing == std_path_already_existing_overwrite_m ) {
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

size_t std_directory_iterate ( const char* path, std_directory_iterator_callback_f callback, void* arg ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    size_t len = std_to_path_buffer ( path );
    t_path_buffer[len++ - 1] = '*';
    t_path_buffer[len++ - 1] = '\0';
    std_assert_m ( len > 0 && len < std_path_size_m );
    WIN32_FIND_DATAW fd;
    HANDLE find_handle = FindFirstFileW ( t_path_buffer, &fd );
    std_assert_m ( find_handle != INVALID_HANDLE_VALUE );
    size_t count = 0;

    do {
        if ( fd.cFileName[0] == '.' ) {
            continue;
        }

        std_path_flags_t flags = 0;

        if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
            flags |= std_path_is_directory_m;
        } else {
            flags |= std_path_is_file_m;
        }

        len = std_path_to_str ( fd.cFileName, t_char_buffer, std_path_size_m );
        std_assert_m ( len > 0 && len < std_path_size_m );
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
        std_path_flags_t flags = 0;

        if ( item->d_type == DT_DIR ) {
            flags |= std_path_is_directory_m;
        } else if ( item->d_type == DT_REG ) {
            flags |= std_path_is_file_m;
        }

        callback ( item->d_name, flags, arg );
        ++count;
        item = readdir ( dir );
    }

    closedir ( dir );

    return count;
#endif
}

size_t std_directory_files ( char** files, size_t files_cap, size_t file_cap, const char* path ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    std_assert_m ( files != NULL );
    size_t len = std_to_path_buffer ( path );
    t_path_buffer[len++ - 1] = '*';
    t_path_buffer[len++ - 1] = '\0';
    std_assert_m ( len > 0 && len < std_path_size_m );
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
            std_path_to_str ( item.cFileName, files[i], file_cap );
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

// TODO append a / at the end of the subdir name?
size_t std_directory_subdirs ( char** subdirs, size_t subdirs_cap, size_t subdir_cap, const char* path ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    std_assert_m ( subdirs != NULL );
    size_t len = std_to_path_buffer ( path );
    t_path_buffer[len++ - 1] = '*';
    t_path_buffer[len++ - 1] = '\0';
    std_assert_m ( len > 0 && len < std_path_size_m );
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
            std_path_to_str ( item.cFileName, subdirs[i], subdir_cap );
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

bool std_directory_info ( std_directory_info_t* info, const char* path ) {
    std_assert_m ( path != NULL );
    std_assert_m ( info != NULL );
#if defined(std_platform_win32_m)
    size_t len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL get_retcode = GetFileAttributesExW ( t_path_buffer, GetFileExInfoStandard, &data );

    if ( get_retcode == FALSE ) {
        return false;
    }

    uint64_t creation_time = ( uint64_t ) data.ftCreationTime.dwHighDateTime << 32 | data.ftCreationTime.dwLowDateTime;
    uint64_t last_access_time = ( uint64_t ) data.ftLastAccessTime.dwHighDateTime << 32 | data.ftLastAccessTime.dwLowDateTime;
    uint64_t last_write_time = ( uint64_t ) data.ftLastWriteTime.dwHighDateTime << 32 | data.ftLastWriteTime.dwLowDateTime;
    std_filetime_to_timestamp ( creation_time, &info->creation_time );
    std_filetime_to_timestamp ( last_access_time, &info->last_access_time );
    std_filetime_to_timestamp ( last_write_time, &info->last_write_time );

    info->flags = 0;

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ) {
        info->flags |= std_file_compressed_m;
    }

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ) {
        info->flags |= std_file_encrypted_m;
    }

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ) {
        info->flags |= std_file_hidden_m;
    }

    if ( data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ) {
        info->flags |= std_file_os_use_m;
    }

    return true;
#elif defined(std_platform_linux_m)
    struct stat data;
    int result = stat ( path, &data );
    std_assert_m ( result == 0 );

    info->creation_time.count = 0;
    std_filetime_to_timestamp ( data.st_atim, &info->last_access_time );
    std_filetime_to_timestamp ( data.st_mtim, &info->last_write_time );

    info->flags = 0;

    return true;
#endif
}

// ======================================================================================= //
//                                         F I L E
// ======================================================================================= //

#include <stdlib.h>

#if defined(std_platform_linux_m)
    #include <sys/sendfile.h>
#endif

// code is duplicated for API that takes both file and api as param because of path_buffer usage

std_file_h std_file_create ( const char* path, std_file_access_t access, std_path_already_existing_e already_existing ) {
    std_assert_m ( path != NULL );

#if defined(std_platform_win32_m)
    DWORD os_access = 0;

    if ( access & std_file_read_m ) {
        os_access |= GENERIC_READ;
    }

    if ( access & std_file_write_m ) {
        os_access |= GENERIC_WRITE;
    }

    DWORD create = already_existing == std_path_already_existing_overwrite_m ? CREATE_ALWAYS : CREATE_NEW;
    size_t len = std_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < std_path_size_m );
    HANDLE h = CreateFileW ( t_path_buffer, os_access, 0, NULL, create, FILE_ATTRIBUTE_NORMAL, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        return std_file_null_handle_m;
    }

    return ( std_file_h ) h;
#elif defined(std_platform_linux_m)
    int flags = 0;

    if ( ( access & std_file_read_m ) && ( access & std_file_write_m ) ) {
        flags |= O_RDWR;
    } else if ( access & std_file_read_m ) {
        flags |= O_RDONLY;
    } else if ( access & std_file_write_m ) {
        flags |= O_WRONLY;
    }

    int fd = open ( path, flags | O_CREAT, 0700 );

    if ( fd == -1 ) {
        return std_file_null_handle_m;
    }

    return ( std_file_h ) fd;
#endif
}

bool std_file_path_create ( const char* path, std_file_access_t access, std_path_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    DWORD os_access = 0;

    if ( access & std_file_read_m ) {
        os_access |= GENERIC_READ;
    }

    if ( access & std_file_write_m ) {
        os_access |= GENERIC_WRITE;
    }

    DWORD create = already_existing == std_path_already_existing_overwrite_m ? CREATE_ALWAYS : CREATE_NEW;
    size_t len;
    len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    HANDLE h = CreateFileW ( t_path_buffer, os_access, 0, NULL, create, FILE_ATTRIBUTE_NORMAL, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        return false;
    }

    CloseHandle ( h );
    return true;
#elif defined(std_platform_linux_m)
    std_file_h file = std_file_create ( path, access, already_existing );

    if ( file == std_file_null_handle_m ) {
        return false;
    }

    int result = close ( ( int ) file );
    return result == 0;
#endif
}

bool std_file_destroy ( std_file_h file ) {
#if defined(std_platform_win32_m)
    HANDLE h = ( HANDLE ) file;
    DWORD len = GetFinalPathNameByHandleW ( h, t_path_buffer, std_path_size_m, 0 );
    std_verify_m ( len > 0 && len < std_path_size_m );
    return DeleteFileW ( t_path_buffer ) == TRUE;
#elif defined(std_platform_linux_m)
    char path[std_path_size_m];
    size_t len = std_file_path ( path, std_path_size_m, file );
    std_verify_m ( len > 0 && len < std_path_size_m );
    return std_file_path_destroy ( path );
#endif
}

bool std_file_path_destroy ( const char* path ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    size_t len;
    len = std_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < std_path_size_m );
    return DeleteFileW ( t_path_buffer ) == TRUE;
#elif defined(std_platform_linux_m)
    int result = remove ( path );
    return result == 0;
#endif
}

bool std_file_copy ( std_file_h file, const char* dest, std_path_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    std_assert_m ( dest != NULL );
    HANDLE h = ( HANDLE ) file;
    size_t len = GetFinalPathNameByHandleW ( h, t_path_buffer, std_path_size_m, 0 );
    std_assert_m ( len > 0 && len < std_path_size_m );
    len = std_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < std_path_size_m );
    BOOL copy_retcode = CopyFileW ( t_path_buffer, t_path_buffer_2, already_existing != std_path_already_existing_overwrite_m );
    return copy_retcode == TRUE;
#elif defined(std_platform_linux_m)
    std_file_h dest_file = std_file_create ( dest, std_file_write_m, already_existing );
    std_file_info_t file_info;
    std_file_info ( &file_info, file );
    off_t copy_offset = 0;
    ssize_t copy_result = sendfile ( ( int ) dest_file, ( int ) file, &copy_offset, file_info.size );
    std_assert_m ( copy_result == file_info.size );
    return true;
#endif
}

bool std_file_path_copy ( const char* path, const char* dest, std_path_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    std_assert_m ( dest != NULL );
    size_t len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    len = std_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < std_path_size_m );
    BOOL copy_retcode = CopyFileW ( t_path_buffer, t_path_buffer_2, already_existing != std_path_already_existing_overwrite_m );
    return copy_retcode == TRUE;
#elif defined(std_platform_linux_m)
    std_file_h source_file = std_file_open ( path, std_file_read_m );
    std_assert_m ( source_file != std_file_null_handle_m );
    return std_file_copy ( source_file, dest, already_existing );
#endif
}

bool std_file_move ( std_file_h file, const char* dest, std_path_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    HANDLE h = ( HANDLE ) file;
    DWORD get_retcode;
    get_retcode = GetFinalPathNameByHandleW ( h, t_path_buffer, std_path_size_m, 0 );
    std_assert_m ( get_retcode > 0 && get_retcode < std_path_size_m );
    size_t len = std_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < std_path_size_m );
    DWORD os_flags = 0;

    if ( already_existing == std_path_already_existing_overwrite_m ) {
        os_flags |= MOVEFILE_REPLACE_EXISTING;
    }

    BOOL move_retcode = MoveFileExW ( t_path_buffer, t_path_buffer_2, os_flags );

    if ( move_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    char path[std_path_size_m];
    size_t len = std_file_path ( path, std_path_size_m, file );
    std_assert_m ( len > 0 && len < std_path_size_m );
    return std_file_path_move ( path, dest, already_existing );
#endif
}

bool std_file_path_move ( const char* path, const char* dest, std_path_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    size_t len = std_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < std_path_size_m );
    len = std_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < std_path_size_m );
    DWORD os_flags = 0;

    if ( already_existing == std_path_already_existing_overwrite_m ) {
        os_flags |= MOVEFILE_REPLACE_EXISTING;
    }

    BOOL move_retcode = MoveFileExW ( t_path_buffer, t_path_buffer_2, os_flags );

    if ( move_retcode == FALSE ) {
        false;
    }

    return true;
#elif defined(std_platform_linux_m)
    // TODO handle already_existing
    int result = rename ( path, dest );
    return result == 0;
#endif
}

std_file_h std_file_open ( const char* path, std_file_access_t access ) {
#if defined(std_platform_win32_m)
    DWORD os_access = 0;
    DWORD os_sharemode = 0;

    if ( access & std_file_read_m ) {
        os_access |= GENERIC_READ;
    }

    if ( access & std_file_write_m ) {
        os_access |= GENERIC_WRITE;
    }

    if ( access == std_file_read_m ) {
        os_sharemode = FILE_SHARE_READ;
    }

    HANDLE h = CreateFile ( path, os_access, os_sharemode, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        std_log_os_error_m();
        return std_file_null_handle_m;
    }

    return ( std_file_h ) h;
#elif defined(std_platform_linux_m)
    int flags = 0;

    if ( ( access & std_file_read_m ) && ( access & std_file_write_m ) ) {
        flags |= O_RDWR;
    } else if ( access & std_file_read_m ) {
        flags |= O_RDONLY;
    } else if ( access & std_file_write_m ) {
        flags |= O_WRONLY;
    }

    int fd = open ( path, flags );

    if ( fd == -1 ) {
        std_log_os_error_m();
        return std_file_null_handle_m;
    }

    return ( std_file_h ) fd;
#endif
}

bool std_file_close ( std_file_h file ) {
#if defined(std_platform_win32_m)
    BOOL close_retcode = CloseHandle ( ( HANDLE ) file );

    if ( close_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    int result = close ( ( int ) file );
    return result == 0;
#endif
}

void* std_file_map ( std_file_h file, size_t size, std_file_map_permits_t permits ) {
#if defined(std_platform_win32_m)
    DWORD page_permits = 0;
    DWORD map_permits = 0;

    bool copy_on_write = permits & std_file_map_copy_on_write_m;
    permits = std_bit_clear_32_m ( permits, std_file_map_copy_on_write_m );

    if ( permits == std_file_map_read_m ) {
        page_permits = PAGE_READONLY;
        map_permits = FILE_MAP_READ;
    } else if ( permits == ( std_file_map_read_m | std_file_map_write_m ) ) {
        page_permits = PAGE_READWRITE;
        map_permits = FILE_MAP_WRITE;
    } else if ( permits == ( std_file_map_execute_m | std_file_map_read_m ) ) {
        page_permits = PAGE_EXECUTE_READ;
        map_permits = FILE_MAP_READ | FILE_MAP_EXECUTE;
    } else if ( permits == ( std_file_map_execute_m | std_file_map_read_m | std_file_map_write_m ) ) {
        page_permits = PAGE_EXECUTE_READWRITE;
        map_permits = FILE_MAP_WRITE | FILE_MAP_EXECUTE;
    }

    if ( copy_on_write ) {
        map_permits |= FILE_MAP_COPY;
    }

    //std_file_info_t file_info;
    //std_file_info ( &file_info, file );
    uint32_t size_high, size_low;
    std_u64_to_2_u32 ( &size_high, &size_low, size );

    HANDLE map_handle = CreateFileMapping ( ( HANDLE ) file, NULL, page_permits, size_high, size_low, NULL );

    if ( map_handle == NULL ) {
        return NULL;
    }

    void* map = MapViewOfFile ( map_handle, map_permits, 0, 0, 0 );
    CloseHandle ( map_handle );

    return map;
#elif defined(std_platform_linux_m)
    int prot = 0;

    if ( permits & std_file_map_read_m ) {
        prot |= PROT_READ;
    }

    if ( permits & std_file_map_write_m ) {
        prot |= PROT_WRITE;
    }

    if ( permits & std_file_map_execute_m ) {
        prot |= PROT_EXEC;
    }

    int flags = 0;

    if ( permits & std_file_map_copy_on_write_m ) {
        flags |= MAP_PRIVATE;
    }

    std_file_info_t file_info;
    std_file_info ( &file_info, file );
    void* map = mmap ( NULL, size, prot, flags, ( int ) file, 0 );

    if ( map == MAP_FAILED ) {
        return NULL;
    }

    return map;
#endif
}

bool std_file_unmap ( std_file_h file, void* base ) {
#if defined(std_platform_win32_m)
    std_unused_m ( file );
    BOOL unmap_retcode = UnmapViewOfFile ( base );

    if ( unmap_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    std_file_info_t file_info;
    std_file_info ( &file_info, file );

    int result = munmap ( base, file_info.size );
    return result == 0;
#endif
}

uint64_t std_file_read ( void* dest, size_t size, std_file_h file ) {
#if defined(std_platform_win32_m)
    void* p = dest;
    size_t total_read_size = 0;
    while ( total_read_size < size ) {
        DWORD remaining_size = std_min ( UINT32_MAX, size - total_read_size );
        DWORD read_size;
        BOOL read_retcode = ReadFile ( ( HANDLE ) file, p, remaining_size, &read_size, NULL );

        if ( read_retcode == FALSE ) {
            std_log_warn_m ( "File read failed with code " std_fmt_u32_m, read_retcode );
            return std_file_read_error_m;
        }

        if ( read_size == 0 ) {
            // Assuming we reached EOF. does this hold?
            break;
        }

        p += read_size;
        total_read_size += read_size;
    }

    return total_read_size;
#elif defined(std_platform_linux_m)
    ssize_t result = read ( ( int ) file, dest, size );

    if ( result == -1 ) {
        std_log_warn_m ( "File read failed with code " std_fmt_i32_m ": " std_fmt_str_m, errno, strerror ( errno ) );
        return std_file_read_error_m;
    }

    return ( uint64_t ) result;
#endif
}

bool std_file_write ( std_file_h file, const void* source, size_t size ) {
#if defined(std_platform_win32_m)
    const void* p = source;
    size_t total_write_size = 0;
    while ( total_write_size < size ) {
        DWORD remaining_size = std_min ( UINT32_MAX, size - total_write_size );
        DWORD write_size;
        BOOL write_retcode = WriteFile ( ( HANDLE ) file, p, remaining_size, &write_size, NULL );

        if ( write_retcode == FALSE ) {
            std_log_warn_m ( "File write failed with code " std_fmt_u32_m, write_retcode );
            return false;
        }

        if ( write_size == 0 ) {
            break;
        }

        p += write_size;
        total_write_size += write_size;
    }

    return total_write_size == size;
#elif defined(std_platform_linux_m)
    ssize_t result = write ( ( int ) file, source, size );

    if ( result == -1 ) {
        std_log_warn_m ( "File write failed with code " std_fmt_i32_m ": " std_fmt_str_m, errno, strerror ( errno ) );
        return false;
    }

    return true;
#endif
}

bool std_file_seek ( std_file_h file, std_file_point_t base, int64_t offset ) {
#if defined(std_platform_win32_m)
    DWORD method;
    std_assert_m ( base == std_file_start_m || base == std_file_end_m || base == std_file_current_m );

    switch ( base ) {
        case std_file_start_m:
            method = FILE_BEGIN;
            break;

        case std_file_end_m:
            method = FILE_END;
            break;

        case std_file_current_m:
            method = FILE_CURRENT;
            break;
    }

    LARGE_INTEGER os_offset;
    os_offset.QuadPart = offset;
    BOOL set_file_pointer_retcode = SetFilePointerEx ( ( HANDLE ) file, os_offset, NULL, method );

    if ( set_file_pointer_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    int whence;
    std_assert_m ( base == std_file_start_m || base == std_file_end_m || base == std_file_current_m );

    switch ( base ) {
        case std_file_start_m:
            whence = SEEK_SET;
            break;

        case std_file_end_m:
            whence = SEEK_END;
            break;

        case std_file_current_m:
            whence = SEEK_CUR;
            break;
    }

    off_t result = lseek ( ( int ) file, ( off_t ) offset, whence );
    return result != -1;
#endif
}

bool std_file_info ( std_file_info_t* info, std_file_h file ) {
#if defined(std_platform_win32_m)
    BY_HANDLE_FILE_INFORMATION os_file_info;
    BOOL get_retcode = GetFileInformationByHandle ( ( HANDLE ) file, &os_file_info );

    if ( get_retcode == FALSE ) {
        return false;
    }

    uint64_t creation_time = ( uint64_t ) os_file_info.ftCreationTime.dwHighDateTime << 32 | os_file_info.ftCreationTime.dwLowDateTime;
    uint64_t last_access_time = ( uint64_t ) os_file_info.ftLastAccessTime.dwHighDateTime << 32 | os_file_info.ftLastAccessTime.dwLowDateTime;
    uint64_t last_write_time = ( uint64_t ) os_file_info.ftLastWriteTime.dwHighDateTime << 32 | os_file_info.ftLastWriteTime.dwLowDateTime;
    std_filetime_to_timestamp ( creation_time, &info->creation_time );
    std_filetime_to_timestamp ( last_access_time, &info->last_access_time );
    std_filetime_to_timestamp ( last_write_time, &info->last_write_time );
    info->size = ( uint64_t ) os_file_info.nFileSizeHigh << 32 | os_file_info.nFileSizeLow;
    info->file_id = ( uint64_t ) os_file_info.nFileIndexHigh << 32 | os_file_info.nFileIndexLow;
    info->volume_id = ( uint32_t ) os_file_info.dwVolumeSerialNumber;
    info->flags = 0;

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ) {
        info->flags |= std_file_compressed_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ) {
        info->flags |= std_file_encrypted_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ) {
        info->flags |= std_file_hidden_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_READONLY ) {
        info->flags |= std_file_readonly_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ) {
        info->flags |= std_file_os_use_m;
    }

    return true;
#elif defined(std_platform_linux_m)
    struct stat file_info;
    int fstat_result = fstat ( ( int ) file, &file_info );
    if ( fstat_result != 0 ) {
        std_log_os_error_m();
    }

    info->creation_time.count = 0;
    std_filetime_to_timestamp ( file_info.st_atim, &info->last_access_time );
    std_filetime_to_timestamp ( file_info.st_mtim, &info->last_write_time );
    info->size = file_info.st_size;
    info->file_id = file_info.st_ino;
    info->volume_id = file_info.st_dev;
    info->flags = 0;

    return true;
#endif
}

bool std_file_path_info ( std_file_info_t* info, const char* path ) {
    if ( !std_path_info ( NULL, path ) ) {
        return false;
    }

    std_file_h file = std_file_open ( path, std_file_read_m );

    if ( file == std_file_null_handle_m ) {
        return false;
    }

    // If info is NULL, don't even try to query for file info, just do the exist check.
    if ( info != NULL && !std_file_info ( info, file ) ) {
        return false;
    }

    std_file_close ( file );
    return true;
}

size_t std_file_path ( char* path, size_t cap, std_file_h file ) {
#if defined(std_platform_win32_m)
    DWORD get_retcode = GetFinalPathNameByHandleW ( ( HANDLE ) file, t_path_buffer, std_path_size_m, 0 );

    if ( get_retcode == 0 ) {
        return 0;
    }

    std_assert_m ( get_retcode < std_path_size_m );
    size_t len = std_from_path_buffer ( path, cap );
    std_assert_m ( len > 0 && len < std_path_size_m );
    return len;
#elif defined(std_platform_linux_m)
    int fd = ( int ) file;
    char proc[64];
    std_str_format ( proc, 32, "/proc/self/fd/" std_fmt_int_m, fd );
    ssize_t len = readlink ( proc, path, cap );

    if ( len == -1 ) {
        return 0;
    }

    return ( size_t ) len;

#endif
}

std_buffer_t std_file_read_to_virtual_heap ( const char* path ) {
    std_file_h file = std_file_open ( path, std_file_read_m );
    std_file_info_t file_info;
    bool valid_info = std_file_info ( &file_info, file );
    if ( !valid_info ) {
        return std_null_buffer_m;
    }

    void* buffer = std_virtual_heap_alloc_m ( file_info.size, 16 );
    uint64_t read_size = std_file_read ( buffer, file_info.size, file );
    if ( read_size == std_file_read_error_m ) {
        return std_null_buffer_m;
        std_virtual_heap_free ( buffer );
    }

    std_file_close ( file );
    return std_buffer ( buffer, file_info.size );
}
