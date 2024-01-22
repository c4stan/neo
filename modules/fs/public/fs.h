#pragma once

#include <std_platform.h>
#include <std_buffer.h>
#include <std_module.h>
#include <std_time.h>

#define fs_module_name_m fs
typedef struct fs_i fs_i;
std_module_export_m void* fs_load ( void* );
std_module_export_m void fs_unload ( void );
std_module_export_m void fs_reload ( void*, void* );

/*
TODO
return values/calling 'convention'
    could return an enum at almost every call that returns some sort of info about the result of the operation
    could just use the shared assert mechanism to return to the user any type of info, using the appropriate log level (info/warn/error)
        this is probably the solution given the premise (the existance of the system itself)
        should they even return anything or just be void and assert any edge case/error found?
            this is for unknown error handling. For voluntary error handlinf, a _validate/_get_info function should be provided, that returns proper information.
*/

typedef uint64_t fs_list_h;
#define fs_list_null_m UINT64_MAX

typedef struct {
    char guid[fs_volume_guid_size_m];
} fs_volume_h;

typedef enum {
    fs_volume_supports_compression_m  = 1 << 0,
    fs_volume_read_only_m             = 1 << 1,
    fs_volume_supports_encryption_m   = 1 << 2,
    fs_volume_is_compressed_m         = 1 << 3,
} fs_volume_flags_t;

typedef struct {
    uint64_t total_size;
    uint64_t free_size;
    uint64_t available_free_size; // Can vary from free_size if per-user quotas are being used
    uint64_t serial_number;
    fs_volume_flags_t flags;
    char name[fs_volume_name_size_m];
    char file_system[fs_volume_fs_size_m];
} fs_volume_info_t;

typedef uint64_t fs_file_h;
#define fs_null_handle_m UINT64_MAX

// TODO why is this a bitset? should be an enum? values are exclusive
typedef enum {
    fs_path_is_file_m         = 1 << 0,
    fs_path_is_dir_m          = 1 << 1,
    fs_path_non_existent_m    = 1 << 2,
} fs_path_flags_t;

typedef struct {
    fs_path_flags_t flags;
    std_timestamp_t creation_time;
} fs_path_info_t;

typedef enum {
    fs_file_compressed_m  = 1 << 0,
    fs_file_encrypted_m   = 1 << 1,
    fs_file_hidden_m      = 1 << 2,
    fs_file_readonly_m    = 1 << 3,
    fs_file_os_use_m      = 1 << 4,
} fs_file_flags_t;

typedef enum {
    fs_file_read_m        = 1 << 0,
    fs_file_write_m       = 1 << 1,
} fs_file_access_t;

typedef enum {
    fs_file_start_m,
    fs_file_end_m,
    fs_file_current_m,
} fs_file_point_t;

typedef enum {
    fs_map_read_m             = 1 << 0,
    fs_map_write_m            = 1 << 1,
    fs_map_execute_m          = 1 << 2,
    fs_map_copy_on_write_m    = 1 << 3,
} fs_map_permits_t;

typedef struct {
    std_timestamp_t creation_time;
    std_timestamp_t last_access_time;
    std_timestamp_t last_write_time;
    uint64_t size;
    uint64_t file_id;
    uint32_t volume_id;
    fs_file_flags_t flags;
} fs_file_info_t;

typedef enum {
    fs_dir_compressed_m   = 1 << 0,
    fs_dir_encrypted_m    = 1 << 1,
    fs_dir_hidden_m       = 1 << 2,
    fs_dir_os_use_m       = 1 << 3,
} fs_dir_flags_t;

typedef struct {
    std_timestamp_t creation_time;
    std_timestamp_t last_access_time;
    std_timestamp_t last_write_time;
    fs_dir_flags_t flags;
} fs_dir_info_t;

typedef enum {
    fs_already_existing_overwrite_m,
    fs_already_existing_fail_m,       // TODO
    fs_already_existing_alias_m       // TODO use a similar but different destination name? Do we ever want this? Can the OS pick one for me?
} fs_already_existing_e;

