#include "wm_display.h"

#include <std_platform.h>
#include <std_compiler.h>
#include <std_log.h>
#include <std_hash.h>

// https://www.asawicki.info/news_1637_how_to_change_display_mode_using_winapi

#if defined(std_platform_linux_m)
    #include <X11/extensions/Xrandr.h>
#endif

size_t wm_display_get_count ( void ) {
#ifdef std_platform_win32_m
    size_t count = 0;

    DISPLAY_DEVICEW device;
    device.cb = sizeof ( device );

    size_t idx = 0;

    while ( EnumDisplayDevicesW ( NULL, ( DWORD ) idx, &device, 0 ) ) {
        if ( device.StateFlags & DISPLAY_DEVICE_ACTIVE ) {
            ++count;
        }

        ++idx;
    }

    return count;
#elif defined(std_platform_linux_m)
    // TODO open just one display for the whole X11 session and keep it open for the entire application lifetime
    Display* display = XOpenDisplay ( NULL );
    Window root = DefaultRootWindow ( display );
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent ( display, root );

    size_t count = 0;
    for ( uint32_t i = 0; i < resources->noutput; ++i ) {
        XRROutputInfo *output = XRRGetOutputInfo ( display, resources, resources->outputs[i] );

        if ( output ) {
            if ( output->connection == RR_Connected && output->crtc != None ) {
                ++count;
            }

            XRRFreeOutputInfo ( output );
        }
    }

    XRRFreeScreenResources ( resources );
    XCloseDisplay ( display );

    return count;
#endif
}

size_t wm_display_get ( wm_display_h* displays, size_t cap ) {
#ifdef std_platform_win32_m

    size_t display_count = 0;

    DWORD adapter_index = 0;
    DISPLAY_DEVICEW adapter;
    std_mem_zero_m ( &adapter );
    adapter.cb = sizeof ( adapter );

    while ( EnumDisplayDevicesW ( NULL, adapter_index, &adapter, 0 ) ) {
        if ( adapter.StateFlags & DISPLAY_DEVICE_ACTIVE ) {
            DWORD device_index = 0;
            DISPLAY_DEVICEW device;
            std_mem_zero_m ( &device );
            device.cb = sizeof ( device );

            while ( EnumDisplayDevicesW ( adapter.DeviceName, device_index, &device, 0 ) ) {
                uint64_t key_hash = std_hash_block_32_m ( device.DeviceKey, 256 );

                if ( display_count >= cap ) {
                    std_log_warn_m ( "Found display count higher than display limit!" );
                    return display_count;
                }

                displays[display_count++] = ( wm_display_h ) ( adapter_index | device_index << 16 | key_hash << 32 );

                std_mem_zero_m ( &device );
                device.cb = sizeof ( device );
                ++device_index;
            }
        }

        std_mem_zero_m ( &adapter );
        adapter.cb = sizeof ( adapter );
        ++adapter_index;
    }

    return display_count;
#elif defined(std_platform_linux_m)
    Display *display = XOpenDisplay ( NULL );
    Window root = DefaultRootWindow ( display );
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent ( display, root );

    size_t display_count = 0;
    for ( int i = 0; i < resources->noutput; ++i ) {
        RROutput output_id = resources->outputs[i];

        XRROutputInfo *output = XRRGetOutputInfo ( display, resources, output_id );
        if ( !output ) {
            continue;
        }

        if ( output->connection == RR_Connected && output->crtc != None ) {
            if ( display_count >= cap ) {
                std_log_warn_m ( "Found display count higher than display limit!" );
                XRRFreeOutputInfo ( output );
                XRRFreeScreenResources ( resources );
                XCloseDisplay ( display );
                return display_count;
            }

            displays[display_count++] = ( wm_display_h ) output_id;
        }

        XRRFreeOutputInfo ( output );
    }

    XRRFreeScreenResources ( resources );
    XCloseDisplay ( display );

    return display_count;
#endif
}

