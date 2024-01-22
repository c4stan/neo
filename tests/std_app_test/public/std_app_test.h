#pragma once

#include <std_module.h>

#define std_app_test_module_name_m std_app
std_module_export_m void* std_app_test_load ( void* );
std_module_export_m void std_app_test_unload ( void );
std_module_export_m void std_app_test_reload ( void*, void* );
