#include <std_main.h>
#include <std_log.h>
#include <std_process.h>
#include <std_compiler.h>
#include <std_module.h>
#include <std_app.h>

//
// TODO make std_launcher completely independent from std, that way std becomes fully reloadable?
//      right now for the launcher the std code it uses doesn't update in the case where it gets changed and reloaded
//
void std_main ( void ) {
    const char* app_name;
    {
        std_process_info_t process_info;
        std_process_info ( &process_info, std_process_this() );

        // TODO "hide" args[0] from app?
        app_name = process_info.args[0];
    }

    std_log_info_m ( "Launching app " std_fmt_str_m, app_name );
    std_auto_m app = ( std_app_i* ) std_module_load ( app_name );

    for ( ;; ) {
        std_app_state_e state = app->tick();

        if ( std_unlikely_m ( state != std_app_state_tick_m ) ) {
            if ( state == std_app_state_reload_m ) {
                std_module_reload ( app_name );
            } else if ( state == std_app_state_reboot_m ) {
                app = ( std_app_i* ) std_module_reboot ( app_name );
            } else { 
                std_assert_m ( state == std_app_state_exit_m );
                break;
            }
        }
    }

    std_module_unload ( app_name );
}
