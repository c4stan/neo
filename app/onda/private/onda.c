#include <onda.h>

#include <std_app.h>
#include <std_log.h>
#include <std_process.h>
#include <std_allocator.h>
#include <std_file.h>

#include <net.h>
#include <aud.h>
#include <wm.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

 #define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

typedef struct {
    net_socket_h client_socket;
    net_socket_address_t client_address;
    char path[std_path_size_m];
    char root_path[std_path_size_m];
} onda_server_state_t;

typedef struct {
    aud_i* aud;
    wm_i* wm;
    aud_device_h device;
    wm_window_h window;
} onda_client_state_t;

typedef struct {
    std_app_i api;
    bool boot;
    bool is_server;
    net_i* net;
    net_socket_h socket;
    std_virtual_stack_t read_stack;
    std_virtual_stack_t temp_stack;
    std_virtual_stack_t write_stack;
    union {
        onda_server_state_t server;
        onda_client_state_t client;
    };
} onda_state_t;

#define onda_state_m( ... ) ( onda_state_t ) { \
    __VA_ARGS__ \
}

std_module_declare_state_m ( onda );
std_module_implement_state_m ( onda );

static void onda_print_help ( void ) {
    char buffer[128];
    std_stack_t stack = std_static_stack_m ( buffer );
    std_stack_string_append ( &stack, "onda ( -s | -c )\n" );
    std_stack_string_append ( &stack, std_fmt_tab_m "if -s: PORT PATH\n" );
    std_stack_string_append ( &stack, std_fmt_tab_m "if -c: IP PORT\n" );
    std_log_info_m ( buffer );
}

static bool onda_boot_server ( uint16_t port, const char* path ) {
    net_i* net = onda_state->net;
    onda_state->is_server = true;
    onda_state->server.client_socket = net_null_handle_m;

    net_socket_address_t address = net_socket_address_m (
        .port = port,
    );
    net->ip_string_to_bytes ( &address.ip, "127.0.0.1", net_address_family_ip4_m );

    onda_state->socket = net->create_socket ( &net_socket_params_m () );
    bool success = net->bind_socket ( onda_state->socket, &address );
    if ( !success ) {
        std_log_error_m ( "Failed to bind socket address" );
        return false;
    }

    std_path_normalize ( onda_state->server.root_path, sizeof ( onda_state->server.root_path ), path );
    std_path_info_t path_info;
    success = std_path_info ( &path_info, onda_state->server.root_path );
    if ( !success ) {
        std_log_error_m ( "Invalid path" );
        return false;
    }
    if ( path_info.flags != std_path_is_directory_m ) {
        std_log_error_m ( "Path must be an existing directory" );
        return false;
    }

    success = net->listen_for_connections ( onda_state->socket );
    if ( !success ) {
        std_log_error_m ( "Failed to listen to server socket" );
        return false;
    }

    return true;
}

static bool onda_boot_client ( const char* server_ip, uint16_t server_port ) {
    net_i* net = onda_state->net;
    onda_state->is_server = false;

    onda_state->socket = net->create_socket ( &net_socket_params_m () );

    net_socket_address_t server_address = net_socket_address_m ( 
        .port = server_port
    );
    bool success = net->ip_string_to_bytes ( &server_address.ip, server_ip, net_address_family_ip4_m );
    if ( !success ) {
        std_log_error_m ( "Invalid server ip address" );
        return false;
    }

    success = net->connect_socket ( onda_state->socket, &server_address );
    if ( !success ) {
        std_log_error_m ( "Failed to connect to server" );
        return false;
    }

    aud_i* aud = std_module_load_m ( aud_module_name_m );
    onda_state->client.aud = aud;
    size_t device_count = aud->get_devices_count();
    std_log_info_m ( "Device count: " std_fmt_size_m, device_count );
    aud_device_h devices[aud_device_max_devices_m];
    aud->get_devices ( devices, aud_device_max_devices_m );
    for ( size_t i = 0; i < device_count; ++i ) {
        aud_device_info_t info;
        success = aud->get_device_info ( &info, devices[i] );
        std_assert_m ( success );
        std_log_info_m ( "Device " std_fmt_size_m ": " std_fmt_str_m, i, info.name );
    }

    aud_device_h device = devices[0];
    aud_device_params_t device_params = aud_device_params_m (
        .channel_count = 2,
        .sample_frequency = 48000,
        .bits_per_sample = 32
    );
    success = aud->activate_device ( device, &device_params );
    std_assert_m ( success );
    std_log_info_m ( "\tChannels: " std_fmt_size_m, device_params.channel_count );
    std_log_info_m ( "\tSample frequency: " std_fmt_size_m, device_params.sample_frequency );
    std_log_info_m ( "\tbpp: " std_fmt_size_m, device_params.bits_per_sample );
    aud_device_info_t device_info;
    aud->get_device_info ( &device_info, device );
    std_log_info_m ( "Picking device 0: " std_fmt_str_m, device_info.name );
    onda_state->client.device = device;

    wm_i* wm = std_module_load_m ( wm_module_name_m );
    onda_state->client.wm = wm;
    onda_state->client.window = wm->get_console_window();

    return true;
}

