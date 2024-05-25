#include "xg_enum.h"

/*
case xg_format_undefined_m:
case xg_format_r4g4_unorm_pack8_m:
case xg_format_r4g4b4a4_unorm_pack16_m:
case xg_format_b4g4r4a4_unorm_pack16_m:
case xg_format_r5g6b5_unorm_pack16_m:
case xg_format_b5g6r5_unorm_pack16_m:
case xg_format_r5g5b5a1_unorm_pack16_m:
case xg_format_b5g5r5a1_unorm_pack16_m:
case xg_format_a1r5g5b5_unorm_pack16_m:
case xg_format_r8_unorm_m:
case xg_format_r8_snorm_m:
case xg_format_r8_uscaled_m:
case xg_format_r8_sscaled_m:
case xg_format_r8_uint_m:
case xg_format_r8_sint_m:
case xg_format_r8_srgb_m:
case xg_format_r8g8_unorm_m:
case xg_format_r8g8_snorm_m:
case xg_format_r8g8_uscaled_m:
case xg_format_r8g8_sscaled_m:
case xg_format_r8g8_uint_m:
case xg_format_r8g8_sint_m:
case xg_format_r8g8_srgb_m:
case xg_format_r8g8b8_unorm_m:
case xg_format_r8g8b8_snorm_m:
case xg_format_r8g8b8_uscaled_m:
case xg_format_r8g8b8_sscaled_m:
case xg_format_r8g8b8_uint_m:
case xg_format_r8g8b8_sint_m:
case xg_format_r8g8b8_srgb_m:
case xg_format_b8g8r8_unorm_m:
case xg_format_b8g8r8_snorm_m:
case xg_format_b8g8r8_uscaled_m:
case xg_format_b8g8r8_sscaled_m:
case xg_format_b8g8r8_uint_m:
case xg_format_b8g8r8_sint_m:
case xg_format_b8g8r8_srgb_m:
case xg_format_r8g8b8a8_unorm_m:
case xg_format_r8g8b8a8_snorm_m:
case xg_format_r8g8b8a8_uscaled_m:
case xg_format_r8g8b8a8_sscaled_m:
case xg_format_r8g8b8a8_uint_m:
case xg_format_r8g8b8a8_sint_m:
case xg_format_r8g8b8a8_srgb_m:
case xg_format_b8g8r8a8_snorm_m:
case xg_format_b8g8r8a8_unorm_m:
case xg_format_b8g8r8a8_uscaled_m:
case xg_format_b8g8r8a8_sscaled_m:
case xg_format_b8g8r8a8_uint_m:
case xg_format_b8g8r8a8_sint_m:
case xg_format_b8g8r8a8_srgb_m:
case xg_format_a8b8g8r8_unorm_pack32_m:
case xg_format_a8b8g8r8_snorm_pack32_m:
case xg_format_a8b8g8r8_uscaled_pack32_m:
case xg_format_a8b8g8r8_sscaled_pack32_m:
case xg_format_a8b8g8r8_uint_pack32_m:
case xg_format_a8b8g8r8_sint_pack32_m:
case xg_format_a8b8g8r8_srgb_pack32_m:
case xg_format_a2r10g10b10_unorm_pack32_m:
case xg_format_a2r10g10b10_snorm_pack32_m:
case xg_format_a2r10g10b10_uscaled_pack32_m:
case xg_format_a2r10g10b10_sscaled_pack32_m:
case xg_format_a2r10g10b10_uint_pack32_m:
case xg_format_a2r10g10b10_sint_pack32_m:
case xg_format_a2b10g10r10_unorm_pack32_m:
case xg_format_a2b10g10r10_snorm_pack32_m:
case xg_format_a2b10g10r10_uscaled_pack32_m:
case xg_format_a2b10g10r10_sscaled_pack32_m:
case xg_format_a2b10g10r10_uint_pack32_m:
case xg_format_a2b10g10r10_sint_pack32_m:
case xg_format_r16_unorm_m:
case xg_format_r16_snorm_m:
case xg_format_r16_uscaled_m:
case xg_format_r16_sscaled_m:
case xg_format_r16_uint_m:
case xg_format_r16_sint_m:
case xg_format_r16_sfloat_m:
case xg_format_r16g16_unorm_m:
case xg_format_r16g16_snorm_m:
case xg_format_r16g16_uscaled_m:
case xg_format_r16g16_sscaled_m:
case xg_format_r16g16_uint_m:
case xg_format_r16g16_sint_m:
case xg_format_r16g16_sfloat_m:
case xg_format_r16g16b16_unorm_m:
case xg_format_r16g16b16_snorm_m:
case xg_format_r16g16b16_uscaled_m:
case xg_format_r16g16b16_sscaled_m:
case xg_format_r16g16b16_uint_m:
case xg_format_r16g16b16_sint_m:
case xg_format_r16g16b16_sfloat_m:
case xg_format_r16g16b16a16_unorm_m:
case xg_format_r16g16b16a16_snorm_m:
case xg_format_r16g16b16a16_uscaled_m:
case xg_format_r16g16b16a16_sscaled_m:
case xg_format_r16g16b16a16_uint_m:
case xg_format_r16g16b16a16_sint_m:
case xg_format_r16g16b16a16_sfloat_m:
case xg_format_r32_uint_m:
case xg_format_r32_sint_m:
case xg_format_r32_sfloat_m:
case xg_format_r32g32_uint_m:
case xg_format_r32g32_sint_m:
case xg_format_r32g32_sfloat_m:
case xg_format_r32g32b32_uint_m:
case xg_format_r32g32b32_sint_m:
case xg_format_r32g32b32_sfloat_m:
case xg_format_r32g32b32a32_uint_m:
case xg_format_r32g32b32a32_sint_m:
case xg_format_r32g32b32a32_sfloat_m:
case xg_format_r64_uint_m:
case xg_format_r64_sint_m:
case xg_format_r64_sfloat_m:
case xg_format_r64g64_uint_m:
case xg_format_r64g64_sint_m:
case xg_format_r64g64_sfloat_m:
case xg_format_r64g64b64_uint_m:
case xg_format_r64g64b64_sint_m:
case xg_format_r64g64b64_sfloat_m:
case xg_format_r64g64b64a64_uint_m:
case xg_format_r64g64b64a64_sint_m:
case xg_format_r64g64b64a64_sfloat_m:
case xg_format_b10g11r11_ufloat_pack32_m:
case xg_format_e5b9g9r9_ufloat_pack32_m:
case xg_format_d16_unorm_m:
case xg_format_x8_d24_unorm_pack32_m:
case xg_format_d32_sfloat_m:
case xg_format_s8_uint_m:
case xg_format_d16_unorm_s8_uint_m:
case xg_format_d24_unorm_s8_uint_m:
case xg_format_d32_sfloat_s8_uint_m:
case xg_format_bc1_rgb_unorm_block_m:
case xg_format_bc1_rgb_srgb_block_m:
case xg_format_bc1_rgba_unorm_block_m:
case xg_format_bc1_rgba_srgb_block_m:
case xg_format_bc2_unorm_block_m:
case xg_format_bc2_srgb_block_m:
case xg_format_bc3_unorm_block_m:
case xg_format_bc3_srgb_block_m:
case xg_format_bc4_unorm_block_m:
case xg_format_bc4_snorm_block_m:
case xg_format_bc5_unorm_block_m:
case xg_format_bc5_snorm_block_m:
case xg_format_bc6h_ufloat_block_m:
case xg_format_bc6h_sfloat_block_m:
case xg_format_bc7_unorm_block_m:
case xg_format_bc7_srgb_block_m:
case xg_format_etc2_r8g8b8_unorm_block_m:
case xg_format_etc2_r8g8b8_srgb_block_m:
case xg_format_etc2_r8g8b8a1_unorm_block_m:
case xg_format_etc2_r8g8b8a1_srgb_block_m:
case xg_format_etc2_r8g8b8a8_unorm_block_m:
case xg_format_etc2_r8g8b8a8_srgb_block_m:
case xg_format_eac_r11_unorm_block_m:
case xg_format_eac_r11_snorm_block_m:
case xg_format_eac_r11g11_unorm_block_m:
case xg_format_eac_r11g11_snorm_block_m:
case xg_format_astc_4_mx4_UNORM_BLOCK:
case xg_format_astc_4_mx4_SRGB_BLOCK:
case xg_format_astc_5_mx4_UNORM_BLOCK:
case xg_format_astc_5_mx4_SRGB_BLOCK:
case xg_format_astc_5_mx5_UNORM_BLOCK:
case xg_format_astc_5_mx5_SRGB_BLOCK:
case xg_format_astc_6_mx5_UNORM_BLOCK:
case xg_format_astc_6_mx5_SRGB_BLOCK:
case xg_format_astc_6_mx6_UNORM_BLOCK:
case xg_format_astc_6_mx6_SRGB_BLOCK:
case xg_format_astc_8_mx5_UNORM_BLOCK:
case xg_format_astc_8_mx5_SRGB_BLOCK:
case xg_format_astc_8_mx6_UNORM_BLOCK:
case xg_format_astc_8_mx6_SRGB_BLOCK:
case xg_format_astc_8_mx8_UNORM_BLOCK:
case xg_format_astc_8_mx8_SRGB_BLOCK:
case xg_format_astc_10_mx5_UNORM_BLOCK:
case xg_format_astc_10_mx5_SRGB_BLOCK:
case xg_format_astc_10_mx6_UNORM_BLOCK:
case xg_format_astc_10_mx6_SRGB_BLOCK:
case xg_format_astc_10_mx8_UNORM_BLOCK:
case xg_format_astc_10_mx8_SRGB_BLOCK:
case xg_format_astc_10_mx10_UNORM_BLOCK:
case xg_format_astc_10_mx10_SRGB_BLOCK:
case xg_format_astc_12_mx10_UNORM_BLOCK:
case xg_format_astc_12_mx10_SRGB_BLOCK:
case xg_format_astc_12_mx12_UNORM_BLOCK:
case xg_format_astc_12_mx12_SRGB_BLOCK:
*/

