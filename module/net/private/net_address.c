#include "net_address.h"

#include <std_compiler.h>
#include <std_log.h>

bool net_address_ip_string_to_bytes ( net_address_bytes_t* bytes_address, const char* string_address, net_address_family_e family ) {
    bytes_address->u64[0] = 0;
    bytes_address->u64[1] = 0;

#if defined std_platform_win32_m
    INT result = InetPton ( net_address_family_to_winsock ( family ), string_address, bytes_address->bytes );
#elif defined std_platform_linux_m
    int result = inet_pton ( net_address_family_to_winsock ( family ), string_address, bytes_address->bytes );
#endif

    if ( result != 1 ) {
        return false;
    }

    return true;
}

bool net_address_ip_bytes_to_string ( char* string_address, const net_address_bytes_t* bytes_address, net_address_family_e family ) {
#if defined std_platform_win32_m
    const char* result = InetNtop ( net_address_family_to_winsock ( family ), bytes_address->bytes, string_address, net_address_string_size_m );
#elif defined std_platform_linux_m
    const char* result = inet_ntop ( net_address_family_to_winsock ( family ), bytes_address->bytes, string_address, net_address_string_size_m );
#endif

    if ( result == NULL ) {
        return false;
    }

    return true;
}

int net_address_family_to_winsock ( net_address_family_e family ) {
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

net_address_family_e net_address_family_from_winsock ( int family ) {
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
