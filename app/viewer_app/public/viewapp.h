#pragma once

#include <std_module.h>

std_module_export_m void* viewer_app_load ( void* );
std_module_export_m void viewer_app_unload ( void );
std_module_export_m void viewer_app_reload ( void*, void* );
