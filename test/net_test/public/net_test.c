#include <std_main.h>

#include <net.h>

#include <stdio.h>
#include <stdlib.h>

#include <std_log.h>
#include <std_string.h>

std_warnings_ignore_m ( "-Wunused-variable" )
std_warnings_ignore_m ( "-Wunused-function" )

static void test_udp_msg ( void ) {
    net_i* net = std_module_get_m ( net_module_name_m );
    {
        net_socket_h s1;
        net_socket_address_t s1_address;
        net_socket_h s2;
        net_socket_address_t s2_address;

        {
            net_socket_params_t socket_params;
            socket_params.family = net_address_family_ip4_m;
            socket_params.protocol = net_ip_protocol_udp_m;
            socket_params.is_blocking = true;
            s1 = net->create_socket ( &socket_params );

            net->ip_string_to_bytes ( &s1_address.ip, "127.0.0.1", net_address_family_ip4_m );
            s1_address.port = 666;
            net->bind_socket ( s1, &s1_address );
        }

        {
            net_socket_params_t socket_params;
            socket_params.family = net_address_family_ip4_m;
            socket_params.protocol = net_ip_protocol_udp_m;
            socket_params.is_blocking = true;
            s2 = net->create_socket ( &socket_params );

            net->ip_string_to_bytes ( &s2_address.ip, "127.0.0.1", net_address_family_ip4_m );
            s2_address.port = 999;
            net->bind_socket ( s2, &s2_address );
        }

        char* msg = "hello world";
        size_t msg_size = std_str_len ( msg ) + 1;
        std_log_info_m ( "Sending string '" std_fmt_str_m "' to address 127.0.0.1:" std_fmt_u16_m"...", msg, s2_address.port );
        size_t write_size = net->write_socket ( s1, &s2_address, msg, msg_size );
        std_assert_m ( write_size == msg_size );

        net_socket_address_t read_address;
        char buffer[32];
        size_t read_size = net->read_socket ( &read_address, buffer, sizeof ( buffer ), s2 );
        std_assert_m ( read_size == msg_size );
        std_log_info_m ( "Received string '" std_fmt_str_m "' from address 127.0.0.1:" std_fmt_u16_m, buffer, read_address.port );

        net->destroy_socket ( s1 );
        net->destroy_socket ( s2 );
    }
}

typedef struct {
    net_socket_h socket;
} test_tcp_msg_t1_args_t;

static void test_tcp_msg_t1 ( void* _args ) {
    net_i* net = std_module_get_m ( net_module_name_m );

    test_tcp_msg_t1_args_t* args = ( test_tcp_msg_t1_args_t* ) _args;
    net_socket_h server_socket = args->socket;

    net->listen_for_connections ( server_socket );

    net_socket_address_t client_address;
    net_socket_h client_socket = net->accept_pending_connection ( &client_address, server_socket );
    char buffer[32];
    net->ip_bytes_to_string ( buffer, &client_address.ip, net_address_family_ip4_m );

    char* msg = "hello world";
    size_t msg_size = std_str_len ( msg ) + 1;
    std_log_info_m ( "Sending string '" std_fmt_str_m "' to address " std_fmt_str_m ":" std_fmt_u16_m"...", msg, buffer, client_address.port );
    net->write_connected_socket ( client_socket, msg, msg_size );
}

static void test_tcp_msg ( void ) {
    net_i* net = std_module_get_m ( net_module_name_m );
    {
        net_socket_h s1;
        net_socket_address_t s1_address;
        net_socket_h s2;
        net_socket_address_t s2_address;

        {
            net_socket_params_t socket_params;
            socket_params.family = net_address_family_ip4_m;
            socket_params.protocol = net_ip_protocol_tcp_m;
            socket_params.is_blocking = true;
            s1 = net->create_socket ( &socket_params );

            net->ip_string_to_bytes ( &s1_address.ip, "127.0.0.1", net_address_family_ip4_m );
            s1_address.port = 666;
            net->bind_socket ( s1, &s1_address );
        }

        {
            net_socket_params_t socket_params;
            socket_params.family = net_address_family_ip4_m;
            socket_params.protocol = net_ip_protocol_tcp_m;
            socket_params.is_blocking = true;
            s2 = net->create_socket ( &socket_params );

            net->ip_string_to_bytes ( &s2_address.ip, "127.0.0.1", net_address_family_ip4_m );
            s2_address.port = 999;
            net->bind_socket ( s2, &s2_address );
        }

        test_tcp_msg_t1_args_t thread_args;
        thread_args.socket = s1;
        std_thread_h thread = std_thread ( test_tcp_msg_t1, &thread_args, "server", std_thread_core_mask_any_m );

        net->connect_socket ( s2, &s1_address );
        char buffer[32];
        size_t read_size = net->read_connected_socket ( buffer, sizeof ( buffer ), s2 );
        std_log_info_m ( "Received string '" std_fmt_str_m "' from address 127.0.0.1:" std_fmt_u16_m, buffer, s1_address.port );

        net->destroy_socket ( s1 );
        net->destroy_socket ( s2 );

        std_assert_m ( std_thread_join ( thread ) );
    }
}

#if 0
void test_http_server ( void ) {
    net_i* net = std_module_get_m ( net_module_name_m );

    net_socket_h server_socket;
    net_socket_address_t server_address;

    {
        net_socket_params_t socket_params;
    }
}
#endif

void std_main ( void ) {
    std_module_load_m ( net_module_name_m );
#if 1
    test_udp_msg();
    test_tcp_msg();
#else
    test_http_server();
#endif
    std_log_info_m ( "NET_TEST COMPLETE!" );
}
