std_warnings_save_state_m()
std_warnings_ignore_m ( "-Wunused-function" )

// ======================================================================================= //
//                                         S S E 1
// ======================================================================================= //
#if VM_SSE > 0

VM_API vm_vec_4f_t vm_vec_4f_load ( const float* f4 ) {
    return _mm_load_ps ( f4 );
}

VM_API vm_vec_4f_t vm_vec_4f_load_unaligned ( const float* f4 ) {
    return _mm_loadu_ps ( f4 );
}

VM_API vm_vec_4f_t vm_vec_4f_load_xxxx ( float f ) {
    return _mm_load_ps1 ( &f );
}

VM_API vm_vec_4f_t vm_vec_4f_zero ( void ) {
    return _mm_setzero_ps ();
}

VM_API vm_vec_4f_t vm_vec_4f_load_reverse ( const float* f4 ) {
    return _mm_loadr_ps ( f4 );
}

VM_API vm_vec_4f_t vm_vec_4f_load_x_zero_ywz ( float f ) {
    return _mm_load_ss ( &f );
}

VM_API vm_vec_4f_t vm_vec_4f_set ( float x, float y, float z, float w ) {
    return _mm_setr_ps ( x, y, z, w );
}

VM_API vm_vec_4f_t vm_vec_4f_set_xxxx ( float x ) {
    return _mm_set1_ps ( x );
}

VM_API void vm_vec_4f_store ( float* f4, vm_vec_4f_t vec ) {
    _mm_store_ps ( f4, vec );
}

VM_API void vm_vec_4f_store_unaligned ( float* f4, vm_vec_4f_t vec ) {
    _mm_storeu_ps ( f4, vec );
}

VM_API void vm_vec_4f_store_xxxx ( float* f4, vm_vec_4f_t vec ) {
    _mm_store_ps1 ( f4, vec );
}

VM_API void vm_vec_4f_store_x ( float* f, vm_vec_4f_t vec ) {
    _mm_store_ss ( f, vec );
}

VM_API void vm_vec_4f_store_reverse ( float* f4, vm_vec_4f_t vec ) {
    _mm_storer_ps ( f4, vec );
}

VM_API vm_vec_4f_t vm_vec_4f_add ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_add_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_add_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_add_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_sub ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_sub_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_sub_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_sub_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_mul ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_mul_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_mul_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_mul_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_div ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_div_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_div_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_div_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_and ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_and_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_not_a_and_b ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_andnot_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_or ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_or_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_xor ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_xor_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_equal ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmpeq_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmpeq_ss ( xyzw, x___ );
}

VM_API int vm_vec_4f_compare_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_comieq_ss ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_greater ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmpgt_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_greater_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmpgt_ss ( xyzw, x___ );
}

VM_API int vm_vec_4f_compare_greater_x ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_comigt_ss ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_greater_equal ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmpge_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_greater_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmpge_ss ( xyzw, x___ );
}

VM_API int vm_vec_4f_compare_greater_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_comige_ss ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_lesser ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmplt_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_lesser_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmplt_ss ( xyzw, x___ );
}

VM_API int vm_vec_4f_compare_lesser_x ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_comilt_ss ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_lesser_equal ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmple_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_lesser_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmple_ss ( xyzw, x___ );
}

VM_API int vm_vec_4f_compare_lesser_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_comile_ss ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_not_equal ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmpneq_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_not_equal_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmpneq_ss ( xyzw, x___ );
}

VM_API int vm_vec_4f_compare_not_equal_x ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_comineq_ss ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_nan_any ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmpunord_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_nan_any_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmpunord_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_not_nan_both ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_cmpord_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_compare_not_nan_both_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_cmpord_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_min ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_min_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_min_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_min_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_max ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_max_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_max_x_move_yzw ( vm_vec_4f_t xyzw, vm_vec_4f_t x___ ) {
    return _mm_max_ss ( xyzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_move_x_yzw ( vm_vec_4f_t x___, vm_vec_4f_t _yzw ) {
    return _mm_move_ss ( _yzw, x___ );
}

VM_API vm_vec_4f_t vm_vec_4f_move_xy_xy ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_movelh_ps ( a, b );
}

VM_API vm_vec_4f_t vm_vec_4f_move_zw_zw ( vm_vec_4f_t a, vm_vec_4f_t b ) {
    return _mm_movehl_ps ( b, a );
}

VM_API uint8_t vm_vec_4f_mask ( vm_vec_4f_t vec ) {
    return _mm_movemask_ps ( vec );
}

#endif

std_warnings_restore_state_m()
