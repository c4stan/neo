#include "fs_file.h"

#include "fs_shared.h"

#include <std_platform.h>
#include <std_log.h>

#include <stdlib.h>

#if defined(std_platform_linux_m)
    #include <sys/sendfile.h>
#endif

// code is duplicated for API that takes both file and api as param because of path_buffer usage

fs_file_h fs_file_create ( const char* path, fs_file_access_t access, fs_already_existing_e already_existing ) {
    std_assert_m ( path != NULL );

#if defined(std_platform_win32_m)
    DWORD os_access = 0;

    if ( access & fs_file_read_m ) {
        os_access |= GENERIC_READ;
    }

    if ( access & fs_file_write_m ) {
        os_access |= GENERIC_WRITE;
    }

    DWORD create = already_existing == fs_already_existing_overwrite_m ? CREATE_ALWAYS : CREATE_NEW;
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    HANDLE h = CreateFileW ( t_path_buffer, os_access, 0, NULL, create, FILE_ATTRIBUTE_NORMAL, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        return fs_null_handle_m;
    }

    return ( fs_file_h ) h;
#elif defined(std_platform_linux_m)
    int flags = 0;

    if ( ( access & fs_file_read_m ) && ( access & fs_file_write_m ) ) {
        flags |= O_RDWR;
    } else if ( access & fs_file_read_m ) {
        flags |= O_RDONLY;
    } else if ( access & fs_file_write_m ) {
        flags |= O_WRONLY;
    }

    int fd = open ( path, flags | O_CREAT, 0700 );

    if ( fd == -1 ) {
        return fs_null_handle_m;
    }

    return ( fs_file_h ) fd;
#endif
}

bool fs_file_path_create ( const char* path, fs_file_access_t access, fs_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    DWORD os_access = 0;

    if ( access & fs_file_read_m ) {
        os_access |= GENERIC_READ;
    }

    if ( access & fs_file_write_m ) {
        os_access |= GENERIC_WRITE;
    }

    DWORD create = already_existing == fs_already_existing_overwrite_m ? CREATE_ALWAYS : CREATE_NEW;
    size_t len;
    len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    HANDLE h = CreateFileW ( t_path_buffer, os_access, 0, NULL, create, FILE_ATTRIBUTE_NORMAL, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        return false;
    }

    CloseHandle ( h );
    return true;
#elif defined(std_platform_linux_m)
    fs_file_h file = fs_file_create ( path, access, already_existing );

    if ( file == fs_null_handle_m ) {
        return false;
    }

    int result = close ( ( int ) file );
    return result == 0;
#endif
}

bool fs_file_delete ( fs_file_h file ) {
#if defined(std_platform_win32_m)
    HANDLE h = ( HANDLE ) file;
    DWORD len = GetFinalPathNameByHandleW ( h, t_path_buffer, fs_path_size_m, 0 );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    return DeleteFileW ( t_path_buffer ) == TRUE;
#elif defined(std_platform_linux_m)
    char path[fs_path_size_m];
    size_t len = fs_file_get_path ( path, fs_path_size_m, file );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    return fs_file_path_delete ( path );
#endif
}

bool fs_file_path_delete ( const char* path ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    size_t len;
    len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    return DeleteFileW ( t_path_buffer ) == TRUE;
#elif defined(std_platform_linux_m)
    int result = remove ( path );
    return result == 0;
#endif
}

bool fs_file_copy ( fs_file_h file, const char* dest, fs_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    std_assert_m ( dest != NULL );
    HANDLE h = ( HANDLE ) file;
    size_t len = GetFinalPathNameByHandleW ( h, t_path_buffer, fs_path_size_m, 0 );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    len = fs_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    BOOL copy_retcode = CopyFileW ( t_path_buffer, t_path_buffer_2, already_existing != fs_already_existing_overwrite_m );
    return copy_retcode == TRUE;
#elif defined(std_platform_linux_m)
    fs_file_h dest_file = fs_file_create ( dest, fs_file_write_m, already_existing );
    fs_file_info_t file_info;
    fs_file_get_info ( &file_info, file );
    off_t copy_offset = 0;
    ssize_t copy_result = sendfile ( ( int ) dest_file, ( int ) file, &copy_offset, file_info.size );
    std_assert_m ( copy_result == file_info.size );
    return true;
#endif
}

