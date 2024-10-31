#pragma once

// ======================================================================================= //
//                                       V E C T O R
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
typedef union {
    float e[$SIZE];
    struct {
        $TYPE x;
#if $SIZE > 1
        $TYPE y;
#endif
#if $SIZE > 2
        $TYPE z;
#endif
#if $SIZE > 3
        $TYPE w;
#endif
    };
} sm_vec_$SIZE$PREFIX_t;

make <float, f, 3>

make <float, f, 4>

*/
// template generation begin
typedef union {
    float e[3];
    struct {
        float x;
#if 3 > 1
        float y;
#endif
#if 3 > 2
        float z;
#endif
#if 3 > 3
        float w;
#endif
    };
} sm_vec_3f_t;

typedef union {
    float e[4];
    struct {
        float x;
#if 4 > 1
        float y;
#endif
#if 4 > 2
        float z;
#endif
#if 4 > 3
        float w;
#endif
    };
} sm_vec_4f_t;
// template generation end

// ======================================================================================= //
//                                 C O N S T R U C T O R S
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX ( const $TYPE f[$SIZE] );

make <float, f, 3>
make <float, f, 4>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f ( const float f[3] );
sm_vec_4f_t sm_vec_4f ( const float f[4] );
// template generation end

// ======================================================================================= //
//                                  C O N V E R S I O N S
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, FROM, TO>
sm_vec_$TO$PREFIX_t sm_vec_$FROM$PREFIX_to_$TO$PREFIX ( sm_vec_$FROM$PREFIX_t vec );

make <float, f, 4, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_4f_to_3f ( sm_vec_4f_t vec );
// template generation end

sm_vec_4f_t sm_vec_3f_to_4f ( sm_vec_3f_t vec, float w );

// ======================================================================================= //
//                                          L E N
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
$TYPE sm_vec_$SIZE$PREFIX_len ( sm_vec_$SIZE$PREFIX_t vec );

make <float, f, 3>

*/
// template generation begin
float sm_vec_3f_len ( sm_vec_3f_t vec );
// template generation end

// ======================================================================================= //
//                                         N O R M
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX_norm ( sm_vec_$SIZE$PREFIX_t vec );

make <float, f, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f_norm ( sm_vec_3f_t vec );
// template generation end

// ======================================================================================= //
//                                          D O T
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
$TYPE sm_vec_$SIZE$PREFIX_dot ( sm_vec_$SIZE$PREFIX_t a, sm_vec_$SIZE$PREFIX_t b );

make <float, f, 3>
make <float, f, 4>

*/
// template generation begin
float sm_vec_3f_dot ( sm_vec_3f_t a, sm_vec_3f_t b );
float sm_vec_4f_dot ( sm_vec_4f_t a, sm_vec_4f_t b );
// template generation end

// ======================================================================================= //
//                                        C R O S S
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX_cross ( sm_vec_$SIZE$PREFIX_t a, sm_vec_$SIZE$PREFIX_t b );

make <float, f, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f_cross ( sm_vec_3f_t a, sm_vec_3f_t b );
// template generation end

// ======================================================================================= //
//                                          M U L
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX_mul ( sm_vec_$SIZE$PREFIX_t vec, $TYPE scale );

make <float, f, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f_mul ( sm_vec_3f_t vec, float scale );
// template generation end

// ======================================================================================= //
//                                          A D D
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX_add ( sm_vec_$SIZE$PREFIX_t a, sm_vec_$SIZE$PREFIX_t b );

make <float, f, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f_add ( sm_vec_3f_t a, sm_vec_3f_t b );
// template generation end

// ======================================================================================= //
//                                          S U B
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX_sub ( sm_vec_$SIZE$PREFIX_t a, sm_vec_$SIZE$PREFIX_t b );

make <float, f, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f_sub ( sm_vec_3f_t a, sm_vec_3f_t b );
// template generation end

// ======================================================================================= //
//                                          N E G
// ======================================================================================= //
/* template begin

def <TYPE, PREFIX, SIZE>
sm_vec_$SIZE$PREFIX_t sm_vec_$SIZE$PREFIX_neg ( sm_vec_$SIZE$PREFIX_t a );

make <float, f, 3>

*/
// template generation begin
sm_vec_3f_t sm_vec_3f_neg ( sm_vec_3f_t a );
// template generation end
