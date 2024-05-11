#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_color;
layout ( binding = 1, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_depth;
layout ( binding = 2, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_history;
layout ( binding = 3, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_id;

layout ( binding = 4, set = xs_resource_binding_set_per_draw_m ) uniform sampler sampler_point;
layout ( binding = 5, set = xs_resource_binding_set_per_draw_m ) uniform sampler sampler_linear;

layout ( location = 0 ) out vec4 out_color;

vec4 clip_aabb ( vec4 color, float p, vec3 minimum, vec3 maximum ) {
    // note: only clips towards aabb center (but fast!)
    vec3 center  = 0.5 * ( maximum + minimum );
    vec3 extents = 0.5 * ( maximum - minimum );

    // This is actually `distance`, however the keyword is reserved
    vec4 offset = color - vec4 ( center, p );
    vec3 repeat = abs ( offset.xyz / extents );

    repeat.x = max ( repeat.x, max ( repeat.y, repeat.z ) );

    if ( repeat.x > 1.0 ) {
        // `color` is not intersecting (nor inside) the AABB; it's clipped to the closest extent
        return vec4 ( center, p ) + offset / repeat.x;
    } else {
        // `color` is intersecting (or inside) the AABB.
        return color;
    }
}

void main ( void ) {
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / frame_cbuffer.resolution_f32 );
    vec2 texel_size = 1.0 / frame_cbuffer.resolution_f32;

    // Sample
    vec3 color = texture ( sampler2D ( tex_color, sampler_point ), screen_uv ).xyz;
    float depth = texture ( sampler2D ( tex_depth, sampler_point ), screen_uv ).x;

    // Reproject
    vec3 view = view_from_depth ( screen_uv, depth );
    vec3 world = ( view_cbuffer.world_from_view * vec4 ( view, 1 ) ).xyz;
    vec3 prev_screen = prev_screen_from_world ( world );
    vec2 screen_offset = prev_screen.xy - screen_uv;
    vec2 reprojected_uv = screen_uv + screen_offset;

    // Sample history
    vec3 history = texture ( sampler2D ( tex_history, sampler_linear ), reprojected_uv ).xyz;

#if 0
    // Sample color for pixel and neighborhood
    vec3 tile[3][3];

    for ( int x = -1; x <= 1; ++x ) {
        for ( int y = -1; y <= -1; ++y ) {
            vec2 sample_offset = vec2 ( x, y );
            vec2 sample_uv = screen_uv + sample_offset * texel_size;

            vec3 color_sample = texture ( sampler2D ( tex_color, sampler_point ), sample_uv ).xyz;
            tile[x][y] = color_sample;
        }
    }

#endif

    // Neighborhood clamping
    vec3 color_min = color;
    vec3 color_max = color;

#if 0

    for ( int y = -1; y < 1; ++y ) {
        for ( int x = -1; x < 1; ++x ) {
            vec2 sample_offset = vec2 ( x, y );
            vec2 sample_uv = screen_uv + sample_offset * texel_size;
            vec3 c = texture ( sampler2D ( tex_color, sampler_point ), sample_uv ).xyz;
            color_min = min ( color_min, c );
            color_max = max ( color_max, c );
        }
    }

#else
    vec3 n0 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 ( 1, 0 ) * texel_size ).xyz;
    vec3 n1 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 ( 0, 1 ) * texel_size ).xyz;
    vec3 n2 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 ( -1, 0 ) * texel_size ).xyz;
    vec3 n3 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 ( 0, -1 ) * texel_size ).xyz;

    color_min = min ( color, min ( n0, min ( n1, min ( n2, n3 ) ) ) );
    color_max = max ( color, max ( n0, max ( n1, max ( n2, n3 ) ) ) );
#endif

    history = clamp ( history, color_min, color_max );
    //history = clip_aabb ( vec4 ( history, 0 ), 0, color_min, color_max ).xyz;

#if 0
    vec3 min_cross = min ( min ( tile[0][0], tile[2][2] ), min ( tile[0][2], tile[2][0] ) );
    vec3 max_cross = max ( max ( tile[0][0], tile[2][2] ), max ( tile[0][2], tile[2][0] ) );
    vec3 min_plus  = min ( min ( tile[0][1], tile[2][1] ), min ( tile[1][0], tile[1][2] ) );
    vec3 max_plus  = max ( max ( tile[0][1], tile[2][1] ), max ( tile[1][0], tile[1][2] ) );

    vec3 tile_min = min ( min_cross, min_plus ) * 0.5 + min_plus * 0.5;
    vec3 tile_max = min ( max_cross, max_plus ) * 0.5 + max_plus * 0.5;
    history = clamp ( history, tile_min, tile_max );

    float luma_min = rgb_to_luma ( tile_min );
    float luma_max = rgb_to_luma ( tile_max );
    float luma_history = rgb_to_luma ( history );

    // Filter

    // Blend factor
    vec2 pixel_offset = screen_offset * frame_cbuffer.resolution_f32;
    float motion_factor = clamp ( dot ( 1.0.xx, 0.5 - abs ( 0.5 - fract ( pixel_offset ) ) ), 0.0, 1.0 );
    float contrast_factor = 0.125f * min ( luma_history, luma_min ) / ( 0.00005 + max ( abs ( luma_min - luma_history ), abs ( luma_max - luma_history ) ) );
    float blend_factor = 0.5;//motion_factor * contrast_factor;

#endif

    // Blend
    vec3 blend = mix ( history, color, 0.1 );

    out_color = vec4 ( blend, 1 );
}
