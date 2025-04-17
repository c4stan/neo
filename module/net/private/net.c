#include <net.h>

#include <std_log.h>

#include "net_platform.h"
#include "net_socket.h"
#include "net_address.h"

static net_i s_api;

static net_i net_api ( void ) {
    net_i net = {0};

    net.create_socket = net_socket_create;
    net.destroy_socket = net_socket_destroy;

    net.bind_socket = net_socket_bind_address;
    net.connect_socket = net_socket_connect;
    net.listen_for_connections = net_socket_listen_for_connections;
    net.accept_pending_connection = net_socket_accept_pending_connection;

    net.get_socket_available_read_size = net_socket_get_available_read_size;
    net.read_connected_socket = read_connected_socket;
    net.write_connected_socket = write_connected_socket;
    net.read_socket = read_socket;
    net.write_socket = write_socket;

    net.ip_string_to_bytes = net_address_ip_string_to_bytes;
    net.ip_bytes_to_string = net_address_ip_bytes_to_string;

    return net;
}

void* net_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    net_platform_init();
    net_socket_init();

    s_api = net_api();

    return &s_api;
}

void net_unload ( void ) {
    net_platform_shutdown();
}
