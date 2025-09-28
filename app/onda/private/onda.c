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
#include "foxen-flac.h"

#define onda_subpath_max_len_m 128
#define onda_stream_chunk_size_m (1024ull * 1024 * 1)

typedef struct {
    net_socket_h client_socket;
    net_socket_address_t client_address;
    //char path[std_path_size_m];
    char root_path[std_path_size_m];
} onda_server_state_t;

typedef struct {
    char** subdirs;
    char** files;
    uint32_t subdir_count;
    uint32_t file_count;
} list_result_t;

typedef struct {
    aud_i* aud;
    wm_i* wm;
    aud_device_h device;
    wm_window_h window;
    list_result_t list;
    float volume;
    char path[std_path_size_m];
} onda_client_state_t;

typedef struct {
    std_app_i api;
    bool boot;
    bool is_server;
    net_i* net;
    net_socket_h socket;
    std_stack_t stack;
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

    onda_state->client.path[0] = '\0';
    std_mem_zero_m ( &onda_state->client.list );
    onda_state->client.volume = 0.2f;

    return true;
}

static bool onda_boot ( void ) {
    onda_state->boot = true;

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );

    if ( process_info.args_count < 3 ) {
        onda_print_help();
        std_log_error_m ( "Bad arguments" );
        return false;
    }

    onda_state->stack = std_stack_create ( 1024ull * 1024 * 1024 );

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

#if 0
static std_buffer_t read_socket ( net_socket_h socket, std_stack_t* stack ) {
    net_i* net = onda_state->net;
    std_stack_clear ( stack );

    size_t chunk_size = 1024 * 4;
    void* base = stack->top;
    size_t read_size = 0;
    size_t total_read_size = 0;
    do {
        void* alloc = std_stack_alloc ( stack, chunk_size );
        read_size = net->read_connected_socket ( alloc, chunk_size, socket );
        total_read_size += read_size;
    } while ( net->get_socket_available_read_size ( socket ) > 0 );

    return std_buffer_m ( .base = base, .size = total_read_size );
}
#endif

//static void onda_server_drop_client ( void ) {
//    net_i* net = onda_state->net;
//    net->destroy_socket ( onda_state->server.client_socket );
//    onda_state->server.client_socket = net_null_handle_m;
//}