bool fs_file_path_copy ( const char* path, const char* dest, fs_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    std_assert_m ( path != NULL );
    std_assert_m ( dest != NULL );
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    len = fs_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    BOOL copy_retcode = CopyFileW ( t_path_buffer, t_path_buffer_2, already_existing != fs_already_existing_overwrite_m );
    return copy_retcode == TRUE;
#elif defined(std_platform_linux_m)
    fs_file_h source_file = fs_file_open ( path, fs_file_read_m );
    std_assert_m ( source_file != fs_null_handle_m );
    return fs_file_copy ( source_file, dest, already_existing );
#endif
}

bool fs_file_move ( fs_file_h file, const char* dest, fs_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    HANDLE h = ( HANDLE ) file;
    DWORD get_retcode;
    get_retcode = GetFinalPathNameByHandleW ( h, t_path_buffer, fs_path_size_m, 0 );
    std_assert_m ( get_retcode > 0 && get_retcode < fs_path_size_m );
    size_t len = fs_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    DWORD os_flags = 0;

    if ( already_existing == fs_already_existing_overwrite_m ) {
        os_flags |= MOVEFILE_REPLACE_EXISTING;
    }

    BOOL move_retcode = MoveFileExW ( t_path_buffer, t_path_buffer_2, os_flags );

    if ( move_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    char path[fs_path_size_m];
    size_t len = fs_file_get_path ( path, fs_path_size_m, file );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    return fs_file_path_move ( path, dest, already_existing );
#endif
}

bool fs_file_path_move ( const char* path, const char* dest, fs_already_existing_e already_existing ) {
#if defined(std_platform_win32_m)
    size_t len = fs_to_path_buffer ( path );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    len = fs_to_path_buffer_2 ( dest );
    std_assert_m ( len > 0 && len < fs_path_size_m );
    DWORD os_flags = 0;

    if ( already_existing == fs_already_existing_overwrite_m ) {
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

fs_file_h fs_file_open ( const char* path, fs_file_access_t access ) {
#if defined(std_platform_win32_m)
    DWORD os_access = 0;
    DWORD os_sharemode = 0;

    if ( access & fs_file_read_m ) {
        os_access |= GENERIC_READ;
    }

    if ( access & fs_file_write_m ) {
        os_access |= GENERIC_WRITE;
    }

    if ( access == fs_file_read_m ) {
        os_sharemode = FILE_SHARE_READ;
    }

    HANDLE h = CreateFile ( path, os_access, os_sharemode, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

    if ( h == INVALID_HANDLE_VALUE ) {
        return fs_null_handle_m;
    }

    return ( fs_file_h ) h;
#elif defined(std_platform_linux_m)
    int flags = 0;

    if ( ( access & fs_file_read_m ) && ( access & fs_file_write_m ) ) {
        flags |= O_RDWR;
    } else if ( access & fs_file_read_m ) {
        flags |= O_RDONLY;
    } else if ( access & fs_file_write_m ) {
        flags |= O_WRONLY;
    }

    int fd = open ( path, flags );

    if ( fd == -1 ) {
        return fs_null_handle_m;
    }

    return ( fs_file_h ) fd;
#endif
}

bool fs_file_close ( fs_file_h file ) {
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

std_buffer_t fs_file_map ( fs_file_h file, fs_map_permits_t permits ) {
#if defined(std_platform_win32_m)
    DWORD page_permits = 0;
    DWORD map_permits = 0;

    bool copy_on_write = permits & fs_map_copy_on_write_m;
    std_bit_clear_32 ( ( uint32_t* ) &permits, fs_map_copy_on_write_m );

    if ( permits == fs_map_read_m ) {
        page_permits = PAGE_READONLY;
        map_permits = FILE_MAP_READ;
    } else if ( permits == ( fs_map_read_m | fs_map_write_m ) ) {
        page_permits = PAGE_READWRITE;
        map_permits = FILE_MAP_WRITE;
    } else if ( permits == ( fs_map_execute_m | fs_map_read_m ) ) {
        page_permits = PAGE_EXECUTE_READ;
        map_permits = FILE_MAP_READ | FILE_MAP_EXECUTE;
    } else if ( permits == ( fs_map_execute_m | fs_map_read_m | fs_map_write_m ) ) {
        page_permits = PAGE_EXECUTE_READWRITE;
        map_permits = FILE_MAP_WRITE | FILE_MAP_EXECUTE;
    }

    if ( copy_on_write ) {
        map_permits |= FILE_MAP_COPY;
    }

    fs_file_info_t file_info;
    fs_file_get_info ( &file_info, file );
    uint32_t size_high, size_low;
    std_u64_to_2_u32 ( &size_high, &size_low, file_info.size );

    HANDLE map_handle = CreateFileMapping ( ( HANDLE ) file, NULL, page_permits, size_high, size_low, NULL );

    if ( map_handle == NULL ) {
        return std_null_buffer_m;
    }

    void* map = MapViewOfFile ( map_handle, map_permits, 0, 0, 0 );
    CloseHandle ( map_handle );

    return std_buffer ( map, file_info.size );
#elif defined(std_platform_linux_m)
    int prot = 0;

    if ( permits & fs_map_read_m ) {
        prot |= PROT_READ;
    }

    if ( permits & fs_map_write_m ) {
        prot |= PROT_WRITE;
    }

    if ( permits & fs_map_execute_m ) {
        prot |= PROT_EXEC;
    }

    int flags = 0;

    if ( permits & fs_map_copy_on_write_m ) {
        flags |= MAP_PRIVATE;
    }

    fs_file_info_t file_info;
    fs_file_get_info ( &file_info, file );
    void* map = mmap ( NULL, file_info.size, prot, flags, ( int ) file, 0 );

    if ( map == MAP_FAILED ) {
        return std_null_buffer_m;
    }

    return std_buffer ( map, file_info.size );
#endif
}

bool fs_file_unmap ( std_buffer_t buffer ) {
#if defined(std_platform_win32_m)
    BOOL unmap_retcode = UnmapViewOfFile ( buffer.base );

    if ( unmap_retcode == FALSE ) {
        return false;
    }

    return true;
#elif defined(std_platform_linux_m)
    int result = munmap ( buffer.base, buffer.size );
    return result == 0;
#endif
}

bool fs_file_read ( size_t* actual_read_size, std_buffer_t dest, fs_file_h file ) {
#if defined(std_platform_win32_m)
    DWORD read_size;
    std_assert_m ( dest.size < UINT32_MAX );
    BOOL read_retcode = ReadFile ( ( HANDLE ) file, dest.base, ( DWORD ) dest.size, &read_size, NULL );

    if ( read_retcode == FALSE ) {
        std_log_warn_m ( "File read failed with code " std_fmt_u32_m, read_retcode );
        return false;
    }

    if ( actual_read_size ) {
        *actual_read_size = ( size_t ) read_size;
    }

    return true;
#elif defined(std_platform_linux_m)
    ssize_t result = read ( ( int ) file, dest.base, dest.size );

    if ( result == -1 ) {
        std_log_warn_m ( "File read failed with code " std_fmt_i32_m ": " std_fmt_str_m, errno, strerror ( errno ) );
        return false;
    }

    if ( actual_read_size ) {
        *actual_read_size = ( size_t ) result;
    }

    return true;
#endif
}

bool fs_file_write ( size_t* actual_write_size, std_buffer_t source, fs_file_h file ) {
#if defined(std_platform_win32_m)
    DWORD write_size;
    std_assert_m ( source.size < UINT32_MAX );
    BOOL write_retcode = WriteFile ( ( HANDLE ) file, source.base, ( DWORD ) source.size, &write_size, NULL );

    if ( write_retcode == FALSE ) {
        std_log_warn_m ( "File write failed with code " std_fmt_u32_m, write_retcode );
        return false;
    }

    *actual_write_size = ( size_t ) write_size;
    return true;
#elif defined(std_platform_linux_m)
    ssize_t result = read ( ( int ) file, source.base, source.size );

    if ( result == -1 ) {
        std_log_warn_m ( "File write failed with code " std_fmt_i32_m ": " std_fmt_str_m, errno, strerror ( errno ) );
        *actual_write_size = 0;
        return false;
    }

    *actual_write_size = ( size_t ) result;
    return true;
#endif
}

bool fs_file_seek ( fs_file_h file, fs_file_point_t base, int64_t offset ) {
#if defined(std_platform_win32_m)
    DWORD method;
    std_assert_m ( base == fs_file_start_m || base == fs_file_end_m || base == fs_file_current_m );

    switch ( base ) {
        case fs_file_start_m:
            method = FILE_BEGIN;
            break;

        case fs_file_end_m:
            method = FILE_END;
            break;

        case fs_file_current_m:
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
    std_assert_m ( base == fs_file_start_m || base == fs_file_end_m || base == fs_file_current_m );

    switch ( base ) {
        case fs_file_start_m:
            whence = SEEK_SET;
            break;

        case fs_file_end_m:
            whence = SEEK_END;
            break;

        case fs_file_current_m:
            whence = SEEK_CUR;
            break;
    }

    off_t result = lseek ( ( int ) file, ( off_t ) offset, whence );
    return result != -1;
#endif
}

bool fs_file_get_info ( fs_file_info_t* info, fs_file_h file ) {
#if defined(std_platform_win32_m)
    BY_HANDLE_FILE_INFORMATION os_file_info;
    BOOL get_retcode = GetFileInformationByHandle ( ( HANDLE ) file, &os_file_info );

    if ( get_retcode == FALSE ) {
        return false;
    }

    uint64_t creation_time = ( uint64_t ) os_file_info.ftCreationTime.dwHighDateTime << 32 | os_file_info.ftCreationTime.dwLowDateTime;
    uint64_t last_access_time = ( uint64_t ) os_file_info.ftLastAccessTime.dwHighDateTime << 32 | os_file_info.ftLastAccessTime.dwLowDateTime;
    uint64_t last_write_time = ( uint64_t ) os_file_info.ftLastWriteTime.dwHighDateTime << 32 | os_file_info.ftLastWriteTime.dwLowDateTime;
    fs_filetime_to_timestamp ( creation_time, &info->creation_time );
    fs_filetime_to_timestamp ( last_access_time, &info->last_access_time );
    fs_filetime_to_timestamp ( last_write_time, &info->last_write_time );
    info->size = ( uint64_t ) os_file_info.nFileSizeHigh << 32 | os_file_info.nFileSizeLow;
    info->file_id = ( uint64_t ) os_file_info.nFileIndexHigh << 32 | os_file_info.nFileIndexLow;
    info->volume_id = ( uint32_t ) os_file_info.dwVolumeSerialNumber;
    std_ignore_warning_m ( info->flags = 0, "-Wassign-enum" )

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED ) {
        info->flags |= fs_file_compressed_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED ) {
        info->flags |= fs_file_encrypted_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN ) {
        info->flags |= fs_file_hidden_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_READONLY ) {
        info->flags |= fs_file_readonly_m;
    }

    if ( os_file_info.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM ) {
        info->flags |= fs_file_os_use_m;
    }

    return true;
#elif defined(std_platform_linux_m)
    struct stat file_info;
    int fstat_result = fstat ( ( int ) file, &file_info );
    std_assert_m ( fstat_result == 0 );

    info->creation_time.count = 0;
    fs_filetime_to_timestamp ( file_info.st_atim, &info->last_access_time );
    fs_filetime_to_timestamp ( file_info.st_mtim, &info->last_write_time );
    info->size = file_info.st_size;
    info->file_id = file_info.st_ino;
    info->volume_id = file_info.st_dev;
    info->flags = 0;

    return true;
#endif
}

bool fs_file_path_get_info ( fs_file_info_t* info, const char* path ) {
    fs_file_h file = fs_file_open ( path, fs_file_read_m );

    if ( file == fs_null_handle_m ) {
        return false;
    }

    // If info is NULL, don't even try to query for file info, just do the exist check.
    if ( info != NULL && !fs_file_get_info ( info, file ) ) {
        return false;
    }

    fs_file_close ( file );
    return true;
}

size_t fs_file_get_path ( char* path, size_t cap, fs_file_h file ) {
#if defined(std_platform_win32_m)
    DWORD get_retcode = GetFinalPathNameByHandleW ( ( HANDLE ) file, t_path_buffer, fs_path_size_m, 0 );

    if ( get_retcode == 0 ) {
        return 0;
    }

    std_assert_m ( get_retcode < fs_path_size_m );
    size_t len = fs_from_path_buffer ( path, cap );
    std_assert_m ( len > 0 && len < fs_path_size_m );
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
