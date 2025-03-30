#include "xg_debug_capture.h"

#include <std_platform.h>
#include <std_log.h>

std_warnings_save_state_m()
#if defined ( std_compiler_clang_m )
std_warnings_ignore_m ( "-Wmicrosoft-enum-value" )
#endif
std_warnings_ignore_m ( "-Wstrict-prototypes" )

#include "renderdoc_app.h"

std_warnings_restore_state_m()

static xg_debug_capture_state_t* xg_debug_capture_state;

void xg_debug_capture_load ( xg_debug_capture_state_t* state ) {
    xg_debug_capture_state = state;

#if defined(std_platform_win32_m)
    HMODULE mod = GetModuleHandleA ( "renderdoc.dll" );

    if ( mod ) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = ( pRENDERDOC_GetAPI ) GetProcAddress ( mod, "RENDERDOC_GetAPI" );
        int ret = RENDERDOC_GetAPI ( eRENDERDOC_API_Version_1_5_0, ( void** ) &xg_debug_capture_state->renderdoc_api );
        std_verify_m ( ret == 1 );
    }

#elif defined(std_platform_linux_m)
    void* mod = dlopen ( "librenderdoc.so", RTLD_LAZY );

    if ( mod ) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = ( pRENDERDOC_GetAPI ) dlsym ( mod, "RENDERDOC_GetAPI" );
        int ret = RENDERDOC_GetAPI ( eRENDERDOC_API_Version_1_5_0, ( void** ) &xg_debug_capture_state->renderdoc_api );
        std_verify_m ( ret == 1 );

        ( ( RENDERDOC_API_1_5_0* ) xg_debug_capture_state->renderdoc_api )->SetCaptureFilePathTemplate ( std_pp_eval_string_m ( std_binding_renderdoc_captures_m ) );
    }

#endif
}

void xg_debug_capture_reload ( xg_debug_capture_state_t* state ) {
    xg_debug_capture_state = state;
}

void xg_debug_capture_unload ( void ) {
    std_noop_m;
}

static RENDERDOC_API_1_5_0* xg_debug_capture_get_api ( void ) {
    return ( RENDERDOC_API_1_5_0* ) xg_debug_capture_state->renderdoc_api;
}

static bool xg_debug_capture_is_available ( void ) {
    return xg_debug_capture_state->renderdoc_api != NULL;
}

void xg_debug_capture_start ( void ) {
    if ( !xg_debug_capture_is_available() ) {
        std_log_warn_m ( "Debug capture not available" );
        return;
    }
    xg_debug_capture_get_api()->StartFrameCapture ( NULL, NULL );
}

void xg_debug_capture_stop ( void ) {
    if ( !xg_debug_capture_is_available() ) {
        std_log_warn_m ( "Debug capture not available" );
        return;
    }
    xg_debug_capture_get_api()->EndFrameCapture ( NULL, NULL );
}
