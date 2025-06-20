#define xs_shader_binding_set_workload_m    0
#define xs_shader_binding_set_pass_m        1
#define xs_shader_binding_set_material_m    2
#define xs_shader_binding_set_dispatch_m    3

////////
// Means that all uniform/storage matrix data is treated as row-majow (which is the default in C-like code)
// https://www.khronos.org/opengl/wiki/Interface_Block_(GLSL)#Matrix_storage_order
// NOTE: remember that in glsl
// - mat4x3 is a matrix with 4 columns and 3 rows
// - matrix constructors take in values in column major order
// - matrix[c] identifies the matrix column vector c
// - matrix[c][r] identifies the element in column c and row r
// and changing default storage layout to row major doesn't change these things.
layout ( row_major ) uniform;
layout ( row_major ) buffer;