static list_result_t onda_build_list ( std_stack_t* stack, const char* client_path ) {
    // TODO dynamic subdir/file count
    uint32_t max_subdirs = 256;
    uint32_t max_files = 256;

    char* path = std_stack_alloc ( stack, std_path_size_m );
    std_str_copy ( path, std_path_size_m, onda_state->server.root_path );
    std_path_append ( path, std_path_size_m, client_path );

    char** subdirs = std_stack_alloc_array_m ( stack, char*, max_subdirs );
    for ( uint32_t i = 0; i < max_subdirs; ++i ) {
        subdirs[i] = std_stack_alloc ( stack, onda_subpath_max_len_m );
    }
    size_t subdir_count = std_directory_subdirs ( subdirs, max_subdirs, onda_subpath_max_len_m, path );
    char** raw_files = std_stack_alloc_array_m ( stack, char*, max_files );
    for ( uint32_t i = 0; i < max_files; ++i ) {
        raw_files[i] = std_stack_alloc ( stack, onda_subpath_max_len_m );
    }
    size_t raw_file_count = std_directory_files ( raw_files, max_files, onda_subpath_max_len_m, path );

    char** files = std_stack_alloc_array_m ( stack, char*, max_files );
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

typedef struct {
    uint32_t total_size;
    uint32_t subdir_count;
    uint32_t file_count;
    char data[];
} onda_list_header_t;

static void onda_server_send_list ( std_stack_t* stack, const list_result_t* list ) {
    net_i* net = onda_state->net;

    std_auto_m header = std_stack_alloc_m ( stack, onda_list_header_t );
    header->subdir_count = list->subdir_count;
    header->file_count = list->file_count;

    // TODO dynamic size paths
    for ( uint32_t i = 0; i < list->subdir_count; ++i ) {
        char* subdir = std_stack_alloc ( stack, onda_subpath_max_len_m );
        std_str_copy ( subdir, onda_subpath_max_len_m, list->subdirs[i] );
    }

    for ( uint32_t i = 0; i < list->file_count; ++i ) {
        char* file = std_stack_alloc ( stack, onda_subpath_max_len_m );
        std_str_copy ( file, onda_subpath_max_len_m, list->files[i] );
    }

    size_t msg_size = std_stack_used_size_from ( stack, header );
    header->total_size = msg_size;

    net->write_connected_socket ( onda_state->server.client_socket, header, msg_size );
}

//typedef struct {
//    uint32_t media_count;
//} onda_playlist_header_t;

typedef enum {
    onda_client_stream_type_mp3_m,
    onda_client_stream_type_flac_m,
    onda_client_stream_type_unknown_m,
} onda_client_stream_type_e;

typedef struct {
    uint32_t label_size;
    uint32_t media_size;
    uint32_t sample_count;
    uint32_t sample_frequency;
    uint32_t channel_count;
    uint32_t bits_per_sample;
    onda_client_stream_type_e type;
    char data[];
} onda_media_header_t;

#if 0
static void onda_server_send_media ( std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;
    std_file_h file = std_file_open ( path, std_file_read_m );
    std_file_info_t file_info;
    std_file_info ( &file_info, file );
    char* label = std_path_relative_to ( path, onda_state->server.root_path );
    size_t label_size = std_str_len ( label ) + 1;
    std_auto_m header = std_stack_alloc_m ( stack, onda_media_header_t );
    header->label_size = label_size;
    header->media_size = file_info.size;
    void* label_data = std_stack_alloc ( stack, label_size );
    std_str_copy ( label_data, label_size, label );
    void* media_data = std_stack_alloc ( stack, file_info.size );
    std_file_read ( media_data, file_info.size, file );
    std_file_close ( file );

    char* ext = std_path_ext ( path );
    if ( ext ) {
        if ( std_str_cmp ( ext, "mp3" ) == 0 ) {
            header->type = onda_client_stream_type_mp3_m;
            mp3dec_ex_t dec;
            mp3dec_ex_open_buf ( &dec, media_data, file_info.size, MP3D_SEEK_TO_SAMPLE );
            header->sample_count = dec.samples;
            header->sample_frequency = dec.info.hz;
            header->channel_count = dec.info.channels;
            header->bits_per_sample = 16;
        } else if ( std_str_cmp ( ext, "flac" ) == 0 ) {
            header->type = onda_client_stream_type_flac_m;
            header->bits_per_sample = 32;
            // TODO            
        } else {
            header->type = onda_client_stream_type_unknown_m;
        }
    }
    net->write_connected_socket ( onda_state->server.client_socket, header, std_stack_used_size_from ( stack, header ) );
}
#endif

static void onda_server_stream_media ( std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;
    std_file_h file = std_file_open ( path, std_file_read_m );
    std_file_info_t file_info;
    std_file_info ( &file_info, file );
    char* label = std_path_relative_to ( path, onda_state->server.root_path );
    size_t label_size = std_str_len ( label ) + 1;
    void* begin = std_stack_alloc ( stack, sizeof ( onda_media_header_t ) );
    std_auto_m header = ( onda_media_header_t* ) begin;
    header->label_size = label_size;
    header->media_size = file_info.size;
    void* label_data = std_stack_alloc ( stack, label_size );
    std_str_copy ( label_data, label_size, label );
    void* media_data = std_stack_alloc ( stack, file_info.size );
    std_file_read ( media_data, file_info.size, file );
    std_file_close ( file );

    char* ext = std_path_ext ( path );
    if ( ext ) {
        if ( std_str_cmp ( ext, "mp3" ) == 0 ) {
            header->type = onda_client_stream_type_mp3_m;

            mp3dec_ex_t dec;
            mp3dec_ex_open_buf ( &dec, media_data, file_info.size, MP3D_SEEK_TO_SAMPLE );
            header->sample_frequency = dec.info.hz;
            header->channel_count = dec.info.channels;
            header->bits_per_sample = 16;
#if 0
            header->sample_count = dec.samples;
#else
            uint64_t sample_count = 0;
            uint32_t decode_offset = 0;
            mp3dec_t mp3dec;
            mp3dec_init ( &mp3dec );
            while ( decode_offset < file_info.size ) {
                mp3dec_frame_info_t info;
                int samples = mp3dec_decode_frame ( &mp3dec, media_data + decode_offset, file_info.size - decode_offset, NULL, &info );
                if ( samples > 0 ) {
                    sample_count += ( uint64_t ) samples * info.channels;
                }
                decode_offset += info.frame_bytes;
            }
            header->sample_count = sample_count;
#endif
        } else if ( std_str_cmp ( ext, "flac" ) == 0 ) {
            header->type = onda_client_stream_type_flac_m;
        
            fx_flac_t* flac = FX_FLAC_ALLOC_DEFAULT();
            uint32_t decode_offset = 0;
            uint32_t decode_size = file_info.size - decode_offset;
            while ( fx_flac_process ( flac, media_data + decode_offset, &decode_size, NULL, NULL) < FLAC_END_OF_METADATA ) {
                decode_offset += decode_size;
                decode_size = file_info.size - decode_offset;
            }

            header->sample_frequency = fx_flac_get_streaminfo ( flac, FLAC_KEY_SAMPLE_RATE );
            header->channel_count = fx_flac_get_streaminfo ( flac, FLAC_KEY_N_CHANNELS );
            header->bits_per_sample = 32;
            header->sample_count = fx_flac_get_streaminfo ( flac, FLAC_KEY_N_SAMPLES ) * header->channel_count;
        } else {
            header->type = onda_client_stream_type_unknown_m;
        }
    }

#if 1
    uint32_t total_size = std_stack_used_size_from ( stack, begin );
    uint32_t stream_size = 0;
    while ( stream_size < total_size ) {
        uint32_t chunk_size = onda_stream_chunk_size_m;
        uint32_t remaining_size = total_size - stream_size;
        uint32_t write_size = remaining_size < chunk_size ? remaining_size : chunk_size;
        net->write_connected_socket ( onda_state->server.client_socket, begin + stream_size, write_size );
        stream_size += write_size;
    }
#else
    net->write_connected_socket ( onda_state->server.client_socket, begin, std_stack_used_size_from ( stack, begin ) );
#endif
}

#if 0
static void onda_server_send_playlist ( void ) {
    net_i* net = onda_state->net;
    std_stack_t* write_stack = &onda_state->write_stack;
    std_stack_clear ( write_stack );

    list_result_t list = onda_build_list ( &onda_state->temp_stack );

    onda_playlist_header_t playlist_header = {
        .media_count = list.file_count,
    };
    net->write_connected_socket ( onda_state->server.client_socket, &playlist_header, sizeof ( playlist_header ) );

    std_path_info_t path_info;
    std_verify_m ( std_path_info ( &path_info, onda_state->server.path ) );
    if ( path_info.flags & std_path_is_file_m ) {
        onda_server_stream_media ( write_stack, onda_state->server.path );
    } else {
        for ( uint32_t i = 0; i < list.file_count; ++i ) {
            std_path_append_file ( onda_state->server.path, sizeof ( onda_state->server.path ), list.files[i] );
            onda_server_stream_media ( write_stack, onda_state->server.path );
            std_path_pop ( onda_state->server.path );
            std_stack_clear ( write_stack );
        }
    }
    
    std_stack_clear ( write_stack );
}
#endif

typedef struct {
    uint32_t data_size;
    char data[];
} onda_client_request_t;

static std_app_state_e onda_update_server ( void ) {
    net_i* net = onda_state->net;

    if ( onda_state->server.client_socket == net_null_handle_m ) {
        std_log_info_m ( "Waiting for client..." );
        net_socket_h client_socket = net->accept_pending_connection ( &onda_state->server.client_address, onda_state->socket );

        onda_state->server.client_socket = client_socket;
        //std_str_copy_static_m ( onda_state->server.path, onda_state->server.root_path );

        char client_ip[32];
        net->ip_bytes_to_string ( client_ip, &onda_state->server.client_address.ip, net_address_family_ip4_m );
        std_log_info_m ( "Serving client " std_fmt_str_m ":" std_fmt_u16_m"...", client_ip, onda_state->server.client_address.port );
    }

    net_socket_h client_socket = onda_state->server.client_socket;
    std_stack_t* stack = &onda_state->stack;
    std_stack_clear ( stack );
    std_auto_m request = std_stack_alloc_m ( stack, onda_client_request_t );
    size_t read_result = net->read_connected_socket ( request, sizeof ( onda_client_request_t ), client_socket, net_connected_socket_read_flag_read_all_m );
    if ( read_result == 0 ) {
        net->destroy_socket ( onda_state->server.client_socket );
        onda_state->server.client_socket = net_null_handle_m;
        std_log_info_m ( "Client socket closed" );
        return std_app_state_tick_m;
    }

    char* client_msg = std_stack_alloc ( stack, request->data_size );
    net->read_connected_socket ( client_msg, request->data_size, client_socket, net_connected_socket_read_flag_read_all_m );
    std_log_info_m ( "Received request '" std_fmt_str_m "'", client_msg );

    if ( std_str_starts_with ( client_msg, "list" ) ) {
        char* path = client_msg + 5;
        list_result_t list = onda_build_list ( stack, path );
        onda_server_send_list ( stack, &list );
#if 0
    } else if ( std_str_starts_with ( client_msg, "open" ) ) {
        char* path = client_msg + 5;
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
#endif   
    } else if ( std_str_starts_with ( client_msg, "play" ) ) {
        char* client_path = client_msg + 5;
#if 0
        list_result_t list = onda_build_list ( temp_stack, path );
        uint32_t id = std_str_to_u32 ( client_msg + 5);
        std_assert_m ( id < list.subdir_count + list.file_count );
        if ( id < list.subdir_count ) {
            std_path_append_dir ( onda_state->server.path, sizeof ( onda_state->server.path ), list.subdirs[id] );
        } else {
            std_path_append_file ( onda_state->server.path, sizeof ( onda_state->server.path ), list.files[id - list.subdir_count] );
        }

        onda_server_send_playlist ();

        std_path_pop ( onda_state->server.path );
#endif
        char* media_path = std_stack_string_copy ( stack, onda_state->server.root_path );
        std_stack_alloc ( stack, std_str_len ( client_path ) );
        std_path_append_file ( media_path, (char*) stack->top - media_path, client_path );
        onda_server_stream_media ( stack, media_path );
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

#if 0
static aud_source_h create_source_mp3 ( void* data, size_t size ) {
    aud_i* aud = onda_state->client.aud;
    aud_source_h source = aud_null_handle_m;
    mp3dec_ex_t mp3dec;
    if ( mp3dec_ex_open_buf ( &mp3dec, data, size, MP3D_SEEK_TO_SAMPLE ) == 0 ) {
        std_assert_m ( mp3dec.info.channels == 2 );
        aud_source_params_t params = {
            .sample_frequency = mp3dec.info.hz,
            .bits_per_sample = 16,
            .sample_count = mp3dec.samples,
            .channel_count = 2,
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
        std_assert_m ( channel_count == 2 );
        aud_source_params_t params = {
            .sample_frequency = sample_frequency,
            .bits_per_sample = 16,
            .sample_count = sample_count,
            .channel_count = 2,
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
#endif

typedef struct {
    uint32_t total_size;
    uint32_t read_size;
    uint32_t consumed_size;
    void* begin;
    // TODO
    onda_client_stream_type_e type;
    union {
        mp3dec_t mp3dec;
        fx_flac_t* flac;
    };
} onda_client_stream_state_t;

static aud_source_h create_stream_source ( onda_client_stream_state_t* stream_state, const onda_media_header_t* header ) {
    aud_i* aud = onda_state->client.aud;
    aud_source_h source = aud_null_handle_m;

    std_assert_m ( header->channel_count == 2 );
    //std_assert_m ( header->bits_per_sample == 16 );
    aud_source_params_t params = {
        .sample_frequency = header->sample_frequency,
        .bits_per_sample = header->bits_per_sample,
        .sample_count = header->sample_count,
        .channel_count = header->channel_count,
    };
    source = aud->create_source ( &params );

    return source;
}

//static void flush_stream ( onda_client_stream_state_t* stream_state ) {
//}

static list_result_t onda_client_read_list ( std_stack_t* stack ) {
    net_i* net = onda_state->net;
    net_socket_h socket = onda_state->socket;

    std_auto_m header = std_stack_alloc_m ( stack, onda_list_header_t );
    net->read_connected_socket ( header, sizeof ( onda_list_header_t ), socket, net_connected_socket_read_flag_read_all_m );

    uint32_t subdir_data_size = onda_subpath_max_len_m * header->subdir_count;
    uint32_t file_data_size = onda_subpath_max_len_m * header->file_count;
    char* subdir_data = std_stack_alloc ( stack, subdir_data_size );
    char* file_data = std_stack_alloc ( stack, file_data_size );
    net->read_connected_socket ( header->data, subdir_data_size + file_data_size, socket, net_connected_socket_read_flag_read_all_m );

    list_result_t list = {
        .subdir_count = header->subdir_count,
        .file_count = header->file_count,
    };
    list.subdirs = std_stack_alloc ( stack, subdir_data_size );
    list.files = std_stack_alloc ( stack, file_data_size );
    for ( uint32_t i = 0; i < list.subdir_count; ++i ) {
        list.subdirs[i] = subdir_data + onda_subpath_max_len_m * i;
    }
    for ( uint32_t i = 0; i < list.file_count; ++i ) {
        list.files[i] = file_data + onda_subpath_max_len_m * i;
    }

    return list;
}

//static onda_playlist_header_t* onda_client_read_playlist_header ( std_stack_t* stack ) {
//    net_i* net = onda_state->net;
//    net_socket_h socket =  onda_state->socket;
//
//    std_stack_align ( stack, 8 );
//    std_auto_m header = std_stack_alloc_m ( stack, onda_playlist_header_t );
//    net->read_connected_socket ( header, sizeof ( onda_playlist_header_t ), socket, net_connected_socket_read_flag_read_all_m );
//
//    return header;
//}

//static onda_media_header_t* onda_client_read_media ( std_stack_t* stack ) {
//    net_i* net = onda_state->net;
//    net_socket_h socket =  onda_state->socket;
//
//    std_stack_align ( stack, 8 );
//    std_auto_m header = std_stack_alloc_m ( stack, onda_media_header_t );
//    net->read_connected_socket ( header, sizeof ( onda_media_header_t ), socket, net_connected_socket_read_flag_read_all_m );
//
//    uint32_t data_size = header->label_size + header->media_size;
//    void* data = std_stack_alloc ( stack, data_size );
//    net->read_connected_socket ( data, data_size, socket, net_connected_socket_read_flag_read_all_m );
//
//    return header;
//}

static onda_media_header_t* onda_client_read_media_header ( std_stack_t* stack, onda_client_stream_state_t* state ) {
    net_i* net = onda_state->net;
    net_socket_h socket =  onda_state->socket;

    std_stack_align ( stack, 8 );
    std_auto_m header = std_stack_alloc_m ( stack, onda_media_header_t );
    net->read_connected_socket ( header, sizeof ( onda_media_header_t ), socket, net_connected_socket_read_flag_read_all_m );

    // read label
    uint32_t label_size = header->label_size;
    char* label = std_stack_alloc ( stack, label_size );
    net->read_connected_socket ( label, label_size, socket, net_connected_socket_read_flag_read_all_m );

    // read first chunk
    //std_stack_align ( stack, 8 );
    //uint32_t data_size = std_min ( onda_stream_chunk_size_m, header->media_size );
    //void* data = std_stack_alloc ( stack, data_size );
    //uint32_t read_size = net->read_connected_socket ( data, data_size, socket, net_connected_socket_read_flag_read_all_m );
    
    state->begin = std_stack_align ( stack, 8 );
    state->total_size = header->media_size;
    state->read_size = 0;
    state->consumed_size = 0;
    state->type = header->type;

    return header;
}

static void* onda_client_read_media_chunk ( std_stack_t* stack, onda_client_stream_state_t* state ) {
    net_i* net = onda_state->net;
    net_socket_h socket =  onda_state->socket;

#if 0
    uint32_t remaining_size = state->total_size - state->read_size;
    uint32_t alloc_size = remaining_size < onda_stream_chunk_size_m ? remaining_size : onda_stream_chunk_size_m;
    void* data = std_stack_alloc ( stack, alloc_size );
    uint32_t read_size = net->read_connected_socket ( data, alloc_size, socket, net_connected_socket_read_flag_read_all_m );
    state->read_size += read_size;
    std_stack_free ( stack, alloc_size - read_size );
#else
    uint32_t alloc_size = state->total_size - state->read_size;
    void* data = std_stack_alloc ( stack, alloc_size );
    uint32_t read_size = net->read_connected_socket ( data, alloc_size, socket, net_connected_socket_read_flag_none_m );
    state->read_size += read_size;
    std_stack_free ( stack, alloc_size - read_size );
#endif

    return data;
}

#if 0
static bool onda_client_play_source ( aud_source_h source, char* label ) {
    aud_i* aud = onda_state->client.aud;

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
            return false;
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
            char bar[100] = { 0 };
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

    return true;
}
#endif

static void onda_client_feed_stream ( aud_source_h source, onda_client_stream_state_t* stream_state ) {
    aud_i* aud = onda_state->client.aud;
    std_stack_t* stack = &onda_state->stack;
    
    if ( stream_state->read_size < stream_state->total_size ) {
        onda_client_read_media_chunk ( stack, stream_state );
    }

    if ( stream_state->type == onda_client_stream_type_mp3_m ) {
        int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        while ( stream_state->consumed_size < stream_state->read_size ) {
            void* decode_base = stream_state->begin + stream_state->consumed_size;
            uint32_t decode_size = stream_state->read_size - stream_state->consumed_size;
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame ( &stream_state->mp3dec, decode_base, decode_size, pcm, &info );
            stream_state->consumed_size += info.frame_bytes;
            aud->feed_source ( source, pcm, sizeof ( int16_t ) * samples * info.channels );
        }
    } else if ( stream_state->type == onda_client_stream_type_flac_m ) {
        int32_t pcm[4096];
        while ( stream_state->consumed_size < stream_state->read_size ) {
            void* decode_base = stream_state->begin + stream_state->consumed_size;
            uint32_t decode_size = stream_state->read_size - stream_state->consumed_size;
            uint32_t sample_count = std_static_array_capacity_m ( pcm );
            fx_flac_state_t decode_result = fx_flac_process ( stream_state->flac, decode_base, &decode_size, pcm, &sample_count );
            if ( decode_result == FLAC_ERR ) {
                // TODO
                std_assert_m ( false );
            }
            stream_state->consumed_size += decode_size;
            aud->feed_source ( source, pcm, sizeof ( int32_t ) * sample_count );
        }
    } else if ( stream_state->type == onda_client_stream_type_unknown_m ) {
        std_log_error_m ( "Unknown stream type" );
    } else {
        std_log_error_m ( "Malformed stream type" );
    }
}

static void onda_client_decode_init ( onda_client_stream_state_t* stream_state ) {
    if ( stream_state->type == onda_client_stream_type_mp3_m ) {
        mp3dec_init ( &stream_state->mp3dec );
    } else if ( stream_state->type == onda_client_stream_type_flac_m ) {
        stream_state->flac = FX_FLAC_ALLOC_DEFAULT();
    } else if ( stream_state->type == onda_client_stream_type_unknown_m ) {
        std_log_error_m ( "Unknown stream type" );
    } else {
        std_log_error_m ( "Malformed stream type" );
    }
}

static bool onda_client_stream_source ( aud_source_h source, char* label, onda_client_stream_state_t* stream_state ) {
    aud_i* aud = onda_state->client.aud;

    //std_log_m ( std_log_level_custom_m, "" );

    aud->play_source ( source );
    aud->set_source_volume ( source, onda_state->client.volume );

    const std_ring_t* device_ring = aud->get_device_ring ( onda_state->client.device );
    uint64_t ring_capacity = std_ring_capacity ( device_ring );

    aud_device_info_t device_info;
    aud->get_device_info ( &device_info, onda_state->client.device );

    onda_client_decode_init ( stream_state );

    wm_i* wm = onda_state->client.wm;
    uint64_t step_ms = 50;
    std_tick_t frame_tick = std_tick_now();
    wm_input_state_t old_input_state;
    wm->get_window_input_state ( onda_state->client.window, &old_input_state );
    while ( true ) {
        // input
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
            return false;
        }
        if ( new_input_state.keyboard[wm_keyboard_state_alt_left_m] ) {
            if ( new_input_state.keyboard[wm_keyboard_state_right_m] && !old_input_state.keyboard[wm_keyboard_state_right_m] ) {
                if ( new_input_state.keyboard[wm_keyboard_state_shift_left_m] ) {
                    aud->skip_source ( source, 60 * 60 * 12 );
                } else {
                    aud->skip_source ( source, 10 );
                }
            }
            if ( new_input_state.keyboard[wm_keyboard_state_left_m] && !old_input_state.keyboard[wm_keyboard_state_left_m] ) {
                if ( new_input_state.keyboard[wm_keyboard_state_shift_left_m] ) {
                    aud->skip_source ( source, -60 * 60 * 12 );
                } else {
                    aud->skip_source ( source, -10 );
                }
            }
            if ( new_input_state.keyboard[wm_keyboard_state_up_m] && !old_input_state.keyboard[wm_keyboard_state_up_m] ) {
                onda_state->client.volume = std_min_f32 ( 1, onda_state->client.volume + 0.05 );
                aud->set_source_volume ( source, onda_state->client.volume );
            }
            if ( new_input_state.keyboard[wm_keyboard_state_down_m] && !old_input_state.keyboard[wm_keyboard_state_down_m] ) {
                onda_state->client.volume = std_max_f32 ( 0, onda_state->client.volume - 0.05 );
                aud->set_source_volume ( source, onda_state->client.volume );
            }
        }

        old_input_state = new_input_state;

        // time
        std_tick_t new_tick = std_tick_now();
        float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );
        if ( delta_ms < step_ms / 2 ) {
            std_thread_this_sleep ( sleep_ms );
            continue;
        }
        
        frame_tick = new_tick;

        // feed
#if 1
        onda_client_feed_stream ( source, stream_state );
#else
        std_stack_t* read_stack = &onda_state->read_stack;
        if ( stream_state->read_size < stream_state->total_size ) {
            onda_client_read_media_chunk ( read_stack, stream_state );
            int16_t* buffer = std_virtual_heap_alloc_array_m ( int16_t, source_info.sample_count );
            mp3dec_ex_t mp3dec;
            mp3dec_ex_open_buf ( &mp3dec, stream_state->begin, stream_state->read_size, MP3D_SEEK_TO_SAMPLE );
            uint64_t samples_read = mp3dec_ex_read ( &mp3dec, buffer, mp3dec.samples );
            std_verify_m ( samples_read == mp3dec.samples );
            aud->feed_source ( source, buffer, sizeof ( int16_t ) * mp3dec.samples );
            std_virtual_heap_free ( buffer );
            stream_state->consumed_size = stream_state->read_size;
        }
#endif

        // print
        aud_source_info_t source_info;
        aud->get_source_info ( &source_info, source );
        float total_duration = source_info.total_time;
        float bar_tick = total_duration * 1000.f / 60.f;

        aud->update_device_ring ( onda_state->client.device );
        uint64_t ring_count = std_ring_count ( device_ring );

        {
            char bar[100] = { 0 };
            size_t i = 0;

            bar[i++] = '[';

            uint64_t ms = ( uint64_t ) ( source_info.time_played * 1000.f );

            while ( ms > bar_tick && i < 60 ) {
                bar[i++] = '=';
                ms -= bar_tick;
            }

            uint64_t streamed_ms = ( uint64_t ) ( ( float ) stream_state->read_size / stream_state->total_size * total_duration * 1000.f );

            while ( streamed_ms > bar_tick && i < 60 ) {
                bar[i++] = '-';
                streamed_ms -= bar_tick;
            }

            while ( i < 60 ) {
                bar[i++] = ' ';
            }

            bar[i++] = ']';

            std_log_m ( std_log_level_custom_m, std_fmt_clear_m std_fmt_clear_m std_fmt_str_m "\n" std_fmt_str_m " " std_fmt_u64_pad_m(2) "/" std_fmt_u64_m " | " std_fmt_f32_dec_m(2), 
                label, bar, ring_count, ring_capacity, onda_state->client.volume );
        }

        // output
        if ( source_info.time_played < total_duration ) {
            if ( ring_count < ring_capacity ) {
                aud->output_to_device ( onda_state->client.device, step_ms );
            }
        } else if ( ring_count == 0 && stream_state->read_size >= stream_state->total_size ) {
            break;
        }
        
        std_thread_this_sleep ( sleep_ms );
    }

    return true;
}

static void onda_client_print_list ( std_stack_t* stack, const list_result_t* list ) {
    void* begin = stack->top;
    std_stack_string_copy_format ( stack, std_fmt_u32_m " dirs\n", list->subdir_count );
    for ( uint32_t i = 0; i < list->subdir_count; ++i ) {
        std_stack_string_append_format ( stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, list->subdirs[i] );
    }

    std_stack_string_append_format ( stack, std_fmt_u32_m " files\n", list->file_count );
    for ( uint32_t i = 0; i < list->file_count; ++i ) {
        std_stack_string_append_format ( stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, list->files[i] );
    }

    std_log_m ( std_log_level_custom_m, begin );
}

static void onda_client_request_list ( std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;
    onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
    void* base = stack->top;
    std_stack_string_copy ( stack, "list ");
    char* dir_path = std_stack_string_append ( stack, onda_state->client.path );
    std_stack_alloc ( stack, std_str_len ( path ) + 1 );
    std_path_append_dir ( dir_path, (char*) stack->top - dir_path, path );
    uint32_t data_size = stack->top - base + 1;
    request->data_size = data_size;
    net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + data_size );
}

static bool onda_client_play_media ( std_stack_t* stack, const char* path ) {
    net_i* net = onda_state->net;

    onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
    char* request_data = std_stack_string_copy ( stack, "play " );
    char* media_path = std_stack_string_append ( stack, onda_state->client.path );
    std_stack_alloc ( stack, std_str_len ( path ) );
    std_path_append_file ( media_path, (char*) stack->top - media_path, path );
    uint32_t request_data_size =  std_str_len ( request_data ) + 1;
    request->data_size = request_data_size;
    net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + request_data_size );

    aud_i* aud = onda_state->client.aud;

    bool exit = false;

#if 0
    onda_media_header_t* media_header = onda_client_read_media ( read_stack );
    uint32_t label_size = media_header->label_size;
    uint32_t media_size = media_header->media_size;

    char* label = media_header->data;
    void* data = label + label_size;

    aud_source_h source = create_source ( label, data, media_size );

    if ( source != aud_null_handle_m ) {
        if ( !onda_client_play_source ( source, label ) ) {
            exit = true;
        }
        aud->destroy_source ( source );
    }
#else

    onda_client_stream_state_t stream_state;
    onda_media_header_t* media_header = onda_client_read_media_header ( stack, &stream_state );
    //uint32_t label_size = media_header->label_size;
    //uint32_t media_size = media_header->media_size;

    char* label = media_header->data;
    //void* data = label + label_size;

    aud_source_h source = create_stream_source ( &stream_state, media_header );

    if ( source != aud_null_handle_m ) {
        if ( !onda_client_stream_source ( source, label, &stream_state ) ) {
            // TODO flush remaining stream from socket here
            exit = true;
        }
        aud->destroy_source ( source );
    }
#endif

    return exit;
}

static std_app_state_e onda_update_client ( void ) {
    net_i* net = onda_state->net;

    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );
    
    char msg_buffer[32];
    char* msg = msg_buffer;
    size_t msg_size = 0;
    std_process_io_read ( msg, &msg_size, sizeof ( msg_buffer ) - 1, process_info.io.stdin_handle );
    msg[msg_size++] = '\0';

    std_stack_t* stack = &onda_state->stack;
    std_stack_clear ( stack );

    if ( std_str_cmp ( msg, "list" ) == 0 ) {
        //onda_client_request_t* request = std_stack_alloc_m ( write_stack, onda_client_request_t );
        //request->data_size = msg_size;
        //std_stack_alloc ( write_stack, msg_size );
        //std_str_copy ( request->data, msg_size, msg );
        //net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + msg_size );
        //onda_state->client.list = onda_client_read_list ( perm_stack, temp_stack );
        onda_client_request_list ( stack, "" );
        list_result_t list = onda_client_read_list ( stack );
        onda_state->client.list = list;
        onda_client_print_list ( stack, &list );
    } else if ( std_str_starts_with ( msg, "open" ) ) {
        uint32_t id = std_str_to_u32 ( msg + 5 );
        list_result_t* client_list = &onda_state->client.list;
        std_assert_m ( id < client_list->subdir_count + client_list->file_count );
        if ( id < client_list->subdir_count ) {
            std_path_append_dir ( onda_state->client.path, std_path_size_m, client_list->subdirs[id] );
        } else {
            std_path_append_file ( onda_state->client.path, std_path_size_m, client_list->files[id - client_list->subdir_count] );
        }
        onda_client_request_list ( stack, "" );
        list_result_t list = onda_client_read_list ( stack );
        onda_state->client.list = list;
        onda_client_print_list ( stack, &list );
    } else if ( std_str_cmp ( msg, "back" ) == 0 ) {
        std_path_pop ( onda_state->client.path );
        onda_client_request_list ( stack, "" );
        list_result_t list = onda_client_read_list ( stack );
        onda_state->client.list = list;
        onda_client_print_list ( stack, &list );
    } else if ( std_str_cmp ( msg, "exit" ) == 0 ) {
        onda_client_request_t* request = std_stack_alloc_m ( stack, onda_client_request_t );
        request->data_size = msg_size;
        std_stack_string_copy ( stack, msg );
        net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + msg_size );
        net->destroy_socket ( onda_state->socket );
        return std_app_state_exit_m;
    } else if ( std_str_starts_with ( msg, "play" ) ) {
#if 0
        onda_client_request_t* request = std_stack_alloc_m ( write_stack, onda_client_request_t );
        msg = std_stack_string_copy ( write_stack, "play " );
        //char* media_path = std_stack_alloc ( write_stack, std_path_size_m );
        //std_str_a ( media_path, std_path_size_m, onda_state->client.path );
        char* media_path = std_stack_string_append ( write_stack, onda_state->client.path );
        list_result_t* client_list = &onda_state->client.list;
        uint32_t id = std_str_to_u32 ( msg + 5 );
        std_assert_m ( id < client_list->subdir_count + client_list->file_count );
        if ( id < client_list->subdir_count ) {
            std_assert_m ( false ); // TODO
            //std_path_append_dir ( media_path, std_path_size_m, client_list->subdirs[id] );
        } else {
            char* file = client_list->files[id - client_list->subdir_count];
            std_stack_alloc ( write_stack, std_str_len ( file ) );
            std_path_append_file ( media_path, (char*) write_stack->top - media_path, file );
        }
        msg_size =  std_str_len ( msg ) + 1;
        request->data_size = msg_size;
        net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + msg_size );

        aud_i* aud = onda_state->client.aud;

        bool exit = false;

        //onda_playlist_header_t* playlist_header = onda_client_read_playlist_header ( read_stack );
        uint32_t media_count = 1;//playlist_header->media_count;
        uint32_t media_it = 0;
        for ( ; media_it < media_count; ++media_it ) {
            if ( exit ) {
                break;
            }

#if 0
            onda_media_header_t* media_header = onda_client_read_media ( read_stack );
            uint32_t label_size = media_header->label_size;
            uint32_t media_size = media_header->media_size;

            char* label = media_header->data;
            void* data = label + label_size;

            aud_source_h source = create_source ( label, data, media_size );

            if ( source != aud_null_handle_m ) {
                if ( !onda_client_play_source ( source, label ) ) {
                    exit = true;
                }
                aud->destroy_source ( source );
            }
#else

            onda_client_stream_state_t stream_state;
            onda_media_header_t* media_header = onda_client_read_media_header ( read_stack, &stream_state );
            //uint32_t label_size = media_header->label_size;
            //uint32_t media_size = media_header->media_size;

            char* label = media_header->data;
            //void* data = label + label_size;

            aud_source_h source = create_stream_source ( &stream_state, media_header );

            if ( source != aud_null_handle_m ) {
                //if ( !onda_client_play_source ( source, label ) ) {
                if ( !onda_client_stream_source ( source, label, &stream_state ) ) {
                    exit = true;
                }
                aud->destroy_source ( source );
            }
#endif
            std_stack_clear ( read_stack );
        }
#else
        list_result_t* client_list = &onda_state->client.list;
        uint32_t id = std_str_to_u32 ( msg + 5 );
        std_assert_m ( id < client_list->subdir_count + client_list->file_count );
        std_log_m ( std_log_level_custom_m, "\n\n" ); // make room for 2 empty lines for the player
        if ( id < client_list->subdir_count ) {
            std_path_append_dir ( onda_state->client.path, std_path_size_m, client_list->subdirs[id] );
            onda_client_request_list ( stack, "" );
            list_result_t subdir_list = onda_client_read_list ( stack );
            for ( uint32_t i = 0; i < subdir_list.file_count; ++i ) {
                bool exit = onda_client_play_media ( stack, subdir_list.files[i] );
                if ( exit ) break;
            }
            std_path_pop ( onda_state->client.path );
        } else {
            char* file = client_list->files[id - client_list->subdir_count];
            onda_client_play_media ( stack, file );
        }
#endif
    } else {
        std_log_error_m ( "Bad input" );
        return std_app_state_exit_m;
    }

#if 0
    if ( std_str_cmp ( msg, "list" ) == 0 
        || std_str_starts_with ( msg, "open" ) 
        || std_str_cmp ( msg, "back" ) == 0 
        || std_str_starts_with ( msg, "play" ) 
        || std_str_cmp ( msg, "exit" ) == 0 
    ) {
        onda_client_request_t* request = std_stack_alloc_m ( write_stack, onda_client_request_t );
        request->data_size = msg_size;
        std_stack_alloc ( write_stack, msg_size );
        std_str_copy ( request->data, msg_size, msg );
        net->write_connected_socket ( onda_state->socket, request, sizeof ( onda_client_request_t ) + msg_size );
        std_stack_clear ( write_stack );
    }

    if ( std_str_cmp ( msg, "list" ) == 0 || std_str_starts_with ( msg, "open" ) || std_str_cmp ( msg, "back" ) == 0  ) {
        list_result_t list = onda_client_read_list ( read_stack, temp_stack );
        std_stack_string_append_format ( write_stack, std_fmt_u32_m " dirs\n", list.subdir_count );
        for ( uint32_t i = 0; i < list.subdir_count; ++i ) {
            std_stack_string_append_format ( write_stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, list.subdirs[i] );
        }

        std_stack_string_append_format ( write_stack, std_fmt_u32_m " files\n", list.file_count );
        for ( uint32_t i = 0; i < list.file_count; ++i ) {
            std_stack_string_append_format ( write_stack, std_fmt_tab_m std_fmt_u32_m " - " std_fmt_str_m "\n", i, list.files[i] );
        }

        std_log_info_m ( write_stack->begin );
    } else if ( std_str_starts_with ( msg, "play" ) ) {
        aud_i* aud = onda_state->client.aud;

        bool exit = false;

        onda_playlist_header_t* playlist_header = onda_client_read_playlist_header ( read_stack );
        uint32_t media_count = playlist_header->media_count;
        uint32_t media_it = 0;
        for ( ; media_it < media_count; ++media_it ) {
            if ( exit ) {
                break;
            }

#if 0
            onda_media_header_t* media_header = onda_client_read_media ( read_stack );
            uint32_t label_size = media_header->label_size;
            uint32_t media_size = media_header->media_size;

            char* label = media_header->data;
            void* data = label + label_size;

            aud_source_h source = create_source ( label, data, media_size );

            if ( source != aud_null_handle_m ) {
                if ( !onda_client_play_source ( source, label ) ) {
                    exit = true;
                }
                aud->destroy_source ( source );
            }
#else

            onda_client_stream_state_t stream_state;
            onda_media_header_t* media_header = onda_client_read_media_header ( read_stack, &stream_state );
            //uint32_t label_size = media_header->label_size;
            //uint32_t media_size = media_header->media_size;

            char* label = media_header->data;
            //void* data = label + label_size;

            aud_source_h source = create_stream_source ( &stream_state, media_header );

            if ( source != aud_null_handle_m ) {
                //if ( !onda_client_play_source ( source, label ) ) {
                if ( !onda_client_stream_source ( source, label, &stream_state ) ) {
                    exit = true;
                }
                aud->destroy_source ( source );
            }
#endif
            std_stack_clear ( read_stack );
        }

        if ( exit ) {
            // flush
            for ( ; media_it < media_count; ++media_it ) {
                onda_client_read_media ( read_stack );
                std_stack_clear ( read_stack );
            }
        }
    } else if ( std_str_cmp ( msg, "exit" ) == 0 ) {
        net->destroy_socket ( onda_state->socket );
        return std_app_state_exit_m;
    } else {
        std_log_error_m ( "Bad input" );
        return std_app_state_exit_m;
    }
#endif

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
    // TODO
    std_module_unload_m ( net_module_name_m );
    if ( !onda_state->is_server ) {
        std_module_unload_m ( aud_module_name_m );
        std_module_unload_m ( wm_module_name_m );
    }
    onda_state_free();
}

std_module_export_m void onda_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );
    std_auto_m state = ( onda_state_t* ) api;
    state->api.tick = onda_tick;
    onda_state_bind ( state );
}
