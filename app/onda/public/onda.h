#pragma once

#include <std_module.h>

#define viewer_app_module_name_m onda
std_module_export_m void* onda_load ( void* );
std_module_export_m void onda_unload ( void );
std_module_export_m void onda_reload ( void*, void* );
