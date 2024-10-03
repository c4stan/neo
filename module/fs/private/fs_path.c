#include "fs_path.h"

#include <std_string.h>
#include <std_log.h>

#include "fs_shared.h"

// --
// TODO: return len or size?

// appends to path first n characters from append, adds a / if necessary
size_t fs_path_append_n ( char* path, size_t cap, const char* append, size_t n ) {
    // Algo:
    // assume that path either ends with a / (meaning it's a dir path) or it's empty -- removed
    // assume append is a valid path (meaning it does not start with a /)
    // copy append after path, assume there's enough space
    // assume append ends with / if it's a dir and doesn't if it's a file (cannot know, cannot check/do it automatically)
    std_assert_m ( path != NULL );
    std_assert_m ( append != NULL );
    size_t path_len = std_str_len ( path );
    //std_assert_m ( path_len == 0 || path[path_len - 1] == '/' );
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

size_t fs_path_append ( char* path, size_t cap, const char* append ) {
    // Algo:
    // compute append length
    // call append_n
    std_assert_m ( path != NULL );
    std_assert_m ( append != NULL );
    size_t append_len = std_str_len ( append );
    return fs_path_append_n ( path, cap, append, append_len );
}

size_t fs_path_pop ( char* path ) {
    // Algo:
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

size_t fs_path_normalize ( char* dest, size_t cap, const char* path ) {
    size_t len = 0;

    // On linux preserve initial '/'
    // TODO does win32 require a branch to ignore it?
#if defined(std_platform_linux_m)

    if ( path[0] == '/' ) {
        dest[len++] = '/';
        ++path;
    }

#endif

    const char* token = path;

    do {
        if ( std_mem_cmp ( token, "./", 2 ) ) {
            // Skip
        } else if ( std_mem_cmp ( token, "../", 3 ) ) {
            fs_path_pop ( dest );
        } else {
            size_t token_len = fs_path_token_len ( path, token );
            len = fs_path_append_n ( dest, cap, token, token_len );

            if ( * ( token + token_len ) != '\0' ) {
                len = fs_path_append ( dest, cap, "/" );
            }

            //if ( fs_path_token_is_folder ( path, token ) ) {
            //}
        }

        token = fs_path_next_token ( path, token );
    } while ( token != NULL );

    return len;
}

bool fs_path_is_drive ( const char* path ) {
#if defined(std_platform_win32_m)
    return std_str_len ( path ) == 3 && path[0] >= 65 && path[0] <= 90 && path[1] == ':' && path[2] == '/';
#elif defined(std_platform_linux_m)
    // compare path device id with parent device id, if differs then path is mount point
    char parent[fs_path_size_m];
    std_str_copy ( parent, fs_path_size_m, path );
    size_t parent_len = fs_path_pop ( parent );

    // redundant {} here because otherwise AStyle formatter goes crazy...
    {
        if ( parent_len == 0 ) {
            return true;
        }
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

const char* fs_path_get_name_ptr ( const char* path ) {
    std_assert_m ( path != NULL );
    size_t len = std_str_len ( path );
    std_assert_m ( len > 0 );
    size_t i = std_str_find_reverse ( path, len, "/" );

    if ( i == std_str_find_null_m ) {
        return path;
    }

    return path + i + 1;
}

size_t fs_path_get_name ( char* name, size_t cap, const char* path ) {
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

bool fs_path_get_info ( fs_path_info_t* info, const char* path ) {
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( path );
    std_verify_m ( len > 0 && len < fs_path_size_m );
    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL get_retcode = GetFileAttributesExW ( t_path_buffer, GetFileExInfoStandard, &data );

    if ( get_retcode == FALSE ) {
        return false;
    }

    uint64_t creation_time = ( uint64_t ) data.ftCreationTime.dwHighDateTime << 32 | data.ftCreationTime.dwLowDateTime;
    fs_filetime_to_timestamp ( creation_time, &info->creation_time );
    info->flags = 0;

    if ( data.dwFileAttributes & INVALID_FILE_ATTRIBUTES ) {
        info->flags |= fs_path_non_existent_m;
    } else if ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
        info->flags |= fs_path_is_dir_m;
    } else {
        info->flags |= fs_path_is_file_m;
    }

    return true;
#elif defined(std_platform_linux_m)
    struct stat stat_info;
    int stat_result = stat ( path, &stat_info );

    if ( stat_result == -1 && errno != ENOENT ) {
        return false;
    }

    info->creation_time.count = 0;
    info->flags = 0;

    if ( stat_result == -1 && errno == ENOENT ) {
        info->flags |= fs_path_non_existent_m;
    } else if ( S_ISREG ( stat_info.st_mode ) ) {
        info->flags |= fs_path_is_file_m;
    } else if ( S_ISDIR ( stat_info.st_mode ) ) {
        info->flags |= fs_path_is_dir_m;
    }

    return true;
#endif
}

size_t fs_path_get_absolute ( char* dest, size_t cap, const char* path ) {
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    GetFullPathNameW ( t_path_buffer, fs_path_size_m, t_path_buffer_2, NULL );
    char buffer[fs_path_size_m];
    size_t full_len = fs_from_path_buffer_2 ( buffer, fs_path_size_m );
    std_assert_m ( full_len > 0 && full_len < fs_path_size_m );
    return fs_path_normalize ( dest, cap, buffer );
#elif defined(std_platform_linux_m)
    char buffer[PATH_MAX];
    char* result = realpath ( path, buffer );

    if ( result == NULL ) {
        return 0;
    }

    return fs_path_normalize ( dest, cap, buffer );
#endif
}

// ---

const char* fs_path_next_token ( const char* path, const char* cursor ) {
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

const char* fs_path_prev_token ( const char* path, const char* cursor ) {
    std_assert_m ( path != NULL );
    std_assert_m ( cursor != NULL );

    while ( cursor > path && *cursor != '/' && *cursor != '\\' ) {
        --cursor;
    }

    if ( cursor == path ) {
        return NULL;
    }

    --cursor;

    while ( cursor > path && *cursor != '/' && *cursor != '\\' ) {
        --cursor;
    }

    if ( *cursor == '/' || *cursor == '\\' ) {
        ++cursor;
    }

    return cursor;
}

size_t fs_path_token_len ( const char* path, const char* cursor ) {
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

bool fs_path_token_is_folder ( const char* path, const char* cursor ) {
    std_unused_m ( path );

    while ( *cursor != '/' && *cursor != '\\' && *cursor != '\0' ) {
        ++cursor;
    }

    if ( *cursor == '\0' ) {
        return false;
    }

    return true;
}