size_t xg_format_size ( xg_format_e format ) {
    switch ( format ) {
        case xg_format_undefined_m:
            return 0;

        case xg_format_r4g4_unorm_pack8_m:

        case xg_format_r8_unorm_m:
        case xg_format_r8_snorm_m:
        case xg_format_r8_uscaled_m:
        case xg_format_r8_sscaled_m:
        case xg_format_r8_uint_m:
        case xg_format_r8_sint_m:
        case xg_format_r8_srgb_m:
            return 1;

        case xg_format_r4g4b4a4_unorm_pack16_m:
        case xg_format_b4g4r4a4_unorm_pack16_m:
        case xg_format_r5g6b5_unorm_pack16_m:
        case xg_format_b5g6r5_unorm_pack16_m:
        case xg_format_r5g5b5a1_unorm_pack16_m:
        case xg_format_b5g5r5a1_unorm_pack16_m:
        case xg_format_a1r5g5b5_unorm_pack16_m:

        case xg_format_r8g8_unorm_m:
        case xg_format_r8g8_snorm_m:
        case xg_format_r8g8_uscaled_m:
        case xg_format_r8g8_sscaled_m:
        case xg_format_r8g8_uint_m:
        case xg_format_r8g8_sint_m:
        case xg_format_r8g8_srgb_m:

        case xg_format_r16_unorm_m:
        case xg_format_r16_snorm_m:
        case xg_format_r16_uscaled_m:
        case xg_format_r16_sscaled_m:
        case xg_format_r16_uint_m:
        case xg_format_r16_sint_m:
        case xg_format_r16_sfloat_m:
            return 2;

        case xg_format_r8g8b8_unorm_m:
        case xg_format_r8g8b8_snorm_m:
        case xg_format_r8g8b8_uscaled_m:
        case xg_format_r8g8b8_sscaled_m:
        case xg_format_r8g8b8_uint_m:
        case xg_format_r8g8b8_sint_m:
        case xg_format_r8g8b8_srgb_m:
        case xg_format_b8g8r8_unorm_m:
        case xg_format_b8g8r8_snorm_m:
        case xg_format_b8g8r8_uscaled_m:
        case xg_format_b8g8r8_sscaled_m:
        case xg_format_b8g8r8_uint_m:
        case xg_format_b8g8r8_sint_m:
        case xg_format_b8g8r8_srgb_m:
            return 3;

        case xg_format_r8g8b8a8_unorm_m:
        case xg_format_r8g8b8a8_snorm_m:
        case xg_format_r8g8b8a8_uscaled_m:
        case xg_format_r8g8b8a8_sscaled_m:
        case xg_format_r8g8b8a8_uint_m:
        case xg_format_r8g8b8a8_sint_m:
        case xg_format_r8g8b8a8_srgb_m:
        case xg_format_b8g8r8a8_snorm_m:
        case xg_format_b8g8r8a8_unorm_m:
        case xg_format_b8g8r8a8_uscaled_m:
        case xg_format_b8g8r8a8_sscaled_m:
        case xg_format_b8g8r8a8_uint_m:
        case xg_format_b8g8r8a8_sint_m:
        case xg_format_b8g8r8a8_srgb_m:
        case xg_format_a8b8g8r8_unorm_pack32_m:
        case xg_format_a8b8g8r8_snorm_pack32_m:
        case xg_format_a8b8g8r8_uscaled_pack32_m:
        case xg_format_a8b8g8r8_sscaled_pack32_m:
        case xg_format_a8b8g8r8_uint_pack32_m:
        case xg_format_a8b8g8r8_sint_pack32_m:
        case xg_format_a8b8g8r8_srgb_pack32_m:

        case xg_format_a2r10g10b10_unorm_pack32_m:
        case xg_format_a2r10g10b10_snorm_pack32_m:
        case xg_format_a2r10g10b10_uscaled_pack32_m:
        case xg_format_a2r10g10b10_sscaled_pack32_m:
        case xg_format_a2r10g10b10_uint_pack32_m:
        case xg_format_a2r10g10b10_sint_pack32_m:
        case xg_format_a2b10g10r10_unorm_pack32_m:
        case xg_format_a2b10g10r10_snorm_pack32_m:
        case xg_format_a2b10g10r10_uscaled_pack32_m:
        case xg_format_a2b10g10r10_sscaled_pack32_m:
        case xg_format_a2b10g10r10_uint_pack32_m:
        case xg_format_a2b10g10r10_sint_pack32_m:

        case xg_format_r16g16_unorm_m:
        case xg_format_r16g16_snorm_m:
        case xg_format_r16g16_uscaled_m:
        case xg_format_r16g16_sscaled_m:
        case xg_format_r16g16_uint_m:
        case xg_format_r16g16_sint_m:
        case xg_format_r16g16_sfloat_m:

        case xg_format_r32_uint_m:
        case xg_format_r32_sint_m:
        case xg_format_r32_sfloat_m:
            return 4;

        case xg_format_r16g16b16_unorm_m:
        case xg_format_r16g16b16_snorm_m:
        case xg_format_r16g16b16_uscaled_m:
        case xg_format_r16g16b16_sscaled_m:
        case xg_format_r16g16b16_uint_m:
        case xg_format_r16g16b16_sint_m:
        case xg_format_r16g16b16_sfloat_m:
            return 6;

        case xg_format_r16g16b16a16_unorm_m:
        case xg_format_r16g16b16a16_snorm_m:
        case xg_format_r16g16b16a16_uscaled_m:
        case xg_format_r16g16b16a16_sscaled_m:
        case xg_format_r16g16b16a16_uint_m:
        case xg_format_r16g16b16a16_sint_m:
        case xg_format_r16g16b16a16_sfloat_m:

        case xg_format_r32g32_uint_m:
        case xg_format_r32g32_sint_m:
        case xg_format_r32g32_sfloat_m:

        case xg_format_r64_uint_m:
        case xg_format_r64_sint_m:
        case xg_format_r64_sfloat_m:
            return 8;

        case xg_format_r32g32b32_uint_m:
        case xg_format_r32g32b32_sint_m:
        case xg_format_r32g32b32_sfloat_m:
            return 12;

        case xg_format_r32g32b32a32_uint_m:
        case xg_format_r32g32b32a32_sint_m:
        case xg_format_r32g32b32a32_sfloat_m:

        case xg_format_r64g64_uint_m:
        case xg_format_r64g64_sint_m:
        case xg_format_r64g64_sfloat_m:
            return 16;

        case xg_format_r64g64b64_uint_m:
        case xg_format_r64g64b64_sint_m:
        case xg_format_r64g64b64_sfloat_m:
            return 24;

        case xg_format_r64g64b64a64_uint_m:
        case xg_format_r64g64b64a64_sint_m:
        case xg_format_r64g64b64a64_sfloat_m:
            return 32;

        // TODO
        case xg_format_b10g11r11_ufloat_pack32_m:
        case xg_format_e5b9g9r9_ufloat_pack32_m:
        case xg_format_d16_unorm_m:
        case xg_format_x8_d24_unorm_pack32_m:
        case xg_format_d32_sfloat_m:
        case xg_format_s8_uint_m:
        case xg_format_d16_unorm_s8_uint_m:
        case xg_format_d24_unorm_s8_uint_m:
        case xg_format_d32_sfloat_s8_uint_m:
        case xg_format_bc1_rgb_unorm_block_m:
        case xg_format_bc1_rgb_srgb_block_m:
        case xg_format_bc1_rgba_unorm_block_m:
        case xg_format_bc1_rgba_srgb_block_m:
        case xg_format_bc2_unorm_block_m:
        case xg_format_bc2_srgb_block_m:
        case xg_format_bc3_unorm_block_m:
        case xg_format_bc3_srgb_block_m:
        case xg_format_bc4_unorm_block_m:
        case xg_format_bc4_snorm_block_m:
        case xg_format_bc5_unorm_block_m:
        case xg_format_bc5_snorm_block_m:
        case xg_format_bc6h_ufloat_block_m:
        case xg_format_bc6h_sfloat_block_m:
        case xg_format_bc7_unorm_block_m:
        case xg_format_bc7_srgb_block_m:
        case xg_format_etc2_r8g8b8_unorm_block_m:
        case xg_format_etc2_r8g8b8_srgb_block_m:
        case xg_format_etc2_r8g8b8a1_unorm_block_m:
        case xg_format_etc2_r8g8b8a1_srgb_block_m:
        case xg_format_etc2_r8g8b8a8_unorm_block_m:
        case xg_format_etc2_r8g8b8a8_srgb_block_m:
        case xg_format_eac_r11_unorm_block_m:
        case xg_format_eac_r11_snorm_block_m:
        case xg_format_eac_r11g11_unorm_block_m:
        case xg_format_eac_r11g11_snorm_block_m:
        case xg_format_astc_4_mx4_UNORM_BLOCK:
        case xg_format_astc_4_mx4_SRGB_BLOCK:
        case xg_format_astc_5_mx4_UNORM_BLOCK:
        case xg_format_astc_5_mx4_SRGB_BLOCK:
        case xg_format_astc_5_mx5_UNORM_BLOCK:
        case xg_format_astc_5_mx5_SRGB_BLOCK:
        case xg_format_astc_6_mx5_UNORM_BLOCK:
        case xg_format_astc_6_mx5_SRGB_BLOCK:
        case xg_format_astc_6_mx6_UNORM_BLOCK:
        case xg_format_astc_6_mx6_SRGB_BLOCK:
        case xg_format_astc_8_mx5_UNORM_BLOCK:
        case xg_format_astc_8_mx5_SRGB_BLOCK:
        case xg_format_astc_8_mx6_UNORM_BLOCK:
        case xg_format_astc_8_mx6_SRGB_BLOCK:
        case xg_format_astc_8_mx8_UNORM_BLOCK:
        case xg_format_astc_8_mx8_SRGB_BLOCK:
        case xg_format_astc_10_mx5_UNORM_BLOCK:
        case xg_format_astc_10_mx5_SRGB_BLOCK:
        case xg_format_astc_10_mx6_UNORM_BLOCK:
        case xg_format_astc_10_mx6_SRGB_BLOCK:
        case xg_format_astc_10_mx8_UNORM_BLOCK:
        case xg_format_astc_10_mx8_SRGB_BLOCK:
        case xg_format_astc_10_mx10_UNORM_BLOCK:
        case xg_format_astc_10_mx10_SRGB_BLOCK:
        case xg_format_astc_12_mx10_UNORM_BLOCK:
        case xg_format_astc_12_mx10_SRGB_BLOCK:
        case xg_format_astc_12_mx12_UNORM_BLOCK:
        case xg_format_astc_12_mx12_SRGB_BLOCK:
        default:
            return 0;
    }
}