static bool onda_boot ( void ) {
    onda_state->boot = true;

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    if ( process_info.args_count < 3 ) {
        onda_print_help();
        std_log_error_m ( "Not enough process arguments" );
        return false;
    }

    onda_state->read_stack = std_virtual_stack_create ( 1024 * 1024 * 256 );
    onda_state->temp_stack = std_virtual_stack_create ( 1024 * 1024 * 64 );
    onda_state->write_stack = std_virtual_stack_create ( 1024 * 1024 * 256 );

    const char* mode_arg = process_info.args[1];
    if ( std_str_cmp ( mode_arg, "-s" ) == 0 ) {
        const char* port_arg = process_info.args[2];
        uint16_t port = std_str_to_u16 ( port_arg );
        const char* path_arg = process_info.args[3];
        onda_boot_server ( port, path_arg );
        return true;
    } else if ( std_str_cmp ( mode_arg, "-c" ) == 0 ) {
        const char* ip_arg = process_info.args[2];
        const char* server_port_arg = process_info.args[3];
        uint16_t server_port = std_str_to_u16 ( server_port_arg );
        return onda_boot_client ( ip_arg, server_port );
    } else {
        std_log_error_m ( "Bad mode argument: " std_fmt_str_m, mode_arg );
        return false;
    }

    return true;
}

static std_buffer_t read_socket ( net_socket_h socket, std_virtual_stack_t* stack ) {
    net_i* net = onda_state->net;
    std_virtual_stack_clear ( stack );

    size_t chunk_size = 1024 * 4;
    void* base = stack->top;
    size_t read_size = 0;
    size_t total_read_size = 0;
    do {
        void* alloc = std_virtual_stack_alloc ( stack, chunk_size );
        read_size = net->read_connected_socket ( alloc, chunk_size, socket );
        total_read_size += read_size;
    } while ( net->get_socket_available_read_size ( socket ) > 0 );

    return std_buffer_m ( .base = base, .size = total_read_size );
}

static void onda_server_drop_client ( void ) {
    net_i* net = onda_state->net;
    net->destroy_socket ( onda_state->server.client_socket );
    onda_state->server.client_socket = net_null_handle_m;
}

#define subdir_max_len_m 128
#define file_max_len_m 64

typedef struct {
    char** subdirs;
    char** files;
    uint32_t subdir_count;
    uint32_t file_count;
} list_result_t;

static list_result_t onda_build_list ( std_virtual_stack_t* stack ) {
    // TODO dynamic subdir/file count
    uint32_t max_subdirs = 256;
    uint32_t max_files = 256;

    char** subdirs = std_virtual_stack_alloc_array_m ( stack, char*, max_subdirs );
    for ( uint32_t i = 0; i < max_subdirs; ++i ) {
        subdirs[i] = std_virtual_stack_alloc ( stack, subdir_max_len_m );
    }
    size_t subdir_count = std_directory_subdirs ( subdirs, max_subdirs, subdir_max_len_m, onda_state->server.path );
    char** raw_files = std_virtual_stack_alloc_array_m ( stack, char*, max_files );
    for ( uint32_t i = 0; i < max_files; ++i ) {
        raw_files[i] = std_virtual_stack_alloc ( stack, file_max_len_m );
    }
    size_t raw_file_count = std_directory_files ( raw_files, max_files, file_max_len_m, onda_state->server.path );

    char** files = std_virtual_stack_alloc_array_m ( stack, char*, max_files );
    uint32_t file_count = 0;
    for ( uint32_t i = 0; i < raw_file_count; ++i ) {
        char* ext = std_path_ext ( raw_files[i] );
        if ( std_str_cmp ( ext, "mp3" ) != 0 && std_str_cmp ( ext, "flac" ) != 0 ) {
            continue;
        }

        files[file_count++] = raw_files[i];
    }

    list_result_t result = {
        .subdirs = subdirs,
        .files = files,
        .subdir_count = subdir_count,
        .file_count = file_count,
    };
    return result;
}