#if defined(std_platform_win32_m)
static bool wm_display_get_devices ( DISPLAY_DEVICEW* adapter, DISPLAY_DEVICEW* display, wm_display_h display_handle ) {
    size_t adapter_index = display_handle & 0xffff;
    size_t display_index = ( display_handle >> 16 ) & 0xffff;
    size_t key_hash = display_handle >> 32;

    std_assert_m ( adapter );
    std_mem_zero_m ( adapter );
    adapter->cb = sizeof ( *adapter );
    {
        BOOL result = EnumDisplayDevicesW ( NULL, ( DWORD ) adapter_index, adapter, 0 );

        if ( !result ) {
            std_log_warn_m ( "Invalid display handle" );
            return false;
        }
    }

    if ( display ) {
        std_mem_zero_m ( display );
        display->cb = sizeof ( *display );
        {
            BOOL result = EnumDisplayDevicesW ( adapter->DeviceName, ( DWORD ) display_index, display, 0 );
            std_verify_m ( result );
        }

        uint32_t display_key_hash = std_hash_block_32_m ( display->DeviceKey, 256 );

        if ( key_hash != display_key_hash || ( display->StateFlags & DISPLAY_DEVICE_ACTIVE ) == 0 ) {
            std_log_warn_m ( "Invalid display handle" );
            return false;
        }
    }

    return true;
}
#endif

bool wm_display_get_info ( wm_display_info_t* info, wm_display_h display_handle ) {
#if defined(std_platform_win32_m)
    DISPLAY_DEVICEW adapter;
    DISPLAY_DEVICEW device;

    if ( !wm_display_get_devices ( &adapter, &device, display_handle ) ) {
        return false;
    }

    {
        int result;
        result = WideCharToMultiByte ( CP_UTF8, 0, adapter.DeviceName, -1, info->adapter_id, sizeof ( info->adapter_id ), NULL, NULL );
        std_assert_m ( result );
        result = WideCharToMultiByte ( CP_UTF8, 0, adapter.DeviceString, -1, info->adapter_name, sizeof ( info->adapter_name ), NULL, NULL );
        std_assert_m ( result );
        result = WideCharToMultiByte ( CP_UTF8, 0, device.DeviceName, -1, info->display_id, sizeof ( info->display_id ), NULL, NULL );
        std_assert_m ( result );
        result = WideCharToMultiByte ( CP_UTF8, 0, device.DeviceString, -1, info->display_name, sizeof ( info->display_name ), NULL, NULL );
        std_assert_m ( result );
    }

    info->is_primary = adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE;

    {
        DEVMODEW display_settings;
        BOOL result = EnumDisplaySettingsW ( adapter.DeviceName, ENUM_CURRENT_SETTINGS, &display_settings );
        std_verify_m ( result );
        info->mode.bits_per_pixel = display_settings.dmBitsPerPel;
        info->mode.width = display_settings.dmPelsWidth;
        info->mode.height = display_settings.dmPelsHeight;
        info->mode.refresh_rate = display_settings.dmDisplayFrequency;
    }

    return true;
#elif defined(std_platform_linux_m)
    Display *display = XOpenDisplay ( NULL );
    Window root = DefaultRootWindow ( display );
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent ( display, root );

    RROutput output_id = display_handle & 0xffffffff;

    XRROutputInfo* output = XRRGetOutputInfo ( display, resources, output_id );
    std_assert_m ( output->connection == RR_Connected );
    std_assert_m ( output->crtc != None );

    std_str_copy_static_m ( info->display_id, "" );
    std_str_copy_static_m ( info->display_name, output->name );
    std_str_copy_static_m ( info->adapter_id, "" );
    std_str_copy_static_m ( info->adapter_name, "" );

    XRRCrtcInfo* crtc = XRRGetCrtcInfo ( display, resources, output->crtc );
    info->mode.width = crtc->width;
    info->mode.height = crtc->height;
    info->mode.refresh_rate = 0;

    for ( int i = 0; i < resources->nmode; ++i ) {
        XRRModeInfo *mode = &resources->modes[i];

        if ( mode->id == crtc->mode ) {
            info->mode.refresh_rate = mode->dotClock / ( mode->hTotal * mode->vTotal );
            info->mode.bits_per_pixel = DefaultDepth ( display, DefaultScreen ( display ) );
            break;
        }
    }

    RROutput primary_output = XRRGetOutputPrimary ( display, root );
    info->is_primary = ( primary_output == output_id );

    XRRFreeCrtcInfo ( crtc );
    XRRFreeOutputInfo ( output );
    XRRFreeScreenResources ( resources );
    XCloseDisplay ( display );

    return true;
#endif
}