typedef void ( fs_iterator_callback_f ) ( const char* name, fs_path_flags_t flags, void* arg );

typedef struct fs_i {
    //    size_t      ( *get_executable_name ) ( char* name, size_t cap );
    //    size_t      ( *get_executable_path ) ( char* path, size_t cap );

    fs_list_h   ( *get_first_volume ) ( fs_volume_h* volume );
    bool        ( *get_next_volume ) ( fs_list_h* list, fs_volume_h* volume );
    void        ( *close_volume_list ) ( fs_list_h list );
    size_t      ( *get_volumes_count ) ( void );
    size_t      ( *get_volumes ) ( fs_volume_h* volumes, size_t cap );
    bool        ( *get_volume_info ) ( fs_volume_info_t* info, const fs_volume_h* volume );
    size_t      ( *get_volume_path ) ( char* path, size_t cap, const fs_volume_h* volume );

    size_t      ( *append_path ) ( char* path, size_t cap, const char* append );
    size_t      ( *pop_path ) ( char* path );
    size_t      ( *normalize_path ) ( char* dest, size_t cap, const char* path );
    bool        ( *is_drive ) ( const char* path );
    size_t      ( *get_path_name ) ( char* name, size_t cap, const char* path );
    const char* ( *get_path_name_ptr ) ( const char* path );
    bool        ( *get_path_info ) ( fs_path_info_t* info, const char* path );
    size_t      ( *get_absolute_path ) ( char* dest, size_t dest_cap, const char* path );

    bool        ( *create_dir ) ( const char* path );
    bool        ( *delete_dir ) ( const char* path );
    bool        ( *copy_dir ) ( const char* path, const char* dest, fs_already_existing_e already_existing );
    bool        ( *move_dir ) ( const char* path, const char* dest, fs_already_existing_e already_existing );
    size_t      ( *iterate_dir ) ( const char* path, fs_iterator_callback_f cb, void* arg );
    size_t      ( *get_dir_files ) ( char** files, size_t files_cap, size_t file_cap, const char* path );
    size_t      ( *get_dir_subdirs ) ( char** subdirs, size_t subdirs_cap, size_t subdir_cap, const char* path );
    bool        ( *get_dir_info ) ( fs_dir_info_t* info, const char* path );

    fs_file_h       ( *create_file ) ( const char* path, fs_file_access_t access, fs_already_existing_e already_existing );
    bool            ( *create_file_path ) ( const char* path, fs_file_access_t access, fs_already_existing_e already_existing );
    bool            ( *delete_file ) ( fs_file_h file );
    bool            ( *delete_file_path ) ( const char* path );
    bool            ( *copy_file ) ( fs_file_h file, const char* dest, fs_already_existing_e already_existing );
    bool            ( *copy_file_path ) ( const char* path, const char* dest, fs_already_existing_e already_existing );
    bool            ( *move_file ) ( fs_file_h file, const char* dest, fs_already_existing_e already_existing );
    bool            ( *move_file_path ) ( const char* path, const char* dest, fs_already_existing_e already_existing );
    fs_file_h       ( *open_file ) ( const char* path, fs_file_access_t access );
    bool            ( *close_file ) ( fs_file_h file );
    std_buffer_t    ( *map_file ) ( fs_file_h file, fs_map_permits_t permits );
    bool            ( *unmap_file ) ( std_buffer_t mapping );
    bool            ( *read_file ) ( size_t* actual_read_size, std_buffer_t dest, fs_file_h file );
    bool            ( *write_file ) ( size_t* actual_write_size, std_buffer_t source, fs_file_h file );
    bool            ( *seek_file ) ( fs_file_h file, fs_file_point_t base, int64_t offset );
    bool            ( *get_file_info ) ( fs_file_info_t* info, fs_file_h file );
    bool            ( *get_file_path_info ) ( fs_file_info_t* info, const char* path );
    size_t          ( *get_file_path ) ( char* path, size_t cap, fs_file_h file );
} fs_i;