static void onda_server_send_list ( std_virtual_stack_t* stack, const list_result_t* list ) {
    net_i* net = onda_state->net;

    std_virtual_stack_string_copy_format ( stack, std_fmt_u32_m, list->subdir_count );
    for ( uint32_t i = 0; i < list->subdir_count; ++i ) {
        std_virtual_stack_string_copy ( stack, list->subdirs[i] );
    }
    std_virtual_stack_string_copy_format ( stack, std_fmt_u32_m, list->file_count );
    for ( uint32_t i = 0; i < list->file_count; ++i ) {
        std_virtual_stack_string_copy ( stack, list->files[i] );
    }
    size_t msg_size = std_virtual_stack_used_size ( stack );
    size_t write_size = net->write_connected_socket ( onda_state->server.client_socket, stack->begin, msg_size );
    std_assert_m ( msg_size == write_size );
}

typedef struct {
    uint32_t total_size;
    uint32_t media_count;
} onda_playlist_header_t;

typedef enum {
    onda_media_type_mp3_m,
    onda_media_type_flac_m,
} onda_media_type_e;

typedef struct {
    uint32_t label_size;
    uint32_t media_size;
    onda_media_type_e media_type;
    char data[];
} onda_media_header_t;

static void onda_server_write_media ( std_virtual_stack_t* stack, const char* path ) {
    std_file_h file = std_file_open ( path, std_file_read_m );
    std_file_info_t file_info;
    std_file_info ( &file_info, file );
    std_auto_m media_header = std_virtual_stack_alloc_m ( stack, onda_media_header_t );
    char* label = std_path_relative_to ( path, onda_state->server.root_path );
    size_t label_size = std_str_len ( label ) + 1;
    media_header->label_size = label_size;
    media_header->media_size = file_info.size;
    media_header->media_type = onda_media_type_mp3_m;
    void* label_data = std_virtual_stack_alloc ( stack, label_size );
    std_str_copy ( label_data, label_size, label );
    void* media_data = std_virtual_stack_alloc ( stack, file_info.size );
    std_file_read ( media_data, file_info.size, file );
    std_file_close ( file );
    std_virtual_stack_align_zero ( stack, 8 );
}

static void onda_server_send_playlist ( void ) {
    net_i* net = onda_state->net;
    std_virtual_stack_t* write_stack = &onda_state->write_stack;
    std_virtual_stack_clear ( write_stack );

    list_result_t list = onda_build_list ( &onda_state->temp_stack );

    std_auto_m playlist_header = std_virtual_stack_alloc_m ( write_stack, onda_playlist_header_t );
    playlist_header->media_count = list.file_count;

    std_path_info_t path_info;
    std_verify_m ( std_path_info ( &path_info, onda_state->server.path ) );
    if ( path_info.flags & std_path_is_file_m ) {
        onda_server_write_media ( write_stack, onda_state->server.path );
    } else {
        for ( uint32_t i = 0; i < list.file_count; ++i ) {
            std_path_append_file ( onda_state->server.path, sizeof ( onda_state->server.path ), list.files[i] );
            onda_server_write_media ( write_stack, onda_state->server.path );
            std_path_pop ( onda_state->server.path );
        }
    }

    size_t total_size = std_virtual_stack_used_size ( write_stack );
    playlist_header->total_size = total_size;
    size_t write_size = net->write_connected_socket ( onda_state->server.client_socket, write_stack->begin, total_size );
    std_assert_m ( write_size == total_size );
}

