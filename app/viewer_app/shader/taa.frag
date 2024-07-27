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

// https://alextardif.com/TAA.html
// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
// https://ziyadbarakat.wordpress.com/2020/07/28/temporal-anti-aliasing-step-by-step/
// https://s3.amazonaws.com/arena-attachments/655504/c5c71c5507f0f8bf344252958254fb7d.pdf?1468341463
// https://www.gamedev.net/forums/topic/714378-removing-jitter-when-rendering-velocity-texture-for-taa/
// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// https://bartwronski.com/2014/03/15/temporal-supersampling-and-antialiasing/

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

#if 1
    // Sample history
    vec3 history = texture ( sampler2D ( tex_history, sampler_linear ), reprojected_uv ).xyz;
#else
    // TODO test on moving scene
    vec3 history = sample_catmull_rom ( tex_history, sampler_linear, reprojected_uv, frame_cbuffer.resolution_f32 ).xyz;
#endif

    // Sample neighborhood
    vec3 n0 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 (  1,  0 ) * texel_size ).xyz;
    vec3 n1 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 (  0,  1 ) * texel_size ).xyz;
    vec3 n2 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 ( -1,  0 ) * texel_size ).xyz;
    vec3 n3 = texture ( sampler2D ( tex_color, sampler_point ), screen_uv + vec2 (  0, -1 ) * texel_size ).xyz;

#if 1
    // Neighborhood clamping [Lottes11], [Malan12]
    // reduces disocclusion ghosting
    vec3 color_min = min ( color, min ( n0, min ( n1, min ( n2, n3 ) ) ) );
    vec3 color_max = max ( color, max ( n0, max ( n1, max ( n2, n3 ) ) ) );

    history = clamp ( history, color_min, color_max );
#else
    // Variance clipping [Salvi16]
    const float VARIANCE_CLIPPING_GAMMA = 1.f;

    vec3 moment1 = color + n0 + n1 + n2 + n3;
    vec3 moment2 = color * color + n0 * n0 + n1 * n1 + n2 * n2 + n3 * n3;

    vec3 mu = moment1 / 5.0;
    vec3 sigma = sqrt ( moment2 / 5.0 - mu * mu );

    vec3 color_min = mu - VARIANCE_CLIPPING_GAMMA * sigma;
    vec3 color_max = mu + VARIANCE_CLIPPING_GAMMA * sigma;

    history = clamp ( history, color_min, color_max );
#endif

    float blend_factor = 0.1f;

#if 0
    // reduces flickering on edges
    // TODO fix this, as it is it kills all AA
    // https://community.arm.com/arm-community-blogs/b/graphics-gaming-and-vr-blog/posts/temporal-anti-aliasing
    float lum0 = rgb_to_luma ( color );
    float lum1 = rgb_to_luma ( history );
    float luma_diff = abs ( lum0 - lum1 ) / ( 0.000001 + max ( lum0, max ( lum1, rgb_to_luma ( color_max ) ) ) );
    luma_diff = 1 - luma_diff;
    luma_diff *= luma_diff;
    blend_factor *= luma_diff;
#endif

    // Blend
    vec3 blend = mix ( history, color, blend_factor );
    out_color = vec4 ( blend, 1 );
}
