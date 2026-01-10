#pragma once

#include <std_module.h>

std_module_export_m void* memview_load ( void* );
std_module_export_m void memview_unload ( void );
std_module_export_m void memview_reload ( void*, void* );