static std_app_state_e onda_update_server ( void ) {
    net_i* net = onda_state->net;

    if ( onda_state->server.client_socket == net_null_handle_m ) {
        std_log_info_m ( "Waiting for client..." );
        net_socket_h client_socket = net->accept_pending_connection ( &onda_state->server.client_address, onda_state->socket );

        onda_state->server.client_socket = client_socket;
        std_str_copy_static_m ( onda_state->server.path, onda_state->server.root_path );

        char client_ip[32];
        net->ip_bytes_to_string ( client_ip, &onda_state->server.client_address.ip, net_address_family_ip4_m );
        std_log_info_m ( "Serving client " std_fmt_str_m ":" std_fmt_u16_m"...", client_ip, onda_state->server.client_address.port );
    }

    net_socket_h client_socket = onda_state->server.client_socket;
    std_buffer_t read_buffer = read_socket ( client_socket, &onda_state->read_stack );

    if ( read_buffer.size == 0 ) {
        onda_server_drop_client();
        return std_app_state_tick_m;
    }

    char* client_msg = read_buffer.base;
    std_log_info_m ( "Received TCP string '" std_fmt_str_m "'", client_msg );

    std_virtual_stack_t* temp_stack = &onda_state->temp_stack;
    std_virtual_stack_t* write_stack = &onda_state->write_stack;
    std_virtual_stack_clear ( temp_stack );
    std_virtual_stack_clear ( write_stack );

    if ( std_str_starts_with ( client_msg, "list" ) ) {
        list_result_t list = onda_build_list ( temp_stack );
        onda_server_send_list ( write_stack, &list );
    } else if ( std_str_starts_with ( client_msg, "open" ) ) {
        list_result_t list = onda_build_list ( temp_stack );
        
        uint32_t id = std_str_to_u32 ( client_msg + 5 );
        std_assert_m ( id < list.subdir_count + list.file_count );
        if ( id < list.subdir_count ) {
            std_path_append_dir ( onda_state->server.path, sizeof ( onda_state->server.path ), list.subdirs[id] );
        } else {
            std_path_append_file ( onda_state->server.path, sizeof ( onda_state->server.path ), list.files[id - list.subdir_count] );
        }
        
        list = onda_build_list ( temp_stack );
        onda_server_send_list ( write_stack, &list );
    } else if ( std_str_starts_with ( client_msg, "back" ) ) {
        std_path_pop ( onda_state->server.path );
        list_result_t list = onda_build_list ( temp_stack );
        onda_server_send_list ( write_stack, &list );
    } else if ( std_str_starts_with ( client_msg, "play" ) ) {
        list_result_t list = onda_build_list ( temp_stack );
        
        uint32_t id = std_str_to_u32 ( client_msg + 5 );
        std_assert_m ( id < list.subdir_count + list.file_count );
        if ( id < list.subdir_count ) {
            std_path_append_dir ( onda_state->server.path, sizeof ( onda_state->server.path ), list.subdirs[id] );
        } else {
            std_path_append_file ( onda_state->server.path, sizeof ( onda_state->server.path ), list.files[id - list.subdir_count] );
        }

        onda_server_send_playlist ();

        std_path_pop ( onda_state->server.path );
    } else if ( std_str_starts_with ( client_msg, "exit" ) ) {
        net->destroy_socket ( onda_state->server.client_socket );
        onda_state->server.client_socket = net_null_handle_m;
        std_log_info_m ( "Client socket closed" );
    } else {
        net->destroy_socket ( onda_state->server.client_socket );
        onda_state->server.client_socket = net_null_handle_m;
        std_log_warn_m ( "Bad request, client dropped" );
    }

    return std_app_state_tick_m;
}

#include <stdio.h>
#include <conio.h>

static aud_source_h create_source_mp3 ( void* data, size_t size ) {
    aud_i* aud = onda_state->client.aud;
    aud_source_h source = aud_null_handle_m;
    mp3dec_ex_t mp3dec;
    if ( mp3dec_ex_open_buf ( &mp3dec, data, size, MP3D_SEEK_TO_SAMPLE ) == 0 ) {
        aud_source_params_t params = {
            .sample_frequency = mp3dec.info.hz,
            .bits_per_sample = 16,
            .sample_count = mp3dec.samples,
            .channel_count = 2, // TODO
        };
        source = aud->create_source ( &params );

        int16_t* buffer = std_virtual_heap_alloc_array_m ( int16_t, mp3dec.samples );
        uint64_t samples_read = mp3dec_ex_read ( &mp3dec, buffer, mp3dec.samples );
        std_verify_m ( samples_read == mp3dec.samples );
        aud->feed_source ( source, buffer, sizeof ( int16_t ) * mp3dec.samples );
        std_virtual_heap_free ( buffer );
        mp3dec_ex_close ( &mp3dec );
    }

    return source;
}

