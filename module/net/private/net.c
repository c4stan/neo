#include <net.h>

#include <std_log.h>

#include "net_platform.h"
#include "net_socket.h"
#include "net_address.h"

typedef struct {
    net_i api;
    net_socket_state_t socket;
} net_state_t;

std_module_implement_state_m ( net );

static void net_api_init ( net_i* net ) {
    net->create_socket = net_socket_create;
    net->destroy_socket = net_socket_destroy;

    net->bind_socket = net_socket_bind_address;
    net->connect_socket = net_socket_connect;
    net->listen_for_connections = net_socket_listen_for_connections;
    net->accept_pending_connection = net_socket_accept_pending_connection;

    net->set_socket_is_blocking = net_socket_set_is_blocking;

    net->get_socket_available_read_size = net_socket_get_available_read_size;
    net->read_connected_socket = net_socket_read_connected;
    net->write_connected_socket = net_socket_write_connected;
    net->read_socket = net_socket_read;
    net->write_socket = net_socket_write;

    net->ip_string_to_bytes = net_address_ip_string_to_bytes;
    net->ip_bytes_to_string = net_address_ip_bytes_to_string;
}

void* net_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    net_state_t* state = net_state_alloc();

    net_platform_init();
    net_socket_load ( &state->socket );

    net_api_init ( &state->api );
    return &state->api;
}

void net_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    net_state_t* state = ( net_state_t* ) api;
    net_state_bind ( state );

    net_socket_reload ( &state->socket );

    net_api_init ( &state->api );
}

void net_unload ( void ) {
    net_socket_unload();
    net_platform_shutdown();

    net_state_free();
}
