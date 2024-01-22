#pragma once

#include <std_module.h>

#define viewer_app_module_name_m viewer_app
std_module_export_m void* viewer_app_load ( void* );
std_module_export_m void viewer_app_unload ( void );
std_module_export_m void viewer_app_reload ( void*, void* );
