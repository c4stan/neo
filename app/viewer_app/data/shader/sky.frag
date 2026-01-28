#version 450

#include "xs.glsl"

#include "common.glsl"

layout ( binding = 0, set = xs_shader_binding_set_dispatch_m ) uniform texture2D color_texture;
layout ( binding = 1, set = xs_shader_binding_set_dispatch_m ) uniform sampler sampler_linear;

layout ( location = 0 ) in vec3 in_dir;

layout ( location = 0 ) out vec4 out_color;

void main() {
    vec3 dir = normalize ( in_dir );
    float theta = atan ( dir.z, dir.x );
    float phi = asin ( dir.y );

    vec2 sample_uv;
    sample_uv.x = ( theta + PI ) / ( 2.0 * PI );
    sample_uv.y = ( phi + PI * 0.5 ) / PI;

    out_color = texture ( sampler2D ( color_texture, sampler_linear ), sample_uv );
    //out_color = vec4(normalize(in_dir) * 0.5 + 0.5, 1.0);
    //out_color = vec4 ( 1, 0, 0, 1 );
}