size_t wm_display_get_modes_count ( wm_display_h display_handle ) {
#if defined(std_platform_win32_m)
    DISPLAY_DEVICEW adapter;

    if ( !wm_display_get_devices ( &adapter, NULL, display_handle ) ) {
        return 0;
    }

    DEVMODEW display_settings;
    size_t idx = 0;

    while ( EnumDisplaySettingsW ( adapter.DeviceName, ( DWORD ) idx, &display_settings ) ) {
        ++idx;
    }

    return idx;
#elif defined(std_platform_linux_m)
    Display* display = XOpenDisplay ( NULL );
    Window root = DefaultRootWindow ( display );
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent ( display, root );
    RROutput output_id = display_handle;
    XRROutputInfo* output = XRRGetOutputInfo ( display, resources, output_id );

    size_t count = ( size_t ) output->nmode;

    XRRFreeOutputInfo ( output );
    XRRFreeScreenResources ( resources );
    XCloseDisplay ( display );

    return count;
#endif
}

bool wm_display_get_modes ( wm_display_mode_t* modes, size_t cap, wm_display_h display_handle ) {
#if defined(std_platform_win32_m)
    DISPLAY_DEVICEW adapter;

    if ( !wm_display_get_devices ( &adapter, NULL, display_handle ) ) {
        return false;
    }

    DEVMODEW display_settings;
    size_t idx = 0;

    while ( EnumDisplaySettingsW ( adapter.DeviceName, ( DWORD ) idx, &display_settings ) ) {
        if ( idx >= cap ) {
            std_log_warn_m ( "Found display modes count higher than display modes limit!" );
            return false;
        }

        modes[idx].bits_per_pixel = display_settings.dmBitsPerPel;
        modes[idx].width = display_settings.dmPelsWidth;
        modes[idx].height = display_settings.dmPelsHeight;
        modes[idx].refresh_rate = display_settings.dmDisplayFrequency;

        ++idx;
    }

    return true;
#elif defined(std_platform_linux_m)
    Display* display = XOpenDisplay ( NULL );
    Window root = DefaultRootWindow ( display );
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent ( display, root );
    RROutput output_id = display_handle;
    XRROutputInfo* output = XRRGetOutputInfo ( display, resources, output_id );

    size_t idx = 0;
    for ( int i = 0; i < output->nmode; ++i ) {
        RRMode mode_id = output->modes[i];
        XRRModeInfo* mode = NULL;

        for ( int j = 0; j < resources->nmode; ++j ) {
            if ( resources->modes[j].id == mode_id ) {
                mode = &resources->modes[j];
                break;
            }
        }

        if ( !mode ) {
            continue;
        }

        if ( idx >= cap ) {
            std_log_warn_m ( "Found display modes count higher than display modes limit!" );
            XRRFreeOutputInfo ( output );
            XRRFreeScreenResources ( resources );
            XCloseDisplay ( display );
            return false;
        }

        modes[idx].bits_per_pixel = DefaultDepth ( display, DefaultScreen ( display ) );
        modes[idx].width = mode->width;
        modes[idx].height = mode->height;
        modes[idx].refresh_rate = mode->dotClock / ( mode->hTotal * mode->vTotal );

        ++idx;
    }

    XRRFreeOutputInfo ( output );
    XRRFreeScreenResources ( resources );
    XCloseDisplay ( display );

    return true;
#endif
}
