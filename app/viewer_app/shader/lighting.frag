#version 450

#include <xs.glsl>

#include "common.glsl"

layout ( binding = 0, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_color;
layout ( binding = 1, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_normal;
layout ( binding = 2, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_material;
layout ( binding = 3, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_depth;
layout ( binding = 4, set = xs_resource_binding_set_per_draw_m ) uniform texture2D tex_shadows;

layout ( binding = 5, set = xs_resource_binding_set_per_draw_m ) uniform sampler sampler_point;

struct light_t {
    vec3 pos;
    float emissive;
    vec3 color;
    uint _pad0;
    mat4 proj_from_view;
    mat4 view_from_world;
};

#define MAX_LIGHT_COUNT 32

layout ( binding = 6, set = xs_resource_binding_set_per_draw_m ) uniform draw_cbuffer_t {
    uint light_count;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    light_t lights[MAX_LIGHT_COUNT];
} draw_cbuffer;

layout ( location = 0 ) out vec4 out_color;

void main ( void ) {
    vec2 screen_uv = vec2 ( gl_FragCoord.xy / frame_cbuffer.resolution_f32 );
    screen_uv = dejitter_uv ( screen_uv );

    vec3 view_normal = texture ( sampler2D ( tex_normal, sampler_point ), screen_uv ).xyz * 2 - 1;
    vec3 base_color = texture ( sampler2D ( tex_color, sampler_point ), screen_uv ).xyz;
    //vec3 material = texture ( sampler2D ( tex_material, sampler_point ), screen_uv ).xyz;
    float depth = texture ( sampler2D ( tex_depth, sampler_point ), screen_uv ).x;

    vec3 view_geo_pos = view_from_depth ( screen_uv, depth );
    // Normal Offset shadows, helps with acne
    // https://digitalrune.github.io/DigitalRune-Documentation/html/3f4d959e-9c98-4a97-8d85-7a73c26145d7.htm
    // https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/
    // https://c0de517e.blogspot.com/2011/05/shadowmap-bias-notes.html
    // https://ndotl.wordpress.com/2014/12/19/notes-on-shadow-bias/
    view_geo_pos += view_normal * 0.01;
    vec4 world_geo_pos = view_cbuffer.world_from_view * vec4 ( view_geo_pos, 1.f );

    vec3 irradiance = vec3 ( 0, 0, 0 );

    for ( uint i = 0; i < draw_cbuffer.light_count; ++i ) {
        vec3 world_light_pos = draw_cbuffer.lights[i].pos;
        float light_emissive = draw_cbuffer.lights[i].emissive;
        vec3 light_color = draw_cbuffer.lights[i].color;

        // TODO refactor screen_from_view to take in a proj_from_view matrix and use that
        vec4 shadow_proj = draw_cbuffer.lights[i].proj_from_view * draw_cbuffer.lights[i].view_from_world * world_geo_pos;
        shadow_proj /= shadow_proj.w;
        vec3 shadow_screen = vec3 ( shadow_proj.xy * vec2 ( 0.5, -0.5 ) + 0.5, shadow_proj.z );

        float shadow = 0;
    #if 1
        int pcf_radius = 3;

        for ( int x = -pcf_radius; x <= pcf_radius; ++x ) {
            for ( int y = -pcf_radius; y <= pcf_radius; ++y ) {
                vec2 shadow_uv = shadow_screen.xy + vec2 ( x, y ) * ( 1.f / 1024.f ); // TODO shadow texel size
                float shadow_depth = texture ( sampler2D ( tex_shadows, sampler_point ), shadow_uv ).x;

                //if ( shadow_screen.x > 1.f || shadow_screen.x < 0.f || shadow_screen.y > 1.f || shadow_screen.y < 0.f ) {
                //    shadow_depth = 1.f;
                //}

                float shadow_bias = 0.003;
                float shadow_contribution = shadow_proj.z - shadow_bias > shadow_depth ? 1.f : 0.f;

                if ( shadow_screen.z > 1.f ) {
                    shadow_contribution = 0;
                }

                shadow += shadow_contribution;
            }
        }

        shadow /= ( pcf_radius * 2 + 1 ) * ( pcf_radius * 2 + 1 );
    #else
        float shadow_depth = texture ( sampler2D ( tex_shadows, sampler_point ), shadow_screen.xy ).x;
        float shadow_bias = 0.03;
        if ( shadow_screen.z > 1.f ) {
            shadow = 0;
        } else {
            shadow = shadow_proj.z - shadow_bias > shadow_depth ? 1.f : 0.f;
        }
    #endif

        float light_visibility = 1.f - shadow;
        //float shadow_occlusion = shadow_depth + 0.001 > shadow_proj.z ? 1.f : 0.f;

        vec3 view_light_pos = ( view_cbuffer.view_from_world * vec4 ( world_light_pos, 1 ) ).xyz;
        vec3 view_light_dir = normalize ( view_light_pos - view_geo_pos );
        float nl = clamp ( dot ( view_light_dir, view_normal ), 0, 1 );
        float d = distance ( view_geo_pos, view_light_pos );

        irradiance += light_visibility * light_emissive * light_color * base_color * nl / ( d * d );
    }

    float ambient_emissive = 0;
    vec3 ambient_color = vec3 ( 1, 1, 1 );
    vec3 direct = irradiance + ambient_emissive * ambient_color * base_color;

    out_color = vec4 ( direct, 1 );
}