const char* xg_format_str ( xg_format_e format ) {
    switch ( format ) {
        case xg_format_undefined_m:
            return "Undefined format";

        case xg_format_r4g4_unorm_pack8_m:
            return "R4G4 UN";

        case xg_format_r4g4b4a4_unorm_pack16_m:
            return "R4G4B4A4 UN";

        case xg_format_b4g4r4a4_unorm_pack16_m:
            return "B4G4R4A4 UN";

        case xg_format_r5g6b5_unorm_pack16_m:
            return "R5G6B5 UN";

        case xg_format_b5g6r5_unorm_pack16_m:
            return "B5G6R5 UN";

        case xg_format_r5g5b5a1_unorm_pack16_m:
            return "R5G6B5A1 UN";

        case xg_format_b5g5r5a1_unorm_pack16_m:
            return "B5G6R5A1 UN";

        case xg_format_a1r5g5b5_unorm_pack16_m:
            return "A1R5G5B5 UN";

        case xg_format_r8_unorm_m:
            return "R8 UN";

        case xg_format_r8_snorm_m:
            return "R8 SN";

        case xg_format_r8_uscaled_m:
            return "R8 US";

        case xg_format_r8_sscaled_m:
            return "R8 SS";

        case xg_format_r8_uint_m:
            return "R8 UI";

        case xg_format_r8_sint_m:
            return "R8 SI";

        case xg_format_r8_srgb_m:
            return "R8 sRGB";

        case xg_format_r8g8_unorm_m:
            return "R8G8 UN";

        case xg_format_r8g8_snorm_m:
            return "R8G8 SN";

        case xg_format_r8g8_uscaled_m:
            return "R8G8 US";

        case xg_format_r8g8_sscaled_m:
            return "R8G8 SS";

        case xg_format_r8g8_uint_m:
            return "R8G8 UI";

        case xg_format_r8g8_sint_m:
            return "R8G8 SI";

        case xg_format_r8g8_srgb_m:
            return "R8G8 sRGB";

        case xg_format_r8g8b8_unorm_m:
            return "R8G8B8 UN";

        case xg_format_r8g8b8_snorm_m:
            return "R8G8B8 SN";

        case xg_format_r8g8b8_uscaled_m:
            return "R8G8B8 US";

        case xg_format_r8g8b8_sscaled_m:
            return "R8G8B8 SS";

        case xg_format_r8g8b8_uint_m:
            return "R8G8B8 UI";

        case xg_format_r8g8b8_sint_m:
            return "R8G8B8 SI";

        case xg_format_r8g8b8_srgb_m:
            return "R8G8B8 sRGB";

        case xg_format_b8g8r8_unorm_m:
            return "B8G8R8 UN";

        case xg_format_b8g8r8_snorm_m:
            return "B8G8R8 SN";

        case xg_format_b8g8r8_uscaled_m:
            return "B8G8R8 US";

        case xg_format_b8g8r8_sscaled_m:
            return "B8G8R8 SS";

        case xg_format_b8g8r8_uint_m:
            return "B8G8R8 UI";

        case xg_format_b8g8r8_sint_m:
            return "B8G8R8 SI";

        case xg_format_b8g8r8_srgb_m:
            return "B8G8R8 sRGB";

        case xg_format_r8g8b8a8_unorm_m:
            return "R8G8B8A8 UN";

        case xg_format_r8g8b8a8_snorm_m:
            return "R8G8B8A8 SN";

        case xg_format_r8g8b8a8_uscaled_m:
            return "R8G8B8A8 US";

        case xg_format_r8g8b8a8_sscaled_m:
            return "R8G8B8A8 SS";

        case xg_format_r8g8b8a8_uint_m:
            return "R8G8B8A8 UI";

        case xg_format_r8g8b8a8_sint_m:
            return "R8G8B8A8 SI";

        case xg_format_r8g8b8a8_srgb_m:
            return "R8G8B8A8 sRGB";

        case xg_format_b8g8r8a8_snorm_m:
            return "B8G8R8A8 SN";

        case xg_format_b8g8r8a8_unorm_m:
            return "B8G8R8A8 UN";

        case xg_format_b8g8r8a8_uscaled_m:
            return "B8G8R8A8 US";

        case xg_format_b8g8r8a8_sscaled_m:
            return "B8G8R8A8 SS";

        case xg_format_b8g8r8a8_uint_m:
            return "B8G8R8A8 UI";

        case xg_format_b8g8r8a8_sint_m:
            return "B8G8R8A8 SI";

        case xg_format_b8g8r8a8_srgb_m:
            return "B8G8R8A8 sRGB";

        case xg_format_a8b8g8r8_unorm_pack32_m:
            return "A8B8G8R8 UN";

        case xg_format_a8b8g8r8_snorm_pack32_m:
            return "A8B8G8R8 SN";

        case xg_format_a8b8g8r8_uscaled_pack32_m:
            return "A8B8G8R8 US";

        case xg_format_a8b8g8r8_sscaled_pack32_m:
            return "A8B8G8R8 SS";

        case xg_format_a8b8g8r8_uint_pack32_m:
            return "A8B8G8R8 UI";

        case xg_format_a8b8g8r8_sint_pack32_m:
            return "A8B8G8R8 SI";

        case xg_format_a8b8g8r8_srgb_pack32_m:
            return "A8B8G8R8 sRGB";

        case xg_format_a2r10g10b10_unorm_pack32_m:
            return "A2R10G10B10 UN";

        case xg_format_a2r10g10b10_snorm_pack32_m:
            return "A2R10G10B10 SN";

        case xg_format_a2r10g10b10_uscaled_pack32_m:
            return "A2R10G10B10 US";

        case xg_format_a2r10g10b10_sscaled_pack32_m:
            return "A2R10G10B10 SS";

        case xg_format_a2r10g10b10_uint_pack32_m:
            return "A2R10G10B10 UI";

        case xg_format_a2r10g10b10_sint_pack32_m:
            return "A2R10G10B10 SI";

        case xg_format_a2b10g10r10_unorm_pack32_m:
            return "A2B10G10R10 UN";

        case xg_format_a2b10g10r10_snorm_pack32_m:
            return "A2B10G10R10 SN";

        case xg_format_a2b10g10r10_uscaled_pack32_m:
            return "A2B10G10R10 US";

        case xg_format_a2b10g10r10_sscaled_pack32_m:
            return "A2B10G10R10 SS";

        case xg_format_a2b10g10r10_uint_pack32_m:
            return "A2B10G10R10 UI";

        case xg_format_a2b10g10r10_sint_pack32_m:
            return "A2B10G10R10 SI";

        case xg_format_r16_unorm_m:
            return "R16 UN";

        case xg_format_r16_snorm_m:
            return "R16 SN";

        case xg_format_r16_uscaled_m:
            return "R16 US";

        case xg_format_r16_sscaled_m:
            return "R16 SS";

        case xg_format_r16_uint_m:
            return "R16 UI";

        case xg_format_r16_sint_m:
            return "R16 SI";

        case xg_format_r16_sfloat_m:
            return "R16 SF";

        case xg_format_r16g16_unorm_m:
            return "R16G16 UN";

        case xg_format_r16g16_snorm_m:
            return "R16G16 SN";

        case xg_format_r16g16_uscaled_m:
            return "R16G16 US";

        case xg_format_r16g16_sscaled_m:
            return "R16G16 SS";

        case xg_format_r16g16_uint_m:
            return "R16G16 UI";

        case xg_format_r16g16_sint_m:
            return "R16G16 SI";

        case xg_format_r16g16_sfloat_m:
            return "R16G16 SF";

        case xg_format_r16g16b16_unorm_m:
            return "R16G16B16 UN";

        case xg_format_r16g16b16_snorm_m:
            return "R16G16B16 SN";

        case xg_format_r16g16b16_uscaled_m:
            return "R16G16B16 US";

        case xg_format_r16g16b16_sscaled_m:
            return "R16G16B16 SS";

        case xg_format_r16g16b16_uint_m:
            return "R16G16B16 UI";

        case xg_format_r16g16b16_sint_m:
            return "R16G16B16 SI";

        case xg_format_r16g16b16_sfloat_m:
            return "R16G16B16 SF";

        case xg_format_r16g16b16a16_unorm_m:
            return "R16G16B16A16 UN";

        case xg_format_r16g16b16a16_snorm_m:
            return "R16G16B16A16 SN";

        case xg_format_r16g16b16a16_uscaled_m:
            return "R16G16B16A16 US";

        case xg_format_r16g16b16a16_sscaled_m:
            return "R16G16B16A16 SS";

        case xg_format_r16g16b16a16_uint_m:
            return "R16G16B16A16 UI";

        case xg_format_r16g16b16a16_sint_m:
            return "R16G16B16A16 SI";

        case xg_format_r16g16b16a16_sfloat_m:
            return "R16G16B16A16 SF";

        case xg_format_r32_uint_m:
            return "R32 UI";

        case xg_format_r32_sint_m:
            return "R32 SI";

        case xg_format_r32_sfloat_m:
            return "R32 SF";

        case xg_format_r32g32_uint_m:
            return "R32G32 UI";

        case xg_format_r32g32_sint_m:
            return "R32G32 SI";

        case xg_format_r32g32_sfloat_m:
            return "R32G32 SF";

        case xg_format_r32g32b32_uint_m:
            return "R32G32B32 UI";

        case xg_format_r32g32b32_sint_m:
            return "R32G32B32 SI";

        case xg_format_r32g32b32_sfloat_m:
            return "R32G32B32 SF";

        case xg_format_r32g32b32a32_uint_m:
            return "R32G32B32 UI";

        case xg_format_r32g32b32a32_sint_m:
            return "R32G32B32 SI";

        case xg_format_r32g32b32a32_sfloat_m:
            return "R32G32B32 SF";

        case xg_format_r64_uint_m:
            return "R64 UI";

        case xg_format_r64_sint_m:
            return "R64 SI";

        case xg_format_r64_sfloat_m:
            return "R64 SF";

        case xg_format_r64g64_uint_m:
            return "R64G64 UI";

        case xg_format_r64g64_sint_m:
            return "R64G64 SI";

        case xg_format_r64g64_sfloat_m:
            return "R64G64 SF";

        case xg_format_r64g64b64_uint_m:
            return "R64G64B64 UI";

        case xg_format_r64g64b64_sint_m:
            return "R64G64B64 SI";

        case xg_format_r64g64b64_sfloat_m:
            return "R64G64B64 SF";

        case xg_format_r64g64b64a64_uint_m:
            return "R64G64B64A64 UI";

        case xg_format_r64g64b64a64_sint_m:
            return "R64G64B64A64 SI";

        case xg_format_r64g64b64a64_sfloat_m:
            return "R64G64B64A64 SF";

        case xg_format_b10g11r11_ufloat_pack32_m:
            return "B10G11R11 UF";

        case xg_format_e5b9g9r9_ufloat_pack32_m:
            return "E5B9G9R9 UF";

        case xg_format_d16_unorm_m:
            return "D16 UN";

        case xg_format_x8_d24_unorm_pack32_m:
            return "X8D24 UN";

        case xg_format_d32_sfloat_m:
            return "D32 SF";

        case xg_format_s8_uint_m:
            return "S8 UI";

        case xg_format_d16_unorm_s8_uint_m:
            return "D16 UN S8 UI";

        case xg_format_d24_unorm_s8_uint_m:
            return "D24 UN S8 UI";

        case xg_format_d32_sfloat_s8_uint_m:
            return "D32 SF S8 UI";

        case xg_format_bc1_rgb_unorm_block_m:
        case xg_format_bc1_rgb_srgb_block_m:
        case xg_format_bc1_rgba_unorm_block_m:
        case xg_format_bc1_rgba_srgb_block_m:
            return "BC1 compressed texture";

        case xg_format_bc2_unorm_block_m:
        case xg_format_bc2_srgb_block_m:
            return "BC2 compressed texture";

        case xg_format_bc3_unorm_block_m:
        case xg_format_bc3_srgb_block_m:
            return "BC3 compressed texture";

        case xg_format_bc4_unorm_block_m:
        case xg_format_bc4_snorm_block_m:
            return "BC4 compressed texture";

        case xg_format_bc5_unorm_block_m:
        case xg_format_bc5_snorm_block_m:
            return "BC5 compressed texture";

        case xg_format_bc6h_ufloat_block_m:
        case xg_format_bc6h_sfloat_block_m:
            return "BC6 compressed texture";

        case xg_format_bc7_unorm_block_m:
        case xg_format_bc7_srgb_block_m:
            return "BC7 compressed texture";

        case xg_format_etc2_r8g8b8_unorm_block_m:
        case xg_format_etc2_r8g8b8_srgb_block_m:
        case xg_format_etc2_r8g8b8a1_unorm_block_m:
        case xg_format_etc2_r8g8b8a1_srgb_block_m:
        case xg_format_etc2_r8g8b8a8_unorm_block_m:
        case xg_format_etc2_r8g8b8a8_srgb_block_m:
        case xg_format_eac_r11_unorm_block_m:
        case xg_format_eac_r11_snorm_block_m:
        case xg_format_eac_r11g11_unorm_block_m:
        case xg_format_eac_r11g11_snorm_block_m:
            return "ETC2 compressed texture";

        case xg_format_astc_4_mx4_UNORM_BLOCK:
        case xg_format_astc_4_mx4_SRGB_BLOCK:
        case xg_format_astc_5_mx4_UNORM_BLOCK:
        case xg_format_astc_5_mx4_SRGB_BLOCK:
        case xg_format_astc_5_mx5_UNORM_BLOCK:
        case xg_format_astc_5_mx5_SRGB_BLOCK:
        case xg_format_astc_6_mx5_UNORM_BLOCK:
        case xg_format_astc_6_mx5_SRGB_BLOCK:
        case xg_format_astc_6_mx6_UNORM_BLOCK:
        case xg_format_astc_6_mx6_SRGB_BLOCK:
        case xg_format_astc_8_mx5_UNORM_BLOCK:
        case xg_format_astc_8_mx5_SRGB_BLOCK:
        case xg_format_astc_8_mx6_UNORM_BLOCK:
        case xg_format_astc_8_mx6_SRGB_BLOCK:
        case xg_format_astc_8_mx8_UNORM_BLOCK:
        case xg_format_astc_8_mx8_SRGB_BLOCK:
        case xg_format_astc_10_mx5_UNORM_BLOCK:
        case xg_format_astc_10_mx5_SRGB_BLOCK:
        case xg_format_astc_10_mx6_UNORM_BLOCK:
        case xg_format_astc_10_mx6_SRGB_BLOCK:
        case xg_format_astc_10_mx8_UNORM_BLOCK:
        case xg_format_astc_10_mx8_SRGB_BLOCK:
        case xg_format_astc_10_mx10_UNORM_BLOCK:
        case xg_format_astc_10_mx10_SRGB_BLOCK:
        case xg_format_astc_12_mx10_UNORM_BLOCK:
        case xg_format_astc_12_mx10_SRGB_BLOCK:
        case xg_format_astc_12_mx12_UNORM_BLOCK:
        case xg_format_astc_12_mx12_SRGB_BLOCK:
            return "ASTC compressed texture";

        default:
            return "Malformed format enum";
    }
}

