#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_normal;
layout ( binding = 1, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_base_color;
layout ( binding = 2, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_lighting;
layout ( binding = 3, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_hiz;

layout ( binding = 4, set = xs_resource_binding_set_per_draw_m ) uniform sampler sampler_point;

layout ( binding = 5, set = xs_resource_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    vec2 resolution_f32;
    uint hiz_mip_count;
} draw_cbuffer;

layout ( location = 0 ) out vec4 out_color;

// ---

// http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radicalInverse_VdC ( uint bits ) {
    bits = ( bits << 16u ) | ( bits >> 16u );
    bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
    bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
    bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
    bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
    return float ( bits ) * 2.3283064365386963e-10; // / 0x100000000
}
vec2 rng_hammersley2d ( uint i, uint N ) {
    return vec2 ( float ( i ) / float ( N ), radicalInverse_VdC ( i ) );
}

// http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
vec2 rng_r2 ( float seed, float n ) {
    float g = 1.32471795724474602596;
    float a1 = 1.0 / g;
    float a2 = 1.0 / ( g * g );
    float x = fract ( seed + a1 * n );
    float y = fract ( seed + a2 * n );
    return vec2 ( x, y );
}

// https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence
float rng_ign ( int pixelX, int pixelY, int frame ) {
    frame = frame % 64; // need to periodically reset frame to avoid numerical issues
    float x = float ( pixelX ) + 5.588238f * float ( frame );
    float y = float ( pixelY ) + 5.588238f * float ( frame );
    return fract ( 52.9829189f * fract ( 0.06711056f * float ( x ) + 0.00583715f * float ( y ) ) );
}

vec3 generate_tangent ( vec3 normal ) {
    vec3 tangent;

    if ( dot ( vec3 ( 0, 1, 0 ), normal ) == 1.0f ) {
        tangent = vec3 ( 1, 0, 0 );
    } else {
        tangent = cross ( vec3 ( 0, 1, 0 ), normal );
        tangent = normalize ( tangent );
    }

    return tangent;
}

void main ( void ) {
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / draw_cbuffer.resolution_f32 );

    // sample
    vec3 view_normal = texture ( sampler2D ( tex_normal, sampler_point ), screen_uv ).xyz * 2 - 1;
    float depth = textureLod ( sampler2D ( tex_hiz, sampler_point ), screen_uv, 0 ).x;
    vec3 base_color = texture ( sampler2D ( tex_base_color, sampler_point ), screen_uv ).xyz;

    // reconstruct position
    vec3 view_pos = view_from_depth ( screen_uv, depth );

    // init rng
    uint rng_state = rng_wang_init ( gl_FragCoord.xy );

#define RAY_COUNT 2

    // init accumulators
    float sample_distances[RAY_COUNT];
    vec3 sample_colors[RAY_COUNT];

    for ( uint ray_it = 0; ray_it < RAY_COUNT; ++ray_it ) {
        sample_colors[ray_it] = vec3 ( 0, 0, 0 );
        sample_distances[ray_it] = 1;
    }

    if ( depth < 1 ) {
        for ( uint ray_it = 0; ray_it < RAY_COUNT; ++ray_it ) {
            // sample hemisphere
            float ex = rng_wang ( rng_state );
            float ey = rng_wang ( rng_state );
            vec2 e2 = vec2 ( ex, ey );
            vec3 hemisphere_normal = sample_cosine_weighted_hemisphere_normal ( e2, view_normal );
            hemisphere_normal = normalize ( view_normal * 0.5f + hemisphere_normal ); // bias towards normal to reduce hiz self intersections

            // trace
            vec3 hit_screen_pos;
            float hit_depth;

            if ( trace_screen_space_ray ( hit_screen_pos, hit_depth, view_pos, hemisphere_normal, tex_hiz, draw_cbuffer.hiz_mip_count, sampler_point, 50 ) ) {
                vec3 hit_color = texture ( sampler2D ( tex_lighting, sampler_point ), hit_screen_pos.xy ).xyz;
                float hit_distance = distance ( view_pos, view_from_depth ( hit_screen_pos.xy, hit_depth ) );
                sample_colors[ray_it] = hit_color *  dot ( view_normal, hemisphere_normal );
                sample_distances[ray_it] = hit_distance;
            }
        }
    }

    vec3 sample_color = vec3 ( 0, 0, 0 );

    for ( uint ray_it = 0; ray_it < RAY_COUNT; ++ray_it ) {
        float attenuation = 1.0 / ( sample_distances[ray_it] + 1 );
        //float occlusion = min ( 1.0, sample_distances[ray_it] );
        sample_color += sample_colors[ray_it] * base_color * attenuation;// * occlusion;// * attenuation * attenuation;
    }

    sample_color /= float ( RAY_COUNT );

    out_color = vec4 ( sample_color, 1 );
}
