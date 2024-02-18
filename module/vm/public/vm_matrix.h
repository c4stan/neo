#pragma once

#if 0
// ======================================================================================= //
//                                    T R A N S F O R M
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, ROWS, COLS, SIZE>
void vm_matrix_$ROWSx$COLS$PREFIX_transform_$PREFIX$SIZE ( $TYPE* restrict result, $TYPE* restrict mat, $TYPE* restrict vec );

make <float, f, 4, 4, 3>

*/
// template generation begin
void vm_matrix_4x4f_transform_f3 ( float* restrict result, float* restrict mat, float* restrict vec );
// template generation end

void vm_matrix_4x4f_axis_rotation ( float* restrict result, const float* restrict axis, float radians );
#endif