const char* xg_color_space_str ( xg_color_space_e colorspace ) {
    switch ( colorspace ) {
        case xg_colorspace_linear_m:
            return "Linear";

        case xg_colorspace_srgb_m:
            return "sRGB";

        case xg_colorspace_hdr_hlg_m:
            return "HDR HLG";

        case xg_colorspace_hdr_pq_m:
            return "HDR PQ";

        default:
            return "Malformed colorspace enum";

    }
}

const char* xg_present_mode_str ( xg_present_mode_e mode ) {
    switch ( mode ) {
        case xg_present_mode_immediate_m:
            return "Immediate";

        case xg_present_mode_mailbox_m:
            return "Mailbox";

        case xg_present_mode_fifo_m:
            return "FIFO";

        case xg_present_mode_fifo_relaxed_m:
            return "Relaxed FIFO";

        default:
            return "Malformed present mode enum";
    }
}

xg_pipeline_stage_bit_e xg_shading_stage_to_pipeline_stage ( xg_shading_stage_e stage ) {
    switch ( stage ) {
        case xg_shading_stage_vertex_m:
            return xg_pipeline_stage_bit_vertex_shader_m;

        case xg_shading_stage_fragment_m:
            return xg_pipeline_stage_bit_fragment_shader_m;

        case xg_shading_stage_compute_m:
            return xg_pipeline_stage_bit_compute_shader_m;

        default:
            return xg_pipeline_stage_bit_invalid_m;
    }
}

bool xg_memory_access_is_write ( xg_memory_access_bit_e access ) {
    uint32_t mask = xg_memory_access_bit_shader_write_m | xg_memory_access_bit_color_write_m | xg_memory_access_bit_depth_stencil_write_m | xg_memory_access_bit_transfer_write_m | xg_memory_access_bit_host_write_m | xg_memory_access_bit_memory_write_m;
    return ( ( unsigned int ) access & mask ) != 0;
}

bool xg_format_has_depth ( xg_format_e format ) {
    switch ( format ) {
        case xg_format_d16_unorm_m:
        case xg_format_x8_d24_unorm_pack32_m:
        case xg_format_d32_sfloat_m:
        case xg_format_d16_unorm_s8_uint_m:
        case xg_format_d24_unorm_s8_uint_m:
        case xg_format_d32_sfloat_s8_uint_m:
            return true;

        default:
            return false;
    }
}

bool xg_format_has_stencil ( xg_format_e format ) {
    switch ( format ) {
        case xg_format_s8_uint_m:
        case xg_format_d16_unorm_s8_uint_m:
        case xg_format_d24_unorm_s8_uint_m:
        case xg_format_d32_sfloat_s8_uint_m:
            return true;

        default:
            return false;
    }
}
