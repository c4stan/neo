#include <std_main.h>

#include <std_process.h>
#include <std_log.h>

#include <fs.h>

static void fs_test_run ( void ) {
    fs_i* fs = std_module_get_m ( fs_module_name_m );
    {
        size_t n = fs->get_volumes_count();
        fs_volume_h volumes[32];
        fs->get_volumes ( volumes, 32 );
        std_log_info_m ( std_fmt_size_m" volumes found:", n );

        fs->read_file ( NULL, std_buffer ( NULL, 0 ), 9458575 );

        // TODO
        for ( size_t i = 0; i < n; ++i ) {
            fs_volume_info_t info;
            fs->get_volume_info ( &info, &volumes[i] );
            char path[256];
            fs->get_volume_path ( path, 256, &volumes[i] );

#if 0

            if ( path[0] == '\0' ) {
                std_str_copy ( path, 256, "-" );
            }

#endif

            std_log_info_m ( std_fmt_size_m"-"" Path: "std_fmt_str_m" | Name: "std_fmt_str_m" | Filesystem: "std_fmt_str_m, i, path, info.name, info.file_system );
            std_log_info_m ( std_fmt_tab_m"GUID: "std_fmt_str_m, volumes[i].guid );
            std_log_info_m ( std_fmt_tab_m"Serial: "std_fmt_u64_m, info.serial_number );

            if ( info.free_size == info.available_free_size ) {
                char approx_total_size[32];
                char approx_free_size[32];
                std_size_to_str_approx ( approx_total_size, 32, info.total_size );
                std_size_to_str_approx ( approx_free_size, 32, info.free_size );
                std_log_info_m ( std_fmt_tab_m"Total size: "std_fmt_u64_m" bytes (" std_fmt_str_m ")", info.total_size, approx_total_size );
                std_log_info_m ( std_fmt_tab_m"Free size: "std_fmt_u64_m" bytes (" std_fmt_str_m ")", info.free_size, approx_free_size );
            } else {
                char approx_total_size[32];
                char approx_free_size[32];
                char approx_available_free_size[32];
                std_size_to_str_approx ( approx_total_size, 32, info.total_size );
                std_size_to_str_approx ( approx_free_size, 32, info.free_size );
                std_size_to_str_approx ( approx_available_free_size, 32, info.available_free_size );
                std_log_info_m ( std_fmt_tab_m"Total size: "std_fmt_u64_m" bytes (" std_fmt_str_m ") Free size: "std_fmt_u64_m" bytes (" std_fmt_str_m ") Avalable free size: "std_fmt_u64_m" bytes (" std_fmt_str_m ")",
                                 info.total_size, approx_total_size, info.free_size, approx_free_size, info.available_free_size, approx_available_free_size );
            }

            char* iscomp = info.flags & fs_volume_is_compressed_m ? "y" : "n";
            char* isro = info.flags & fs_volume_read_only_m ? "y" : "n";
            char* cancomp = info.flags & fs_volume_supports_compression_m ? "y" : "n";
            char* canenc = info.flags & fs_volume_supports_encryption_m ? "y" : "n";
            std_log_info_m ( std_fmt_tab_m "compressed: "std_fmt_str_m" | readonly: "std_fmt_str_m" | can-compress: "std_fmt_str_m" | can-encrypt: "std_fmt_str_m"\n",
                             iscomp, isro, cancomp, canenc );
        }
    }

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    {
        char path[256];
        std_str_copy ( path, 256, process_info.working_path );
        size_t len = fs->pop_path ( path );
        size_t len2 = std_str_len ( path );
        len = fs->pop_path ( path );
        len2 = std_str_len ( path );
        len = fs->pop_path ( path );
        len2 = std_str_len ( path );
        len = fs->append_path ( path, 256, "code/" );
        len2 = std_str_len ( path );
        char path2[256];
        len = fs->normalize_path ( path2, 256, path );
    }
    {
        char path[256];
        {
            char path2[256];
            std_str_copy ( path2, 256, process_info.executable_path );
            fs->normalize_path ( path, 256, path2 );
        }
        fs->pop_path ( path );
        std_log_info_m ( "Path: "std_fmt_str_m, path );
        fs_dir_info_t info;
        fs->get_dir_info ( &info, path );
        std_calendar_time_t creation_time = std_timestamp_to_calendar ( info.creation_time );
        std_calendar_time_t last_write_time = std_timestamp_to_calendar ( info.last_write_time );
        std_calendar_time_t last_access_time = std_timestamp_to_calendar ( info.last_access_time );
        char time[64];
        std_calendar_to_string ( creation_time, time, 64 );
        std_log_info_m ( std_fmt_tab_m"Creation time: "std_fmt_tab_m std_fmt_tab_m std_fmt_str_m, time );
        std_calendar_to_string ( last_access_time, time, 64 );
        std_log_info_m ( std_fmt_tab_m"Last access time: "std_fmt_tab_m std_fmt_str_m, time );
        std_calendar_to_string ( last_write_time, time, 64 );
        std_log_info_m ( std_fmt_tab_m"Last write time: "std_fmt_tab_m std_fmt_str_m, time );
    }

    {
        char path[256];
        std_str_copy ( path, 256, process_info.working_path );
        std_log_info_m ( "Path: "std_fmt_str_m, path );
        {
            char _subdirs[256][32];
            char* subdirs[32];

            for ( size_t i = 0; i < 32; ++i ) {
                subdirs[i] = _subdirs[i];
            }

            size_t n = fs->get_dir_subdirs ( subdirs, 32, 256, path );
            std_log_info_m ( std_fmt_str_m, std_fmt_tab_m "Folders:" );

            for ( size_t i = 0; i < n; ++i ) {
                std_log_info_m ( std_fmt_tab_m std_fmt_tab_m std_fmt_str_m, subdirs[i] );
            }
        }

        {
            char _files[256][32];
            char* files[32];

            for ( size_t i = 0; i < 32; ++i ) {
                files[i] = _files[i];
            }

            size_t n = fs->get_dir_files ( files, 32, 256, path );
            std_log_info_m ( std_fmt_str_m, std_fmt_tab_m "Files:" );

            for ( size_t i = 0; i < n; ++i ) {
                std_log_info_m ( std_fmt_tab_m std_fmt_tab_m std_fmt_str_m, files[i] );
            }

            if ( n > 0 ) {
                std_log_info_m ( std_fmt_tab_m "File: " std_fmt_str_m, files[0] );

                fs_file_info_t info;
                char path2[256];
                std_str_copy ( path2, 256, path );
                fs->append_path ( path2, 100, files[0] );
                std_assert_m ( fs->get_file_path_info ( &info, path2 ) );

                std_calendar_time_t creation_time = std_timestamp_to_calendar ( info.creation_time );
                std_calendar_time_t last_write_time = std_timestamp_to_calendar ( info.last_write_time );
                std_calendar_time_t last_access_time = std_timestamp_to_calendar ( info.last_access_time );
                char time[64];
                std_calendar_to_string ( creation_time, time, 64 );
                std_log_info_m ( std_fmt_tab_m std_fmt_tab_m"Creation time: "std_fmt_tab_m std_fmt_tab_m std_fmt_str_m, time );
                std_calendar_to_string ( last_access_time, time, 64 );
                std_log_info_m ( std_fmt_tab_m std_fmt_tab_m"Last access time: "std_fmt_tab_m std_fmt_str_m, time );
                std_calendar_to_string ( last_write_time, time, 64 );
                std_log_info_m ( std_fmt_tab_m std_fmt_tab_m"Last write time: "std_fmt_tab_m std_fmt_str_m, time );
            }
        }
    }
}

void std_main ( void ) {
    fs_test_run();
    std_log_info_m ( "FS_TEST COMPLETE!" );
}
