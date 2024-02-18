#include "net_address.h"

#include <std_compiler.h>
#include <std_log.h>

bool net_address_ip_string_to_bytes ( net_address_bytes_t* bytes_address, const char* string_address, net_address_family_e family ) {
    bytes_address->u64[0] = 0;
    bytes_address->u64[1] = 0;
    INT result = InetPton ( net_address_family_to_winsock ( family ), string_address, bytes_address->bytes );

    if ( result != 1 ) {
        return false;
    }

    return true;
}

bool net_address_ip_bytes_to_string ( char* string_address, const net_address_bytes_t* bytes_address, net_address_family_e family ) {
    const char* result = InetNtop ( net_address_family_to_winsock ( family ), bytes_address->bytes, string_address, net_address_string_size_m );

    if ( result == NULL ) {
        return false;
    }

    return true;
}

ADDRESS_FAMILY net_address_family_to_winsock ( net_address_family_e family ) {
    switch ( family ) {
        case net_address_family_ip4_m:
            return AF_INET;

        case net_address_family_ip6_m:
            return AF_INET6;

        default:
            std_log_error_m ( "Address family not supported" );
            return 0;
    }
}

net_address_family_e net_address_family_from_winsock ( ADDRESS_FAMILY family ) {
    switch ( family ) {
        case AF_INET:
            return net_address_family_ip4_m;

        case AF_INET6:
            return net_address_family_ip6_m;

        default:
            std_log_error_m ( "Address family not supported" );
            return 0;
    }
}