static aud_source_h create_source_flac ( void* data, size_t size ) {
    aud_i* aud = onda_state->client.aud;
    aud_source_h source = aud_null_handle_m;
    uint32_t channel_count;
    uint32_t sample_frequency;
    uint64_t sample_count;
    drflac_int16* buffer = drflac_open_memory_and_read_pcm_frames_s16 ( data, size, &channel_count, &sample_frequency, &sample_count, NULL );
    if ( buffer ) {
        aud_source_params_t params = {
            .sample_frequency = sample_frequency,
            .bits_per_sample = 16,
            .sample_count = sample_count,
            .channel_count = 2, // TODO
        };
        source = aud->create_source ( &params );

        aud->feed_source ( source, buffer, sizeof ( int16_t ) * sample_count );
        drflac_free ( buffer, NULL );
    }

    return source;
}

static aud_source_h create_source ( char* label, void* data, size_t size ) {
    char* ext = std_path_ext ( label );
    if ( ext ) {
        if ( std_str_cmp ( ext, "mp3" ) == 0 ) {
            return create_source_mp3 ( data, size );
        } else if ( std_str_cmp ( ext, "flac" ) == 0 ) {
            return create_source_flac ( data, size );
        }
    }

    return aud_null_handle_m;
}

static std_app_state_e onda_update_client ( void ) {
    net_i* net = onda_state->net;

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    char msg[32];
    size_t msg_size = 0;
    std_process_io_read ( msg, &msg_size, sizeof ( msg ), process_info.io.stdin_handle );
    msg[msg_size++] = '\0';

    if ( std_str_cmp ( msg, "exit" ) == 0 ) {
        return std_app_state_exit_m;
    }

    size_t write_size = net->write_connected_socket ( onda_state->socket, msg, msg_size );
    std_assert_m ( msg_size == write_size );

    std_log_info_m ( "Waiting for server..." );
    std_buffer_t read_buffer = read_socket ( onda_state->socket, &onda_state->read_stack );

    std_virtual_stack_t* write_stack = &onda_state->write_stack;
    std_virtual_stack_clear ( write_stack );
    std_virtual_stack_string_append ( write_stack, "Received:\n" );

    if ( std_str_cmp ( msg, "list" ) == 0 || std_str_starts_with ( msg, "open" ) || std_str_cmp ( msg, "back" ) == 0  ) {
        char* p = read_buffer.base;
        uint32_t dir_count = std_str_to_u32 ( p );
        p += std_str_len ( p ) + 1;
        std_virtual_stack_string_append_format ( write_stack, std_fmt_u32_m " dirs\n", dir_count );
        for ( uint32_t i = 0; i < dir_count; ++i ) {
            std_virtual_stack_string_append_format ( write_stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, p );
            p += std_str_len ( p ) + 1;
        }

        uint32_t file_count = std_str_to_u32 ( p );
        p += std_str_len ( p ) + 1;
        std_virtual_stack_string_append_format ( write_stack, std_fmt_u32_m " files\n", file_count );
        for ( uint32_t i = 0; i < file_count; ++i ) {
            std_virtual_stack_string_append_format ( write_stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, p );
            p += std_str_len ( p ) + 1;
        }

        std_log_info_m ( write_stack->begin );
    } else if ( std_str_starts_with ( msg, "play" ) ) {
        aud_i* aud = onda_state->client.aud;

        bool exit = false;

        std_auto_m playlist_header = ( onda_playlist_header_t* ) read_buffer.base;
        std_assert_m ( playlist_header->total_size == read_buffer.size );
        uint32_t media_count = playlist_header->media_count;
        void* p = playlist_header + 1; 
        for ( uint32_t media_it = 0; media_it < media_count; ++media_it ) {
            if ( exit ) {
                break;
            }

            std_auto_m media_header = ( onda_media_header_t* ) p;
            uint32_t label_size = media_header->label_size;
            onda_media_type_e media_type = media_header->media_type;
            uint32_t media_size = media_header->media_size;

            std_assert_m ( media_type == onda_media_type_mp3_m );
            char* label = media_header->data;
            void* data = label + label_size;

            p += sizeof ( onda_media_header_t ) + label_size + media_size;
            p = std_align_ptr ( p, 8 );

            aud_source_h source = create_source ( label, data, media_size );

            if ( source != aud_null_handle_m ) {
                std_log_m ( std_log_level_custom_m, "" );

                aud->play_source ( source );
                aud->set_source_volume ( source, 0.2f );

                const std_ring_t* device_ring = aud->get_device_ring ( onda_state->client.device );
                uint64_t ring_capacity = std_ring_capacity ( device_ring );

                aud_device_info_t device_info;
                aud->get_device_info ( &device_info, onda_state->client.device );

                wm_i* wm = onda_state->client.wm;
                bool first_print = true;
                uint64_t step_ms = 20;
                std_tick_t frame_tick = std_tick_now();
                wm_input_state_t old_input_state;
                wm->get_window_input_state ( onda_state->client.window, &old_input_state );
                while ( true ) {
                    wm->update_window ( onda_state->client.window );

                    wm_window_info_t window_info;
                    wm->get_window_info ( &window_info, onda_state->client.window );
                    uint64_t sleep_ms = 0;
                    if ( !window_info.is_focus ) {
                        sleep_ms = step_ms / 2;
                    }
                    
                    wm_input_state_t new_input_state;
                    wm->get_window_input_state ( onda_state->client.window, &new_input_state );
                    if ( new_input_state.keyboard[wm_keyboard_state_esc_m] && !old_input_state.keyboard[wm_keyboard_state_esc_m] ) {
                        exit = true;
                        break;
                    }
                    if ( new_input_state.keyboard[wm_keyboard_state_alt_left_m] ) {
                        if ( new_input_state.keyboard[wm_keyboard_state_right_m] && !old_input_state.keyboard[wm_keyboard_state_right_m] ) {
                            aud->skip_source ( source, 10 );
                        }
                        if ( new_input_state.keyboard[wm_keyboard_state_left_m] && !old_input_state.keyboard[wm_keyboard_state_left_m] ) {
                            aud->skip_source ( source, -10 );
                        }
                    }

                    old_input_state = new_input_state;

                    std_tick_t new_tick = std_tick_now();
                    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );
                    if ( delta_ms < step_ms / 2 ) {
                        std_thread_this_sleep ( sleep_ms );
                        continue;
                    }
                    
                    frame_tick = new_tick;

                    aud_source_info_t source_info;
                    aud->get_source_info ( &source_info, source );
                    float total_duration = source_info.total_time;
                    float bar_tick = total_duration * 1000.f / 60.f;

                    aud->update_device_ring ( onda_state->client.device );
                    uint64_t ring_count = std_ring_count ( device_ring );

                    {
                        char bar[100];
                        size_t i = 0;

                        bar[i++] = '[';

                        uint64_t ms = ( uint64_t ) ( source_info.time_played * 1000.f );

                        while ( ms > bar_tick && i < 60 ) {
                            bar[i++] = '=';
                            ms -= bar_tick;
                        }

                        while ( i < 60 ) {
                            bar[i++] = ' ';
                        }

                        bar[i++] = ']';

                        const char* prefix = "";

                        if ( first_print ) {
                            prefix = "\n";
                        }

                        first_print = false;

                        std_log_m ( std_log_level_custom_m, std_fmt_str_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_str_m "\n" std_fmt_str_m " " std_fmt_u64_pad_m ( 2 ) "/" std_fmt_u64_m, 
                            prefix, label, bar, ring_count, ring_capacity );
                    }

                    if ( source_info.time_played < total_duration ) {
                        if ( ring_count < ring_capacity ) {
                            aud->output_to_device ( onda_state->client.device, step_ms );
                        }
                    } else if ( ring_count == 0 ) {
                        break;
                    }
                    
                    std_thread_this_sleep ( sleep_ms );
                }

                aud->destroy_source ( source );
            }
        }
    } else {
        std_log_error_m ( "Bad input" );
        return std_app_state_exit_m;
    }

    return std_app_state_tick_m;
}

static std_app_state_e onda_update ( void ) {
    if ( onda_state->is_server ) {
        return onda_update_server();
    } else {
        return onda_update_client();
    }
}

std_app_state_e onda_tick ( void ) {
    if ( !onda_state->boot ) {
        if ( !onda_boot() ) {
            return std_app_state_exit_m;
        }
    }
    return onda_update();
}

std_module_export_m void* onda_load ( void* runtime ) {
    std_runtime_bind ( runtime );
    onda_state_t* state = onda_state_alloc();
    *state = onda_state_m (
        .api.tick = onda_tick,
        .net = std_module_load_m ( net_module_name_m ),
    );
    return state;
}

std_module_export_m void onda_unload ( void ) {
    std_module_unload_m ( net_module_name_m );
    onda_state_free();
}

std_module_export_m void onda_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );
    std_auto_m state = ( onda_state_t* ) api;
    state->api.tick = onda_tick;
    onda_state_bind ( state );
}
