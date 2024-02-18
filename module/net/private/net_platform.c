#include "net_platform.h"

#include <std_log.h>

void net_platform_init ( void ) {
    WORD wsa_version = MAKEWORD ( net_winsock_version_major_m, net_winsock_version_minor_m );
    WSADATA wsa_info;

    // Load WS2_32.DLL
    int error = WSAStartup ( wsa_version, &wsa_info );

    if ( error != 0 ) {
        std_log_error_m ( "WinSock WSAStartup call failed." );
    }

    int major = LOBYTE ( wsa_info.wVersion );
    int minor = HIBYTE ( wsa_info.wVersion );

    if ( major != net_winsock_version_major_m || minor != net_winsock_version_minor_m ) {
        std_log_warn_m ( "WinSock startup successful. WinSock supports lower version (" std_fmt_int_m "," std_fmt_int_m ") than what requested (" std_fmt_int_m "," std_fmt_int_m ").",
                       major, minor, net_winsock_version_major_m, net_winsock_version_minor_m );
    } else {
        major = LOBYTE ( wsa_info.wHighVersion );
        minor = HIBYTE ( wsa_info.wHighVersion );

        if ( major != net_winsock_version_major_m || minor != net_winsock_version_minor_m ) {
            std_log_info_m ( "WinSock startup successful. WinSock supports higher version (" std_fmt_int_m "," std_fmt_int_m ") than what requested (" std_fmt_int_m "," std_fmt_int_m ").",
                           major, minor, net_winsock_version_major_m, net_winsock_version_minor_m );
        } else {
            std_log_info_m ( "WinSock startup successful. Selected version (" std_fmt_int_m "," std_fmt_int_m ") is the highest supported by the system.",
                           net_winsock_version_major_m, net_winsock_version_minor_m );
        }
    }
}

void net_platform_shutdown ( void ) {
    int error = WSACleanup();

    if ( error != 0 ) {
        std_log_error_m ( "Winsock WSACleanup call failed." );
    }
}
