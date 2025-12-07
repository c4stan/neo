#pragma once

#include <std_module.h>

std_module_export_m void* onda_load ( void* );
std_module_export_m void onda_unload ( void );
std_module_export_m void onda_reload ( void*, void* );
