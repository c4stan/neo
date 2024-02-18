#pragma once

#include <std_platform.h>

// *****************************************************************************
// This is for the user to define. This is the user entry point.
// *****************************************************************************
void std_main ( void );

// *****************************************************************************
// This is the actual ANSI C main function that calls std_init and std_main.
// *****************************************************************************
void std_init ( int argc, char** argv );
void std_shutdown ( void );
size_t std_runtime_size ( void );
int main ( int argc, char** argv ) {
    std_init ( argc, argv );
    std_main();
    std_shutdown();
}
